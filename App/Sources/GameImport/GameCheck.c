// Game data checker for "Import Your Game". See GameCheck.h.
//
// The same three phases run on two kinds of source: a folder of loose files, or a plain Xbox 360
// disc image read in place (the XDVDFS file system of its game partition). Both are only read.
#include "GameCheck.h"

#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LISTED_PROBLEMS 25
#define SECTOR 2048LL

static void say(gamecheck_log_fn log, const char *fmt, ...) {
  char buffer[700];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  if (log) log(buffer);
}

static void set_message(gamecheck_report *out, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(out->message, sizeof out->message, fmt, args);
  va_end(args);
}

const char *gamecheck_state_name(int state) {
  switch (state) {
    case GC_VERIFIED: return "verified";
    case GC_IDENTIFIED_NO_MANIFEST: return "identified, no manifest";
    case GC_NOT_GAME_FOLDER: return "no default.xex";
    case GC_UNREADABLE_EXECUTABLE: return "unreadable executable";
    case GC_DIFFERENT_GAME: return "different game";
    case GC_UNSUPPORTED_EDITION: return "unsupported edition";
    case GC_INCOMPLETE: return "incomplete";
    case GC_BAD_MANIFEST: return "bad manifest";
    case GC_DISC_IMAGE_NOT_YET: return "disc image";
    case GC_NOT_DISC_IMAGE: return "not a disc image";
  }
  return "unknown";
}

static uint32_t be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const unsigned char *p) {
  return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void put_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}
static void put_le32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}
static void put_le16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
}

static void version_text(uint32_t v, char *text, size_t size) {
  snprintf(text, size, "%u.%u.%u.%u", (unsigned)(v >> 28), (unsigned)((v >> 24) & 0xF),
           (unsigned)((v >> 8) & 0xFFFF), (unsigned)(v & 0xFF));
}

static void join(char *out, size_t size, const char *a, const char *b) {
  snprintf(out, size, "%s/%s", a, b);
}

static void hex_digest(const unsigned char digest[CC_SHA256_DIGEST_LENGTH], char hex[65]) {
  for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
}

// SHA-256 of `size` bytes at `offset` of an open file. All checker calls run on one thread, so
// one static buffer is enough.
static int sha256_range(int fd, long long offset, long long size, char hex[65]) {
  static unsigned char buffer[1 << 20];
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  while (size > 0) {
    const size_t want = size < (long long)sizeof buffer ? (size_t)size : sizeof buffer;
    const ssize_t n = pread(fd, buffer, want, offset);
    if (n <= 0) return 0;
    CC_SHA256_Update(&ctx, buffer, (CC_LONG)n);
    offset += n;
    size -= n;
  }
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  hex_digest(digest, hex);
  return 1;
}

static void sha256_memory(const void *data, size_t size, char hex[65]) {
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(data, (CC_LONG)size, digest);
  hex_digest(digest, hex);
}

static int sha256_file(const char *path, char hex[65]) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  struct stat st;
  const int ok = fstat(fd, &st) == 0 && sha256_range(fd, 0, st.st_size, hex);
  close(fd);
  return ok;
}

// Reads the execution-info optional header of a XEX2 file at `base` (plain, not encrypted).
static int read_xex_identity(int fd, long long base, gamecheck_report *out) {
  unsigned char head[0x18];
  if (pread(fd, head, sizeof head, base) != (ssize_t)sizeof head || memcmp(head, "XEX2", 4) != 0)
    return 0;
  const uint32_t count = be32(head + 0x14);
  for (uint32_t i = 0; i < count && i < 256; i++) {
    unsigned char entry[8];
    if (pread(fd, entry, 8, base + 0x18 + 8 * (long long)i) != 8) return 0;
    if (be32(entry) != 0x00040006u) continue;  // execution info
    unsigned char info[0x18];
    if (pread(fd, info, sizeof info, base + be32(entry + 4)) != (ssize_t)sizeof info) return 0;
    out->media_id = be32(info + 0x0);
    out->version = be32(info + 0x4);
    out->title_id = be32(info + 0xC);
    out->has_identity = 1;
    return 1;
  }
  return 0;
}

// --- Sources: a folder, or the files inside a disc image ----------------------------------------

typedef struct {
  char *path;  // relative, '/' separated, as stored in the image
  long long offset;
  long long size;
} image_file;

typedef struct {
  int is_image;
  const char *root;  // folder
  int fd;            // image
  image_file *files;
  int count;
  int capacity;
} source;

