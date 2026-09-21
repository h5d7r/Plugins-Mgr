#ifndef PLGMGR_FS_H
#define PLGMGR_FS_H

#include <stddef.h>
#include <sys/types.h>

#define GOLDHEN_DIR   "/data/GoldHEN"
#define PLUGINS_DIR   "/data/GoldHEN/plugins"
#define PLUGINS_INI   "/data/GoldHEN/plugins.ini"
#define PLUGINS_BAK   "/data/GoldHEN/plugins.ini.bak"
#define MGR_DIR       "/data/plugins-mgr"
#define LOG_PATH      MGR_DIR "/log.txt"

/* A file on disk. */
typedef struct {
    char name[128];
    long size;
    long mtime;
} plg_file_t;

#define PLG_MAX_FILES 256

/* Ensure a directory path exists (mkdir -p style). Returns 0 on success. */
int plg_ensure_dir(const char *path);

/* List files in PLUGINS_DIR. Returns count or -1. */
int plg_list_files(plg_file_t *out, int max);

/* Atomic write: data -> path via path.tmp + rename. Returns 0 on success. */
int plg_write_atomic(const char *path, const char *data, size_t len);

/* Copy a file within PLUGINS_DIR. Basenames only. Returns 0 on success. */
int plg_copy_file(const char *src_name, const char *dst_name);

/* Move/rename a file within PLUGINS_DIR. Returns 0 on success. */
int plg_move_file(const char *src_name, const char *dst_name);

/* Delete a file within PLUGINS_DIR. Returns 0 on success. */
int plg_delete_file(const char *name);

/* Validate a file basename: no directories/traversal, sane chars, any type. */
int plg_valid_file_name(const char *name);

/* Extract basename from an ini path (after last '/'). */
void plg_basename(const char *path, char *out, size_t outsz);

#endif
