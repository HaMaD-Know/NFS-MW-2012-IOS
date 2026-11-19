// Game data checker for "Import Your Game" (Milestone G1, BYOG_DESIGN.md §6).
//
// Checks a folder the user picked, in three phases:
//   1. Structure: the folder holds default.xex.
//   2. Identity: the title ID in default.xex's plain XEX2 header (nothing is decrypted), and
//      the SHA-256 of default.xex against the edition's manifest.
//   3. Completeness: every file in the manifest is present, with the right size and, in a
//      full check, the right SHA-256.
// It only reads files. It never changes, copies or uploads them.
//
// The manifest is plain text (no game content):
//   # nfsmw-manifest 1
//   edition <id> <description>
//   title <title ID in hex>
//   xex-sha256 <hex>
//   file <sha256 hex> <size in bytes> <relative path, may contain spaces>
#ifndef NFSMW_GAMECHECK_H
#define NFSMW_GAMECHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gamecheck_log_fn)(const char *line);

/** Need for Speed: Most Wanted (2012), Xbox 360. */
#define GAMECHECK_TITLE_ID 0x45410961u

enum gamecheck_state {
  GC_VERIFIED = 0,             /**< Matches the manifest: right edition, nothing missing or damaged. */
  GC_IDENTIFIED_NO_MANIFEST,   /**< Right game, but no manifest exists to check its completeness. */
  GC_NOT_GAME_FOLDER,          /**< No default.xex in the folder. */
  GC_UNREADABLE_EXECUTABLE,    /**< default.xex is not a readable XEX2 file. */
  GC_DIFFERENT_GAME,           /**< Another title ID. */
  GC_UNSUPPORTED_EDITION,      /**< Right game, but default.xex is not the manifest's executable. */
  GC_INCOMPLETE,               /**< Files missing or not matching the manifest. */
  GC_BAD_MANIFEST,             /**< The manifest could not be read. */
  GC_DISC_IMAGE_NOT_YET,       /**< An Xbox 360 disc image (gamecheck_probe_image only). */
  GC_NOT_DISC_IMAGE,           /**< A file that is not an Xbox 360 disc image. */
};

typedef struct {
  int state;
  uint32_t title_id;
  uint32_t media_id;
  uint32_t version;     /**< Executable version, major.minor.build.qfe packed as in the XEX. */
  int has_identity;     /**< 1 when title_id, media_id and version were read. */
  char xex_sha256[65];  /**< Empty until computed. */
  char edition[96];     /**< Manifest edition description, when a manifest was used. */
  int files_listed;
  int files_missing;
  int files_damaged;
  char message[600];    /**< One or two sentences for the user. */
} gamecheck_report;

/** Short name of a state, for logs. */
const char *gamecheck_state_name(int state);

/**
 * Checks a game folder.
 * @param root      The folder (already accessible: the caller holds security-scoped access).
 * @param manifest  Manifest file path, or NULL when no manifest exists.
 * @param full      1 to hash every listed file (slow for real games), 0 for sizes only.
 * @param log       Receives one line per finding. File names are relative to root.
 * @return The state, also stored in out->state.
 */
int gamecheck_verify_folder(const char *root, const char *manifest, int full, gamecheck_log_fn log,
                            gamecheck_report *out);

/** Reports whether a file is an Xbox 360 disc image (XDVDFS). Reads two small blocks. */
int gamecheck_probe_image(const char *path, gamecheck_log_fn log, gamecheck_report *out);

/**
 * Checks a plain Xbox 360 disc image (.iso) in place, without extracting it: finds the game
 * partition, reads the XDVDFS directory tree, then runs the same three phases as for a folder on
 * the files inside the image. Only reads the image.
 */
int gamecheck_verify_image(const char *path, const char *manifest, int full, gamecheck_log_fn log,
                           gamecheck_report *out);

/**
 * Writes a small synthetic Xbox 360 disc image (an XDVDFS file system holding the same fake
 * files as gamecheck_make_test_folder, no game content) and the matching manifest. variant:
 * 0 complete, 1 one file missing, 2 one file damaged. Returns 1 on success.
 */
int gamecheck_make_test_image(const char *path, const char *manifest_path, int variant,
                              gamecheck_log_fn log);

/**
 * Writes a small synthetic game folder (a fake default.xex header and filler files, no game
 * content) and a manifest describing its complete form, then applies a variant:
 * 0 complete, 1 one file missing, 2 one file damaged (same size), 3 a different game,
 * 4 no default.xex, 5 another edition (different executable), 6 complete (for use without a
 * manifest). Replaces anything at root. Returns 1 on success.
 */
int gamecheck_make_test_folder(const char *root, const char *manifest_path, int variant,
                               gamecheck_log_fn log);

/** Builds every variant under work_dir, checks each, and reports PASS or FAIL. Returns 1 on PASS. */
int gamecheck_selftest(const char *work_dir, gamecheck_log_fn log);

#ifdef __cplusplus
}
#endif

#endif