// Opens a file of the source. On success *fd is the descriptor to read, *owned says whether the
// caller must close it, and the file's bytes are [*offset, *offset + *size).
static int source_open(const source *src, const char *relative, int *fd, int *owned,
                       long long *offset, long long *size) {
  if (!src->is_image) {
    char path[2048];
    join(path, sizeof path, src->root, relative);
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    *fd = open(path, O_RDONLY);
    if (*fd < 0) return 0;
    *owned = 1;
    *offset = 0;
    *size = st.st_size;
    return 1;
  }
  for (int i = 0; i < src->count; i++) {
    if (strcasecmp(src->files[i].path, relative) == 0) {  // XDVDFS names ignore case
      *fd = src->fd;
      *owned = 0;
      *offset = src->files[i].offset;
      *size = src->files[i].size;
      return 1;
    }
  }
  return 0;
}

typedef struct {
  char edition[96];
  uint32_t title_id;
  char xex_sha256[65];
  int has_title;
} manifest_head;

// Parses the manifest header lines. Returns the open file positioned at the start, or NULL.
static FILE *open_manifest(const char *path, manifest_head *head) {
  FILE *file = fopen(path, "r");
  if (!file) return NULL;
  memset(head, 0, sizeof *head);
  char line[1400];
  int saw_magic = 0;
  while (fgets(line, sizeof line, file)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, "# nfsmw-manifest 1", 18) == 0) saw_magic = 1;
    else if (strncmp(line, "edition ", 8) == 0) snprintf(head->edition, sizeof head->edition, "%s", line + 8);
    else if (strncmp(line, "title ", 6) == 0) {
      head->title_id = (uint32_t)strtoul(line + 6, NULL, 16);
      head->has_title = 1;
    } else if (strncmp(line, "xex-sha256 ", 11) == 0) {
      snprintf(head->xex_sha256, sizeof head->xex_sha256, "%s", line + 11);
    }
  }
  if (!saw_magic || !head->has_title || strlen(head->xex_sha256) != 64) {
    fclose(file);
    return NULL;
  }
  rewind(file);
  return file;
}

static int verify_source(const source *src, const char *manifest, int full, gamecheck_log_fn log,
                         gamecheck_report *out) {
  const char *where = src->is_image ? "disc image" : "folder";
  char version[32];
  int fd, owned;
  long long offset, size;

  // Phase 1: structure.
  if (!source_open(src, "default.xex", &fd, &owned, &offset, &size)) {
    say(log, "Structure: default.xex not found");
    if (src->is_image)
      set_message(out, "This disc image has no default.xex. It may be damaged or not a game "
                       "disc. Choose another image, or the game folder.");
    else
      set_message(out, "This folder has no default.xex. Choose the folder that contains the "
                       "game's files (the one with default.xex in it).");
    return out->state = GC_NOT_GAME_FOLDER;
  }
  say(log, "Structure: default.xex found in the %s (%lld bytes)", where, size);

  // Phase 2: identity.
  const int identity = read_xex_identity(fd, offset, out);
  const int hashed = identity && sha256_range(fd, offset, size, out->xex_sha256);
  if (owned) close(fd);
  if (!identity) {
    say(log, "Identity: default.xex has no readable XEX2 header");
    set_message(out, "default.xex is not a readable Xbox 360 executable. The file may be damaged "
                     "or from another platform. Copy it again from your disc and choose again.");
    return out->state = GC_UNREADABLE_EXECUTABLE;
  }
  version_text(out->version, version, sizeof version);
  say(log, "Identity: title ID %08X, media ID %08X, executable version %s", out->title_id,
      out->media_id, version);
  if (out->title_id != GAMECHECK_TITLE_ID) {
    set_message(out, "This is not Need for Speed: Most Wanted (2012). Its title ID is %08X; the "
                     "supported game has %08X. Choose your NFS Most Wanted (2012) copy.",
                out->title_id, GAMECHECK_TITLE_ID);
    return out->state = GC_DIFFERENT_GAME;
  }
  if (!hashed) {
    say(log, "Identity: default.xex could not be read to the end");
    set_message(out, "default.xex could not be read completely. Copy it again and choose again.");
    return out->state = GC_UNREADABLE_EXECUTABLE;
  }
  say(log, "Identity: default.xex SHA-256 %s", out->xex_sha256);

  if (!manifest) {
    set_message(out, "This is Need for Speed: Most Wanted (2012) (media ID %08X, version %s). No "
                     "edition is supported yet, so the app cannot yet check that every file is "
                     "present and undamaged.",
                out->media_id, version);
    return out->state = GC_IDENTIFIED_NO_MANIFEST;
  }
  manifest_head head;
  FILE *list = open_manifest(manifest, &head);
  if (!list) {
    say(log, "Manifest: could not be read");
    set_message(out, "The app's list of game files could not be read. This is an app problem, "
                     "not a problem with your copy.");
    return out->state = GC_BAD_MANIFEST;
  }
  snprintf(out->edition, sizeof out->edition, "%s", head.edition);
  if (head.title_id != out->title_id || strcmp(head.xex_sha256, out->xex_sha256) != 0) {
    fclose(list);
    say(log, "Identity: executable is not the one of edition \"%s\"", head.edition);
    set_message(out, "This copy (media ID %08X, version %s) is not a supported edition. Supported: "
                     "%s. Your copy is not supported yet.",
                out->media_id, version, head.edition);
    return out->state = GC_UNSUPPORTED_EDITION;
  }
  say(log, "Identity: matches edition \"%s\"", head.edition);

  // Phase 3: completeness.
  char line[1400];
  while (fgets(line, sizeof line, list)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, "file ", 5) != 0) continue;
    char hash[65] = {0};
    long long expected = -1;
    int used = 0;
    if (sscanf(line + 5, "%64s %lld %n", hash, &expected, &used) < 2 || used == 0) continue;
    const char *relative = line + 5 + used;
    out->files_listed++;
    if (!source_open(src, relative, &fd, &owned, &offset, &size)) {
      if (++out->files_missing <= MAX_LISTED_PROBLEMS) say(log, "Missing: %s", relative);
      continue;
    }
    int damaged = 0;
    if (size != expected) {
      damaged = 1;
      if (out->files_missing + out->files_damaged < MAX_LISTED_PROBLEMS)
        say(log, "Wrong size: %s (%lld bytes, expected %lld)", relative, size, expected);
    } else if (full) {
      char actual[65];
      if (!sha256_range(fd, offset, size, actual) || strcmp(actual, hash) != 0) {
        damaged = 1;
        if (out->files_missing + out->files_damaged < MAX_LISTED_PROBLEMS)
          say(log, "Does not match the original: %s", relative);
      }
    }
    out->files_damaged += damaged;
    if (owned) close(fd);
  }
  fclose(list);
  say(log, "Completeness: %d files listed, %d missing, %d damaged (%s check)", out->files_listed,
      out->files_missing, out->files_damaged, full ? "full" : "quick, sizes only");
  if (out->files_missing || out->files_damaged) {
    set_message(out, "%d file%s missing and %d do%s not match the original. Copy the game again "
                     "from your disc, then choose it again.",
                out->files_missing, out->files_missing == 1 ? " is" : "s are", out->files_damaged,
                out->files_damaged == 1 ? "es" : "");
    return out->state = GC_INCOMPLETE;
  }
  set_message(out, "Game data verified: %s, %d files%s.", head.edition, out->files_listed,
              full ? "" : " (sizes checked; run Verify Again for a full check)");
  return out->state = GC_VERIFIED;
}

