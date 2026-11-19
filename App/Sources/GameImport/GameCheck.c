// Game data checker for "Import Your Game". See GameCheck.h.
#include "GameCheck.h"

#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LISTED_PROBLEMS 25

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
    case GC_NOT_GAME_FOLDER: return "not a game folder";
    case GC_UNREADABLE_EXECUTABLE: return "unreadable executable";
    case GC_DIFFERENT_GAME: return "different game";
    case GC_UNSUPPORTED_EDITION: return "unsupported edition";
    case GC_INCOMPLETE: return "incomplete";
    case GC_BAD_MANIFEST: return "bad manifest";
    case GC_DISC_IMAGE_NOT_YET: return "disc image (not supported yet)";
    case GC_NOT_DISC_IMAGE: return "not a disc image";
  }
  return "unknown";
}

static uint32_t be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void version_text(uint32_t v, char *text, size_t size) {
  snprintf(text, size, "%u.%u.%u.%u", (unsigned)(v >> 28), (unsigned)((v >> 24) & 0xF),
           (unsigned)((v >> 8) & 0xFFFF), (unsigned)(v & 0xFF));
}

static void join(char *out, size_t size, const char *a, const char *b) {
  snprintf(out, size, "%s/%s", a, b);
}

// SHA-256 of a whole file as lowercase hex. Returns 0 when the file cannot be read.
static int sha256_file(const char *path, char hex[65]) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  static unsigned char buffer[1 << 20];
  ssize_t n;
  while ((n = read(fd, buffer, sizeof buffer)) > 0) {
    CC_SHA256_Update(&ctx, buffer, (CC_LONG)n);
  }
  close(fd);
  if (n < 0) return 0;
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  return 1;
}

// Reads the execution-info optional header of a XEX2 file (plain, not encrypted).
// Returns 1 when the identity was read.
static int read_xex_identity(const char *path, gamecheck_report *out) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0) return 0;
  unsigned char head[0x18];
  int ok = 0;
  if (pread(fd, head, sizeof head, 0) == (ssize_t)sizeof head && memcmp(head, "XEX2", 4) == 0) {
    const uint32_t count = be32(head + 0x14);
    for (uint32_t i = 0; i < count && i < 256; i++) {
      unsigned char entry[8];
      if (pread(fd, entry, 8, 0x18 + 8 * (off_t)i) != 8) break;
      if (be32(entry) == 0x00040006u) {  // execution info
        unsigned char info[0x18];
        if (pread(fd, info, sizeof info, (off_t)be32(entry + 4)) == (ssize_t)sizeof info) {
          out->media_id = be32(info + 0x0);
          out->version = be32(info + 0x4);
          out->title_id = be32(info + 0xC);
          out->has_identity = 1;
          ok = 1;
        }
        break;
      }
    }
  }
  close(fd);
  return ok;
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

