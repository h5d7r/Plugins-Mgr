#ifndef PLGMGR_INI_H
#define PLGMGR_INI_H

#include <stddef.h>

#define INI_MAX_LINES   1024
#define INI_MAX_SECTS    64
#define INI_LINE_SZ     512
#define INI_SECT_SZ      64
#define INI_PATH_SZ     300

typedef enum {
    LINE_BLANK = 0,
    LINE_COMMENT,
    LINE_SECTION,
    LINE_ENTRY
} line_type_t;

typedef struct {
    line_type_t type;
    char section[INI_SECT_SZ];   /* owning section ("" = global) */
    char path[INI_PATH_SZ];      /* for LINE_ENTRY: plugin path */
    int enabled;                 /* for LINE_ENTRY: 1 = active, 0 = ';' disabled */
    char raw[INI_LINE_SZ];       /* original text (comments/blanks preserved) */
} ini_line_t;

typedef struct {
    ini_line_t lines[INI_MAX_LINES];
    int count;
    char sects[INI_MAX_SECTS][INI_SECT_SZ];
    int nsects;
} ini_doc_t;

/* Load plugins.ini (missing file -> empty doc with [default]). */
int ini_load(ini_doc_t *doc);

/* Serialize doc back to text in buf. Returns length or -1 on overflow. */
int ini_save_text(const ini_doc_t *doc, char *buf, size_t bufsz);

/* Persist doc: backup current ini to .bak, then atomic write. */
int ini_commit(const ini_doc_t *doc);

/* Find entry index by section+path. Returns -1 if absent. */
int ini_find(const ini_doc_t *doc, const char *section, const char *path);

/* Toggle enabled flag of an entry. Returns 0 ok, -1 not found. */
int ini_toggle(ini_doc_t *doc, const char *section, const char *path);

/* Add a path under section (creates section if needed). 0 added, 1 exists, -1 err. */
int ini_add(ini_doc_t *doc, const char *section, const char *path);

/* Remove a path line from a section. Returns 0 removed, -1 not found. */
int ini_remove(ini_doc_t *doc, const char *section, const char *path);

/* Remove every entry whose basename matches name (used on file delete). */
int ini_remove_by_basename(ini_doc_t *doc, const char *basename);

/* Validate section id: "default" or CUSAxxxxx (lenient: [A-Za-z0-9_-]+). */
int ini_valid_section(const char *s);

/* Return 1 when path ends in =true/=false, 0 otherwise. */
int ini_path_has_bool_suffix(const char *path);

/* Return the suffix state: true=1, false=0, missing suffix=1. */
int ini_path_enabled(const char *path);

/* Replace/append the bool suffix while preserving the plugin path. */
void ini_path_set_enabled(char *path, size_t pathsz, int enabled);

/* Extract the file basename, ignoring a trailing =true/=false suffix. */
void ini_path_file_name(const char *path, char *out, size_t outsz);

/* Validate plugin path: must start with /data/ and be reasonable. */
int ini_valid_path(const char *p);

#endif