int gamecheck_verify_folder(const char *root, const char *manifest, int full, gamecheck_log_fn log,
                            gamecheck_report *out) {
  memset(out, 0, sizeof *out);
  source src = {0};
  src.root = root;
  return verify_source(&src, manifest, full, log, out);
}

// --- Xbox 360 disc images (XDVDFS) --------------------------------------------------------------

static const char kXdvdfsMagic[] = "MICROSOFT*XBOX*MEDIA";

// Returns the byte offset of the game partition, or -1. The XDVDFS volume descriptor sits at
// sector 32 of the partition, which starts at one of these offsets depending on the disc format
// (0 for an image of the game partition alone).
static long long find_partition(int fd) {
  static const long long partitions[] = {0, 0x18300000LL, 0xFD90000LL, 0x2080000LL};
  for (size_t i = 0; i < sizeof partitions / sizeof partitions[0]; i++) {
    char block[20];
    if (pread(fd, block, sizeof block, partitions[i] + 32 * SECTOR) == (ssize_t)sizeof block &&
        memcmp(block, kXdvdfsMagic, 20) == 0)
      return partitions[i];
  }
  return -1;
}

static int add_image_file(source *src, const char *path, long long offset, long long size) {
  if (src->count == src->capacity) {
    const int capacity = src->capacity ? src->capacity * 2 : 256;
    image_file *grown = realloc(src->files, (size_t)capacity * sizeof *grown);
    if (!grown) return 0;
    src->files = grown;
    src->capacity = capacity;
  }
  src->files[src->count].path = strdup(path);
  if (!src->files[src->count].path) return 0;
  src->files[src->count].offset = offset;
  src->files[src->count].size = size;
  src->count++;
  return 1;
}