int gamecheck_verify_folder(const char *root, const char *manifest, int full, gamecheck_log_fn log,
                            gamecheck_report *out) {
  memset(out, 0, sizeof *out);
  char path[2048];
  char version[32];

  // Phase 1: structure.
  join(path, sizeof path, root, "default.xex");
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    say(log, "Structure: default.xex not found");
    set_message(out, "This folder has no default.xex. Choose the folder that contains the game's "
                     "files (the one with default.xex in it).");
    return out->state = GC_NOT_GAME_FOLDER;
  }
  say(log, "Structure: default.xex found (%lld bytes)", (long long)st.st_size);

  // Phase 2: identity.
  if (!read_xex_identity(path, out)) {
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
                     "supported game has %08X. Choose the folder of your NFS Most Wanted (2012) copy.",
                out->title_id, GAMECHECK_TITLE_ID);
    return out->state = GC_DIFFERENT_GAME;
  }
  if (!sha256_file(path, out->xex_sha256)) {
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
    long long size = -1;
    int used = 0;
    if (sscanf(line + 5, "%64s %lld %n", hash, &size, &used) < 2 || used == 0) continue;
    const char *relative = line + 5 + used;
    out->files_listed++;
    join(path, sizeof path, root, relative);
    struct stat file_st;
    if (stat(path, &file_st) != 0 || !S_ISREG(file_st.st_mode)) {
      if (++out->files_missing <= MAX_LISTED_PROBLEMS) say(log, "Missing: %s", relative);
      continue;
    }
    if ((long long)file_st.st_size != size) {
      if (out->files_missing + ++out->files_damaged <= MAX_LISTED_PROBLEMS)
        say(log, "Wrong size: %s (%lld bytes, expected %lld)", relative,
            (long long)file_st.st_size, size);
      continue;
    }
    if (full) {
      char actual[65];
      if (!sha256_file(path, actual) || strcmp(actual, hash) != 0) {
        if (out->files_missing + ++out->files_damaged <= MAX_LISTED_PROBLEMS)
          say(log, "Does not match the original: %s", relative);
      }
    }
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

int gamecheck_probe_image(const char *path, gamecheck_log_fn log, gamecheck_report *out) {
  memset(out, 0, sizeof *out);
  // XDVDFS volume descriptor at sector 32 of the game partition; the partition starts at one of
  // these offsets depending on the disc format (0 for an image of the game partition alone).
  static const long long partitions[] = {0, 0x18300000LL, 0xFD90000LL, 0x2080000LL};
  static const char magic[] = "MICROSOFT*XBOX*MEDIA";
  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    say(log, "Disc image: cannot open (%s)", strerror(errno));
    set_message(out, "The file could not be opened. Choose it again.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  int found = 0;
  for (size_t i = 0; i < sizeof partitions / sizeof partitions[0] && !found; i++) {
    char block[20];
    if (pread(fd, block, sizeof block, partitions[i] + 0x10000) == (ssize_t)sizeof block &&
        memcmp(block, magic, 20) == 0) {
      found = 1;
      say(log, "Disc image: Xbox 360 file system found (partition at 0x%llX)", partitions[i]);
    }
  }
  close(fd);
  if (!found) {
    set_message(out, "This file is not an Xbox 360 disc image. Choose your game folder, or a "
                     "plain .iso of your own disc.");
    return out->state = GC_NOT_DISC_IMAGE;
  }
  set_message(out, "This is an Xbox 360 disc image. Disc images are not supported yet; for now, "
                   "choose the extracted game folder.");
  return out->state = GC_DISC_IMAGE_NOT_YET;
}

// --- Synthetic test data ---------------------------------------------------------------------

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

static int write_fake_xex(const char *path, uint32_t title_id, uint32_t media_id) {
  unsigned char xex[0x400];
  memset(xex, 0, sizeof xex);
  memcpy(xex, "XEX2", 4);
  put_be32(xex + 0x14, 1);           // one optional header
  put_be32(xex + 0x18, 0x00040006);  // execution info
  put_be32(xex + 0x1C, 0x100);       // at offset 0x100
  put_be32(xex + 0x100, media_id);
  put_be32(xex + 0x104, 0x10000100); // version 1.0.1.0
  put_be32(xex + 0x10C, title_id);
  memcpy(xex + 0x200, "SYNTHETIC TEST FILE - NOT GAME DATA", 35);
  return write_file(path, xex, sizeof xex);
}

static const struct {
  const char *dir;
  const char *path;
  size_t size;
} kTestFiles[] = {
    {"Global", "Global/Global Data.bin", 64 * 1024},
    {"Movies", "Movies/intro.bin", 200 * 1024},
    {"Sound", "Sound/bank.bin", 1024 * 1024},
};

int gamecheck_make_test_folder(const char *root, const char *manifest_path, int variant,
                               gamecheck_log_fn log) {
  char path[2048];
  remove_tree(root);
  if (mkdir(root, 0755) != 0) {
    say(log, "Test folder: cannot create (%s)", strerror(errno));
    return 0;
  }
  join(path, sizeof path, root, "default.xex");
  if (!write_fake_xex(path, GAMECHECK_TITLE_ID, 0x7E57DA7A)) return 0;
  char xex_hash[65];
  if (!sha256_file(path, xex_hash)) return 0;

  FILE *manifest = fopen(manifest_path, "w");
  if (!manifest) return 0;
  fprintf(manifest, "# nfsmw-manifest 1\nedition synthetic-test Synthetic test edition (no game "
                    "data)\ntitle %08X\nxex-sha256 %s\n",
          GAMECHECK_TITLE_ID, xex_hash);
  for (size_t i = 0; i < sizeof kTestFiles / sizeof kTestFiles[0]; i++) {
    join(path, sizeof path, root, kTestFiles[i].dir);
    mkdir(path, 0755);
    unsigned char *data = malloc(kTestFiles[i].size);
    if (!data) {
      fclose(manifest);
      return 0;
    }
    for (size_t b = 0; b < kTestFiles[i].size; b++) data[b] = (unsigned char)(b * 31 + i * 7);
    join(path, sizeof path, root, kTestFiles[i].path);
    const int ok = write_file(path, data, kTestFiles[i].size);
    free(data);
    char hash[65];
    if (!ok || !sha256_file(path, hash)) {
      fclose(manifest);
      return 0;
    }
    fprintf(manifest, "file %s %zu %s\n", hash, kTestFiles[i].size, kTestFiles[i].path);
  }
  fclose(manifest);

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

int gamecheck_selftest(const char *work_dir, gamecheck_log_fn log) {
  static const struct {
    int variant;
    int use_manifest;
    int full;
    int expected;
    const char *name;
  } kCases[] = {
      {0, 1, 1, GC_VERIFIED, "complete copy, full check"},
      {0, 1, 0, GC_VERIFIED, "complete copy, quick check"},
      {1, 1, 0, GC_INCOMPLETE, "one file missing"},
      {2, 1, 1, GC_INCOMPLETE, "one file damaged (full check)"},
      {2, 1, 0, GC_VERIFIED, "one file damaged, quick check only sees sizes"},
      {3, 1, 1, GC_DIFFERENT_GAME, "a different game"},
      {4, 1, 1, GC_NOT_GAME_FOLDER, "no default.xex"},
      {5, 1, 1, GC_UNSUPPORTED_EDITION, "another edition"},
      {6, 0, 1, GC_IDENTIFIED_NO_MANIFEST, "right game, no manifest"},
  };
  char root[1024], manifest[1024];
  mkdir(work_dir, 0755);
  snprintf(root, sizeof root, "%s/game", work_dir);
  snprintf(manifest, sizeof manifest, "%s/manifest.txt", work_dir);
  int passed = 0;
  const int total = (int)(sizeof kCases / sizeof kCases[0]);
  for (int i = 0; i < total; i++) {
    if (!gamecheck_make_test_folder(root, manifest, kCases[i].variant, NULL)) {
      say(log, "  FAIL %s: could not build the test folder", kCases[i].name);
      continue;
    }
    gamecheck_report report;
    const int state =
        gamecheck_verify_folder(root, kCases[i].use_manifest ? manifest : NULL, kCases[i].full, NULL, &report);
    const int ok = state == kCases[i].expected;
    passed += ok;
    say(log, "  %s %s: %s", ok ? "ok  " : "FAIL", kCases[i].name, gamecheck_state_name(state));
    say(log, "       \"%s\"", report.message);
  }

  // Disc image probe: a fake image with the file-system marker, and a file without it.
  char image[1024];
  snprintf(image, sizeof image, "%s/test.iso", work_dir);
  unsigned char *data = calloc(1, 0x10000 + 2048);
  int image_ok = 0;
  if (data) {
    memcpy(data + 0x10000, "MICROSOFT*XBOX*MEDIA", 20);
    gamecheck_report report;
    if (write_file(image, data, 0x10000 + 2048) &&
        gamecheck_probe_image(image, NULL, &report) == GC_DISC_IMAGE_NOT_YET) {
      memset(data + 0x10000, 0, 20);
      image_ok = write_file(image, data, 0x10000 + 2048) &&
                 gamecheck_probe_image(image, NULL, &report) == GC_NOT_DISC_IMAGE;
    }
    free(data);
  }
  say(log, "  %s disc image detection", image_ok ? "ok  " : "FAIL");
  remove(image);
  remove_tree(root);
  remove(manifest);

  const int all = passed == total && image_ok;
  say(log, "%s: importer checks %d of %d correct, disc image detection %s", all ? "PASS" : "FAIL",
      passed, total, image_ok ? "correct" : "wrong");
  return all;
}