// Reads one directory table and every table below it. A table is a binary tree of entries:
// left and right child offsets (in 4-byte units from the table start, 0 for none), start sector,
// size, attributes (0x10 = directory), name length and name. Unused space is 0xFF.
static int walk_directory(source *src, long long partition, uint32_t sector, uint32_t size,
                          const char *prefix, int depth) {
  if (size == 0) return 1;
  if (depth > 32 || size > (64u << 20)) return 0;
  unsigned char *table = malloc(size);
  if (!table) return 0;
  if (pread(src->fd, table, size, partition + sector * SECTOR) != (ssize_t)size) {
    free(table);
    return 0;
  }
  const uint32_t slots = size / 4;
  unsigned char *seen = calloc(slots ? slots : 1, 1);
  uint32_t *stack = malloc(sizeof *stack * (2 * (size_t)slots + 2));  // each entry pushes <= 2
  int ok = seen && stack;
  int top = 0;
  if (ok) stack[top++] = 0;
  while (ok && top > 0) {
    const uint32_t slot = stack[--top];
    if (slot >= slots || seen[slot]) continue;
    seen[slot] = 1;
    const unsigned char *e = table + (size_t)slot * 4;
    const size_t room = size - (size_t)slot * 4;
    if (room < 14) continue;
    const uint16_t left = le16(e), right = le16(e + 2);
    if (left == 0xFFFF && right == 0xFFFF) continue;  // padding
    const uint32_t start = le32(e + 4), length = le32(e + 8);
    const unsigned attributes = e[12], name_length = e[13];
    if (name_length == 0 || 14 + (size_t)name_length > room) continue;
    char name[256];
    memcpy(name, e + 14, name_length);
    name[name_length] = 0;
    char path[1200];
    if (prefix[0]) snprintf(path, sizeof path, "%s/%s", prefix, name);
    else snprintf(path, sizeof path, "%s", name);
    if (attributes & 0x10) {
      ok = walk_directory(src, partition, start, length, path, depth + 1);
    } else {
      ok = add_image_file(src, path, partition + start * SECTOR, length);
    }
    if (left) stack[top++] = left;
    if (right) stack[top++] = right;
  }
  free(stack);
  free(seen);
  free(table);
  return ok;
}

static void free_source(source *src) {
  for (int i = 0; i < src->count; i++) free(src->files[i].path);
  free(src->files);
  if (src->is_image && src->fd >= 0) close(src->fd);
}

int gamecheck_verify_image(const char *path, const char *manifest, int full, gamecheck_log_fn log,
                           gamecheck_report *out) {
  memset(out, 0, sizeof *out);
  source src = {0};
  src.is_image = 1;
  src.fd = open(path, O_RDONLY);
  if (src.fd < 0) {
    say(log, "Disc image: cannot open (%s)", strerror(errno));
    set_message(out, "The file could not be opened. Choose it again.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  const long long partition = find_partition(src.fd);
  unsigned char descriptor[0x20];
  if (partition < 0 ||
      pread(src.fd, descriptor, sizeof descriptor, partition + 32 * SECTOR) != (ssize_t)sizeof descriptor) {
    free_source(&src);
    set_message(out, "This file is not an Xbox 360 disc image. Choose your game folder, or a "
                     "plain .iso of your own disc.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  say(log, "Disc image: Xbox 360 file system found (game partition at 0x%llX)", partition);
  if (!walk_directory(&src, partition, le32(descriptor + 0x14), le32(descriptor + 0x18), "", 0)) {
    say(log, "Disc image: the directory could not be read completely (%d files so far)", src.count);
    free_source(&src);
    set_message(out, "The disc image's file list is damaged or incomplete. Make the image again "
                     "from your disc and choose it again.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  say(log, "Disc image: %d files listed", src.count);
  const int state = verify_source(&src, manifest, full, log, out);
  free_source(&src);
  return state;
}

int gamecheck_probe_image(const char *path, gamecheck_log_fn log, gamecheck_report *out) {
  memset(out, 0, sizeof *out);
  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    say(log, "Disc image: cannot open (%s)", strerror(errno));
    set_message(out, "The file could not be opened. Choose it again.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  const long long partition = find_partition(fd);
  close(fd);
  if (partition < 0) {
    set_message(out, "This file is not an Xbox 360 disc image.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  say(log, "Disc image: Xbox 360 file system found (game partition at 0x%llX)", partition);
  set_message(out, "This is an Xbox 360 disc image.");
  return out->state = GC_DISC_IMAGE_NOT_YET;
}

// --- Export and file list ----------------------------------------------------------------------

// Folder listing for nftw, which takes no context argument (checker calls run on one thread).
static source *g_walk_source;
static size_t g_walk_root_length;
static int g_walk_ok;

static int list_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)ftw;
  if (flag == FTW_F && S_ISREG(st->st_mode) && strlen(path) > g_walk_root_length + 1) {
    if (!add_image_file(g_walk_source, path + g_walk_root_length + 1, 0, st->st_size)) g_walk_ok = 0;
  }
  return g_walk_ok ? 0 : 1;
}

// Opens a folder or a disc image and lists every file in it.
static int open_source(const char *path, int is_image, source *src, gamecheck_log_fn log) {
  memset(src, 0, sizeof *src);
  src->fd = -1;
  if (!is_image) {
    src->root = path;
    g_walk_source = src;
    g_walk_root_length = strlen(path);
    while (g_walk_root_length > 1 && path[g_walk_root_length - 1] == '/') g_walk_root_length--;
    g_walk_ok = 1;
    if (nftw(path, list_entry, 16, FTW_PHYS) != 0 || !g_walk_ok) {
      say(log, "Folder: could not be listed completely (%d files so far)", src->count);
      return 0;
    }
    say(log, "Folder: %d files listed", src->count);
    return 1;
  }
  src->is_image = 1;
  src->fd = open(path, O_RDONLY);
  if (src->fd < 0) {
    say(log, "Disc image: cannot open (%s)", strerror(errno));
    return 0;
  }
  const long long partition = find_partition(src->fd);
  unsigned char descriptor[0x20];
  if (partition < 0 ||
      pread(src->fd, descriptor, sizeof descriptor, partition + 32 * SECTOR) != (ssize_t)sizeof descriptor) {
    say(log, "Disc image: not an Xbox 360 disc image");
    return 0;
  }
  if (!walk_directory(src, partition, le32(descriptor + 0x14), le32(descriptor + 0x18), "", 0)) {
    say(log, "Disc image: the directory could not be read completely (%d files so far)", src->count);
    return 0;
  }
  say(log, "Disc image: %d files listed", src->count);
  return 1;
}

int gamecheck_export_file(const char *path, int is_image, const char *relative, const char *destination,
                          gamecheck_log_fn log) {
  source src;
  int ok = 0;
  if (open_source(path, is_image, &src, log)) {
    int fd, owned;
    long long offset, size;
    if (!source_open(&src, relative, &fd, &owned, &offset, &size)) {
      say(log, "Export: %s not found", relative);
    } else {
      const int out = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (out < 0) {
        say(log, "Export: cannot create the copy (%s)", strerror(errno));
      } else {
        static unsigned char buffer[1 << 20];
        long long done = 0;
        ok = 1;
        while (ok && done < size) {
          const size_t want = size - done < (long long)sizeof buffer ? (size_t)(size - done) : sizeof buffer;
          const ssize_t n = pread(fd, buffer, want, offset + done);
          ok = n > 0 && write(out, buffer, (size_t)n) == n;
          if (ok) done += n;
        }
        ok = close(out) == 0 && ok;
        if (ok) {
          char hash[65];
          const int hashed = sha256_file(destination, hash);
          say(log, "Export: %s copied (%lld bytes, SHA-256 %s)", relative, size, hashed ? hash : "?");
        } else {
          say(log, "Export: copying %s failed", relative);
          unlink(destination);
        }
      }
      if (owned) close(fd);
    }
  }
  free_source(&src);
  return ok;
}

static int compare_files(const void *a, const void *b) {
  return strcasecmp(((const image_file *)a)->path, ((const image_file *)b)->path);
}

int gamecheck_write_manifest(const char *path, int is_image, const char *destination, gamecheck_log_fn log) {
  source src;
  int ok = 0;
  FILE *out = NULL;
  if (!open_source(path, is_image, &src, log)) goto done;
  qsort(src.files, (size_t)src.count, sizeof src.files[0], compare_files);

  // Header from default.xex: identity and SHA-256.
  gamecheck_report identity;
  memset(&identity, 0, sizeof identity);
  int fd, owned;
  long long offset, size;
  if (!source_open(&src, "default.xex", &fd, &owned, &offset, &size)) {
    say(log, "File list: default.xex not found");
    goto done;
  }
  const int known = read_xex_identity(fd, offset, &identity) &&
                    sha256_range(fd, offset, size, identity.xex_sha256);
  if (owned) close(fd);
  if (!known) {
    say(log, "File list: default.xex is not a readable XEX2 file");
    goto done;
  }
  char version[32];
  version_text(identity.version, version, sizeof version);
  out = fopen(destination, "w");
  if (!out) {
    say(log, "File list: cannot create the file (%s)", strerror(errno));
    goto done;
  }
  fprintf(out, "# nfsmw-manifest 1\n");
  fprintf(out, "edition %08X-%s Title %08X, media ID %08X, version %s\n", identity.media_id, version,
          identity.title_id, identity.media_id, version);
  fprintf(out, "title %08X\nxex-sha256 %s\n", identity.title_id, identity.xex_sha256);
  long long total = 0;
  ok = 1;
  for (int i = 0; i < src.count && ok; i++) {
    char hash[65];
    if (!source_open(&src, src.files[i].path, &fd, &owned, &offset, &size)) {
      ok = 0;
      break;
    }
    ok = sha256_range(fd, offset, size, hash);
    if (owned) close(fd);
    if (!ok) {
      say(log, "File list: %s could not be read", src.files[i].path);
      break;
    }
    fprintf(out, "file %s %lld %s\n", hash, size, src.files[i].path);
    total += size;
    if ((i + 1) % 100 == 0) say(log, "File list: %d of %d files", i + 1, src.count);
  }
  if (ok) say(log, "File list: %d files, %.2f GB, title %08X, media ID %08X, version %s", src.count,
              (double)total / 1073741824.0, identity.title_id, identity.media_id, version);
done:
  if (out && fclose(out) != 0) ok = 0;
  if (!ok && out) unlink(destination);
  free_source(&src);
  return ok;
}

// --- Synthetic test data (no game content) ------------------------------------------------------

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st;
  (void)flag;
  (void)ftw;
  return remove(path);
}

static void remove_tree(const char *path) {
  nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static int write_file(const char *path, const unsigned char *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (!file) return 0;
  const int ok = fwrite(data, 1, size, file) == size;
  return fclose(file) == 0 && ok;
}

#define FAKE_XEX_SIZE 0x400

static void fake_xex(unsigned char xex[FAKE_XEX_SIZE], uint32_t title_id, uint32_t media_id) {
  memset(xex, 0, FAKE_XEX_SIZE);
  memcpy(xex, "XEX2", 4);
  put_be32(xex + 0x14, 1);           // one optional header
  put_be32(xex + 0x18, 0x00040006);  // execution info
  put_be32(xex + 0x1C, 0x100);       // at offset 0x100
  put_be32(xex + 0x100, media_id);
  put_be32(xex + 0x104, 0x10000100); // version 1.0.1.0
  put_be32(xex + 0x10C, title_id);
  memcpy(xex + 0x200, "SYNTHETIC TEST FILE - NOT GAME DATA", 35);
}

static int write_fake_xex(const char *path, uint32_t title_id, uint32_t media_id) {
  unsigned char xex[FAKE_XEX_SIZE];
  fake_xex(xex, title_id, media_id);
  return write_file(path, xex, sizeof xex);
}

static const struct {
  const char *dir;
  const char *name;
  const char *path;
  size_t size;
} kTestFiles[] = {
    {"Global", "Global Data.bin", "Global/Global Data.bin", 64 * 1024},
    {"Movies", "intro.bin", "Movies/intro.bin", 200 * 1024},
    {"Sound", "bank.bin", "Sound/bank.bin", 1024 * 1024},
};
#define TEST_FILE_COUNT ((int)(sizeof kTestFiles / sizeof kTestFiles[0]))

static unsigned char *test_file_data(int i) {
  unsigned char *data = malloc(kTestFiles[i].size);
  if (data)
    for (size_t b = 0; b < kTestFiles[i].size; b++) data[b] = (unsigned char)(b * 31 + (size_t)i * 7);
  return data;
}

// Writes the manifest of the complete synthetic copy (the same for folders and images).
static int write_test_manifest(const char *manifest_path) {
  unsigned char xex[FAKE_XEX_SIZE];
  char hash[65];
  fake_xex(xex, GAMECHECK_TITLE_ID, 0x7E57DA7A);
  sha256_memory(xex, sizeof xex, hash);
  FILE *manifest = fopen(manifest_path, "w");
  if (!manifest) return 0;
  fprintf(manifest, "# nfsmw-manifest 1\nedition synthetic-test Synthetic test edition (no game "
                    "data)\ntitle %08X\nxex-sha256 %s\n",
          GAMECHECK_TITLE_ID, hash);
  for (int i = 0; i < TEST_FILE_COUNT; i++) {
    unsigned char *data = test_file_data(i);
    if (!data) {
      fclose(manifest);
      return 0;
    }
    sha256_memory(data, kTestFiles[i].size, hash);
    free(data);
    fprintf(manifest, "file %s %zu %s\n", hash, kTestFiles[i].size, kTestFiles[i].path);
  }
  return fclose(manifest) == 0;
}

int gamecheck_make_test_folder(const char *root, const char *manifest_path, int variant,
                               gamecheck_log_fn log) {
  char path[2048];
  remove_tree(root);
  if (mkdir(root, 0755) != 0) {
    say(log, "Test folder: cannot create (%s)", strerror(errno));
    return 0;
  }
  if (!write_test_manifest(manifest_path)) return 0;
  join(path, sizeof path, root, "default.xex");
  if (!write_fake_xex(path, GAMECHECK_TITLE_ID, 0x7E57DA7A)) return 0;
  for (int i = 0; i < TEST_FILE_COUNT; i++) {
    join(path, sizeof path, root, kTestFiles[i].dir);
    mkdir(path, 0755);
    unsigned char *data = test_file_data(i);
    if (!data) return 0;
    join(path, sizeof path, root, kTestFiles[i].path);
    const int ok = write_file(path, data, kTestFiles[i].size);
    free(data);
    if (!ok) return 0;
  }

  switch (variant) {
    case 1:  // one file missing
      join(path, sizeof path, root, kTestFiles[2].path);
      remove(path);
      break;
    case 2: {  // one file damaged, same size
      join(path, sizeof path, root, kTestFiles[1].path);
      FILE *file = fopen(path, "r+b");
      if (!file) return 0;
      fseek(file, 1000, SEEK_SET);
      fputc(0xEE, file);
      fclose(file);
      break;
    }
    case 3:  // a different game
      join(path, sizeof path, root, "default.xex");
      if (!write_fake_xex(path, 0x4D5307D5, 0x7E57DA7A)) return 0;
      break;
    case 4:  // no default.xex
      join(path, sizeof path, root, "default.xex");
      remove(path);
      break;
    case 5:  // another edition: same game, different executable
      join(path, sizeof path, root, "default.xex");
      if (!write_fake_xex(path, GAMECHECK_TITLE_ID, 0x0BADED17)) return 0;
      break;
    default:
      break;
  }
  return 1;
}

// Writes one directory entry; returns its length in bytes (padded to 4).
static size_t put_entry(unsigned char *p, uint16_t right, uint32_t sector, uint32_t size,
                        unsigned attributes, const char *name) {
  const size_t name_length = strlen(name);
  put_le16(p, 0);  // no left child: the entries form a chain through their right children
  put_le16(p + 2, right);
  put_le32(p + 4, sector);
  put_le32(p + 8, size);
  p[12] = (unsigned char)attributes;
  p[13] = (unsigned char)name_length;
  memcpy(p + 14, name, name_length);
  return (14 + name_length + 3) & ~(size_t)3;
}

int gamecheck_make_test_image(const char *path, const char *manifest_path, int variant,
                              gamecheck_log_fn log) {
  // Layout (game partition at 0): sector 32 volume descriptor, 33 root directory, 34-36 one
  // directory each for Global, Movies and Sound, then default.xex and the files.
  const uint32_t root_sector = 33, first_data = 37;
  uint32_t file_sector[TEST_FILE_COUNT];
  const uint32_t xex_sector = first_data;
  uint32_t next = xex_sector + 1;
  for (int i = 0; i < TEST_FILE_COUNT; i++) {
    file_sector[i] = next;
    next += (uint32_t)((kTestFiles[i].size + SECTOR - 1) / SECTOR);
  }
  const size_t image_size = (size_t)next * SECTOR;
  unsigned char *image = calloc(1, image_size);
  if (!image) return 0;

  unsigned char *descriptor = image + 32 * SECTOR;
  memcpy(descriptor, kXdvdfsMagic, 20);
  put_le32(descriptor + 0x14, root_sector);
  put_le32(descriptor + 0x18, (uint32_t)SECTOR);
  memcpy(descriptor + 0x7EC, kXdvdfsMagic, 20);

  // Root directory: default.xex, then the three folders (Sound left out for "one file missing").
  unsigned char *root = image + root_sector * SECTOR;
  memset(root, 0xFF, SECTOR);
  const int folders = variant == 1 ? TEST_FILE_COUNT - 1 : TEST_FILE_COUNT;
  size_t at = 0;
  size_t lengths[1 + TEST_FILE_COUNT];
  lengths[0] = 14 + strlen("default.xex");
  for (int i = 0; i < folders; i++) lengths[1 + i] = 14 + strlen(kTestFiles[i].dir);
  for (int e = 0; e <= folders; e++) {
    const size_t padded = (lengths[e] + 3) & ~(size_t)3;
    const uint16_t right = e < folders ? (uint16_t)((at + padded) / 4) : 0;
    if (e == 0) {
      at += put_entry(root + at, right, xex_sector, FAKE_XEX_SIZE, 0x80, "default.xex");
    } else {
      at += put_entry(root + at, right, 34 + (uint32_t)(e - 1), (uint32_t)SECTOR, 0x10,
                      kTestFiles[e - 1].dir);
    }
  }
  for (int i = 0; i < TEST_FILE_COUNT; i++) {
    unsigned char *table = image + (34 + i) * SECTOR;
    memset(table, 0xFF, SECTOR);
    put_entry(table, 0, file_sector[i], (uint32_t)kTestFiles[i].size, 0x80, kTestFiles[i].name);
  }

  fake_xex(image + xex_sector * SECTOR, GAMECHECK_TITLE_ID, 0x7E57DA7A);
  for (int i = 0; i < TEST_FILE_COUNT; i++) {
    unsigned char *data = test_file_data(i);
    if (!data) {
      free(image);
      return 0;
    }
    memcpy(image + file_sector[i] * SECTOR, data, kTestFiles[i].size);
    free(data);
  }
  if (variant == 2) image[file_sector[1] * SECTOR + 1000] ^= 0xFF;  // damaged, same size

  const int ok = write_file(path, image, image_size) && write_test_manifest(manifest_path);
  free(image);
  if (!ok) say(log, "Test image: could not be written (%s)", strerror(errno));
  return ok;
}

int gamecheck_selftest(const char *work_dir, gamecheck_log_fn log) {
  static const struct {
    int image;
    int variant;
    int use_manifest;
    int full;
    int expected;
    const char *name;
  } kCases[] = {
      {0, 0, 1, 1, GC_VERIFIED, "folder: complete copy, full check"},
      {0, 0, 1, 0, GC_VERIFIED, "folder: complete copy, quick check"},
      {0, 1, 1, 0, GC_INCOMPLETE, "folder: one file missing"},
      {0, 2, 1, 1, GC_INCOMPLETE, "folder: one file damaged (full check)"},
      {0, 2, 1, 0, GC_VERIFIED, "folder: one file damaged, quick check only sees sizes"},
      {0, 3, 1, 1, GC_DIFFERENT_GAME, "folder: a different game"},
      {0, 4, 1, 1, GC_NOT_GAME_FOLDER, "folder: no default.xex"},
      {0, 5, 1, 1, GC_UNSUPPORTED_EDITION, "folder: another edition"},
      {0, 6, 0, 1, GC_IDENTIFIED_NO_MANIFEST, "folder: right game, no manifest"},
      {1, 0, 1, 1, GC_VERIFIED, "disc image: complete copy, full check"},
      {1, 1, 1, 1, GC_INCOMPLETE, "disc image: one file missing"},
      {1, 2, 1, 1, GC_INCOMPLETE, "disc image: one file damaged"},
      {1, 0, 0, 1, GC_IDENTIFIED_NO_MANIFEST, "disc image: right game, no manifest"},
  };
  char root[1024], image[1024], manifest[1024];
  mkdir(work_dir, 0755);
  snprintf(root, sizeof root, "%s/game", work_dir);
  snprintf(image, sizeof image, "%s/game.iso", work_dir);
  snprintf(manifest, sizeof manifest, "%s/manifest.txt", work_dir);
  int passed = 0;
  const int total = (int)(sizeof kCases / sizeof kCases[0]);
  for (int i = 0; i < total; i++) {
    const int made = kCases[i].image ? gamecheck_make_test_image(image, manifest, kCases[i].variant, log)
                                     : gamecheck_make_test_folder(root, manifest, kCases[i].variant, log);
    if (!made) {
      say(log, "  FAIL %s: could not build the test data", kCases[i].name);
      continue;
    }
    gamecheck_report report;
    const char *list = kCases[i].use_manifest ? manifest : NULL;
    const int state = kCases[i].image ? gamecheck_verify_image(image, list, kCases[i].full, NULL, &report)
                                      : gamecheck_verify_folder(root, list, kCases[i].full, NULL, &report);
    const int ok = state == kCases[i].expected;
    passed += ok;
    say(log, "  %s %s: %s", ok ? "ok  " : "FAIL", kCases[i].name, gamecheck_state_name(state));
    say(log, "       \"%s\"", report.message);
  }

  // Export and file list on the complete test image.
  char exported[1024], listed[1024];
  snprintf(exported, sizeof exported, "%s/exported.xex", work_dir);
  snprintf(listed, sizeof listed, "%s/listed.txt", work_dir);
  int tools_ok = gamecheck_make_test_image(image, manifest, 0, log) &&
                 gamecheck_export_file(image, 1, "default.xex", exported, NULL);
  if (tools_ok) {
    unsigned char xex[FAKE_XEX_SIZE];
    char want[65], got[65];
    fake_xex(xex, GAMECHECK_TITLE_ID, 0x7E57DA7A);
    sha256_memory(xex, sizeof xex, want);
    tools_ok = sha256_file(exported, got) && strcmp(want, got) == 0;
  }
  say(log, "  %s export default.xex from a disc image", tools_ok ? "ok  " : "FAIL");
  int list_ok = gamecheck_write_manifest(image, 1, listed, NULL);
  if (list_ok) {
    gamecheck_report listed_report;
    list_ok = gamecheck_verify_image(image, listed, 1, NULL, &listed_report) == GC_VERIFIED &&
              listed_report.files_listed == TEST_FILE_COUNT + 1;
  }
  say(log, "  %s file list of a disc image, then verified against it", list_ok ? "ok  " : "FAIL");
  remove(exported);
  remove(listed);

  // A file that is not a disc image.
  unsigned char junk[4096];
  memset(junk, 0x5A, sizeof junk);
  gamecheck_report report = {0};
  const int junk_ok = write_file(image, junk, sizeof junk) &&
                      gamecheck_verify_image(image, manifest, 1, NULL, &report) == GC_NOT_DISC_IMAGE;
  say(log, "  %s not a disc image: %s", junk_ok ? "ok  " : "FAIL", gamecheck_state_name(report.state));
  remove(image);
  remove_tree(root);
  remove(manifest);

  const int all = passed == total && junk_ok && tools_ok && list_ok;
  say(log, "%s: importer checks %d of %d correct, non-image rejected %s, export %s, file list %s",
      all ? "PASS" : "FAIL", passed, total, junk_ok ? "yes" : "no", tools_ok ? "ok" : "FAILED",
      list_ok ? "ok" : "FAILED");
  return all;
}
