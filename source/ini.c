#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ini.h"
#include "fs.h"

static void trim(char *s) {
    size_t n;
    char *e;
    while (*s && isspace((unsigned char)*s)) {
        memmove(s, s + 1, strlen(s));
    }
    n = strlen(s);
    e = s + n;
    while (e > s && isspace((unsigned char)*(e - 1))) {
        *(--e) = '\0';
    }
}

static int strip_crlf(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return 0;
}

static int path_bool_suffix(const char *path, int *enabled) {
    size_t n = strlen(path);
    if (n >= 5 && strcmp(path + n - 5, "=true") == 0) {
        *enabled = 1;
        return 1;
    }
    if (n >= 6 && strcmp(path + n - 6, "=false") == 0) {
        *enabled = 0;
        return 1;
    }
    return 0;
}

static void strip_bool_suffix(char *path) {
    size_t n = strlen(path);
    if (n >= 6 && strcmp(path + n - 6, "=false") == 0) {
        path[n - 6] = '\0';
    } else if (n >= 5 && strcmp(path + n - 5, "=true") == 0) {
        path[n - 5] = '\0';
    }
}

int ini_path_has_bool_suffix(const char *path) {
    int enabled;
    return path_bool_suffix(path, &enabled);
}

int ini_path_enabled(const char *path) {
    int enabled;
    if (path_bool_suffix(path, &enabled)) {
        return enabled;
    }
    return 1;
}

void ini_path_set_enabled(char *path, size_t pathsz, int enabled) {
    size_t n = strlen(path);
    size_t keep;
    if (n >= 6 && strcmp(path + n - 6, "=false") == 0) {
        keep = n - 6;
    } else if (n >= 5 && strcmp(path + n - 5, "=true") == 0) {
        keep = n - 5;
    } else {
        keep = n;
    }
    if (keep >= pathsz) {
        keep = pathsz - 1;
    }
    path[keep] = '\0';
    if (enabled) {
        if (keep + 5 < pathsz) {
            strcpy(path + keep, "=true");
        }
    } else if (keep + 6 < pathsz) {
        strcpy(path + keep, "=false");
    }
}

void ini_path_file_name(const char *path, char *out, size_t outsz) {
    char p[INI_PATH_SZ];
    const char *slash;
    size_t n;
    if (!out || outsz == 0) {
        return;
    }
    if (!path) {
        out[0] = '\0';
        return;
    }
    n = strlen(path);
    if (n >= sizeof(p)) {
        n = sizeof(p) - 1;
    }
    memcpy(p, path, n);
    p[n] = '\0';
    if (ini_path_has_bool_suffix(p)) {
        strip_bool_suffix(p);
    }
    slash = strrchr(p, '/');
    n = slash ? (size_t)(slash + 1 - p) : 0;
    if (n >= outsz) {
        n = outsz - 1;
    }
    if (n) {
        memcpy(out, slash ? slash + 1 : p, n);
    }
    out[n] = '\0';
}

int ini_valid_section(const char *s) {
    size_t n, i;
    if (!s || (n = strlen(s)) == 0 || n >= INI_SECT_SZ) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

int ini_valid_path(const char *p) {
    size_t n;
    if (!p || (n = strlen(p)) == 0 || n >= INI_PATH_SZ) {
        return 0;
    }
    if (strncmp(p, "/data/", 6) != 0) {
        return 0;
    }
    if (strstr(p, "..")) {
        return 0;
    }
    return 1;
}

static int track_section(ini_doc_t *doc, const char *s) {
    int i;
    for (i = 0; i < doc->nsects; i++) {
        if (strcmp(doc->sects[i], s) == 0) {
            return i;
        }
    }
    if (doc->nsects >= INI_MAX_SECTS) {
        return -1;
    }
    strncpy(doc->sects[doc->nsects], s, INI_SECT_SZ - 1);
    doc->sects[doc->nsects][INI_SECT_SZ - 1] = '\0';
    return doc->nsects++;
}

int ini_load(ini_doc_t *doc) {
    FILE *fp;
    char line[INI_LINE_SZ];
    char cur[INI_SECT_SZ] = "default";

    memset(doc, 0, sizeof(*doc));
    track_section(doc, "default");

    fp = fopen(PLUGINS_INI, "r");
    if (!fp) {
        return 0; /* missing -> start empty */
    }
    while (fgets(line, sizeof(line), fp)) {
        ini_line_t *l;
        char work[INI_LINE_SZ];
        strip_crlf(line);
        if (doc->count >= INI_MAX_LINES) {
            break;
        }
        l = &doc->lines[doc->count];
        memset(l, 0, sizeof(*l));
        strncpy(l->raw, line, sizeof(l->raw) - 1);
        strncpy(work, line, sizeof(work) - 1);
        work[sizeof(work) - 1] = '\0';
        trim(work);

        if (work[0] == '\0') {
            l->type = LINE_BLANK;
        } else if (work[0] == '[') {
            size_t wl = strlen(work);
            if (wl >= 3 && work[wl - 1] == ']') {
                work[wl - 1] = '\0';
                if (ini_valid_section(work + 1)) {
                    l->type = LINE_SECTION;
                    strncpy(cur, work + 1, sizeof(cur) - 1);
                    strncpy(l->section, cur, sizeof(l->section) - 1);
                    track_section(doc, cur);
                } else {
                    l->type = LINE_COMMENT;
                }
            } else {
                l->type = LINE_COMMENT;
            }
        } else {
            /* Possibly disabled entry: leading ';' followed by a path. */
            char *cand = work;
            int enabled = 1;
            int has_suffix;
            if (cand[0] == ';') {
                cand++;
                while (*cand && isspace((unsigned char)*cand)) {
                    cand++;
                }
                /* Only treat as entry if remainder looks like a plugin path. */
                if (ini_valid_path(cand) && doc->count < INI_MAX_LINES) {
                    enabled = 0;
                } else {
                    l->type = LINE_COMMENT;
                    strncpy(l->section, cur, sizeof(l->section) - 1);
                    doc->count++;
                    continue;
                }
            }
            if (l->type != LINE_COMMENT) {
                if (ini_valid_path(cand)) {
                    l->type = LINE_ENTRY;
                    strncpy(l->section, cur, sizeof(l->section) - 1);
                    strncpy(l->path, cand, sizeof(l->path) - 1);
                    has_suffix = ini_path_has_bool_suffix(cand);
                    l->enabled = has_suffix ? ini_path_enabled(cand) : enabled;
                    track_section(doc, cur);
                } else {
                    l->type = LINE_COMMENT;
                    strncpy(l->section, cur, sizeof(l->section) - 1);
                }
            }
        }
        if (l->type == LINE_BLANK || l->type == LINE_COMMENT) {
            strncpy(l->section, cur, sizeof(l->section) - 1);
        }
        doc->count++;
    }
    fclose(fp);
    return 0;
}

int ini_save_text(const ini_doc_t *doc, char *buf, size_t bufsz) {
    size_t pos = 0;
    int i;
    for (i = 0; i < doc->count; i++) {
        const ini_line_t *l = &doc->lines[i];
        char tmp[INI_LINE_SZ + 8];
        if (l->type == LINE_SECTION) {
            snprintf(tmp, sizeof(tmp), "[%s]\n", l->section);
        } else if (l->type == LINE_ENTRY) {
            if (ini_path_has_bool_suffix(l->path)) {
                snprintf(tmp, sizeof(tmp), "%s\n", l->path);
            } else if (l->enabled) {
                snprintf(tmp, sizeof(tmp), "%s\n", l->path);
            } else {
                snprintf(tmp, sizeof(tmp), ";%s\n", l->path);
            }
        } else {
            snprintf(tmp, sizeof(tmp), "%s\n", l->raw);
        }
        if (pos + strlen(tmp) + 1 > bufsz) {
            return -1;
        }
        memcpy(buf + pos, tmp, strlen(tmp));
        pos += strlen(tmp);
    }
    if (pos < bufsz) {
        buf[pos] = '\0';
    }
    return (int)pos;
}

int ini_commit(const ini_doc_t *doc) {
    static char buf[128 * 1024];
    FILE *fp;
    int len;

    len = ini_save_text(doc, buf, sizeof(buf));
    if (len < 0) {
        return -1;
    }
    /* Backup current ini first (best effort). */
    fp = fopen(PLUGINS_INI, "r");
    if (fp) {
        FILE *bf = fopen(PLUGINS_BAK, "w");
        if (bf) {
            char chunk[4096];
            size_t r;
            while ((r = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
                if (fwrite(chunk, 1, r, bf) != r) {
                    break;
                }
            }
            fclose(bf);
        }
        fclose(fp);
    }
    return plg_write_atomic(PLUGINS_INI, buf, (size_t)len);
}

int ini_find(const ini_doc_t *doc, const char *section, const char *path) {
    int i;
    for (i = 0; i < doc->count; i++) {
        const ini_line_t *l = &doc->lines[i];
        if (l->type == LINE_ENTRY && strcmp(l->section, section) == 0 &&
            strcmp(l->path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int ini_toggle(ini_doc_t *doc, const char *section, const char *path) {
    int i = ini_find(doc, section, path);
    int enabled;
    if (i < 0) {
        return -1;
    }
    enabled = !doc->lines[i].enabled;
    if (ini_path_has_bool_suffix(path)) {
        ini_path_set_enabled(doc->lines[i].path, sizeof(doc->lines[i].path), enabled);
    } else {
        doc->lines[i].enabled = enabled;
    }
    return 0;
}

int ini_add(ini_doc_t *doc, const char *section, const char *path) {
    ini_line_t *l;
    char addpath[INI_PATH_SZ];
    int i, sect_line = -1;

    if (!ini_valid_section(section) || !ini_valid_path(path)) {
        return -1;
    }
    if (ini_path_has_bool_suffix(path)) {
        strncpy(addpath, path, sizeof(addpath));
        addpath[sizeof(addpath) - 1] = '\0';
    } else {
        size_t n = strlen(path);
        if (n + 6 >= sizeof(addpath)) {
            return -1;
        }
        memcpy(addpath, path, n);
        strcpy(addpath + n, "=true");
    }
    if (ini_find(doc, section, addpath) >= 0) {
        return 1;
    }
    /* Find last line belonging to section to append after it. */
    for (i = doc->count - 1; i >= 0; i--) {
        if (doc->lines[i].type == LINE_SECTION &&
            strcmp(doc->lines[i].section, section) == 0) {
            sect_line = i;
            break;
        }
        if ((doc->lines[i].type == LINE_ENTRY ||
             (doc->lines[i].type == LINE_COMMENT && doc->lines[i].raw[0] != '\0')) &&
            strcmp(doc->lines[i].section, section) == 0 && sect_line < 0) {
            /* keep scanning for the header */
        }
        if (doc->lines[i].type == LINE_ENTRY &&
            strcmp(doc->lines[i].section, section) == 0) {
            sect_line = i; /* fallback: append after last entry */
        }
    }
    if (track_section(doc, section) < 0) {
        return -1;
    }
    if (doc->count + 2 >= INI_MAX_LINES) {
        return -1;
    }
    if (sect_line < 0) {
        /* New section at end. */
        if (doc->count > 0) {
            l = &doc->lines[doc->count++];
            l->type = LINE_BLANK;
            l->raw[0] = '\0';
            strncpy(l->section, section, sizeof(l->section) - 1);
        }
        l = &doc->lines[doc->count++];
        l->type = LINE_SECTION;
        strncpy(l->section, section, sizeof(l->section) - 1);
        l = &doc->lines[doc->count++];
    } else {
        /* Insert after sect_line (shift tail). */
        memmove(&doc->lines[sect_line + 2], &doc->lines[sect_line + 1],
                (size_t)(doc->count - sect_line - 1) * sizeof(ini_line_t));
        doc->count++;
        l = &doc->lines[sect_line + 1];
    }
    memset(l, 0, sizeof(*l));
    l->type = LINE_ENTRY;
    strncpy(l->section, section, sizeof(l->section) - 1);
    strncpy(l->path, addpath, sizeof(l->path) - 1);
    l->enabled = ini_path_enabled(addpath);
    return 0;
}

int ini_remove(ini_doc_t *doc, const char *section, const char *path) {
    int i = ini_find(doc, section, path);
    if (i < 0) {
        return -1;
    }
    memmove(&doc->lines[i], &doc->lines[i + 1],
            (size_t)(doc->count - i - 1) * sizeof(ini_line_t));
    doc->count--;
    return 0;
}

int ini_remove_by_basename(ini_doc_t *doc, const char *basename) {
    int i, removed = 0;
    char b[INI_PATH_SZ];
    for (i = doc->count - 1; i >= 0; i--) {
        ini_line_t *l = &doc->lines[i];
        if (l->type != LINE_ENTRY) {
            continue;
        }
        ini_path_file_name(l->path, b, sizeof(b));
        if (strcmp(b, basename) == 0) {
            memmove(&doc->lines[i], &doc->lines[i + 1],
                    (size_t)(doc->count - i - 1) * sizeof(ini_line_t));
            doc->count--;
            removed++;
        }
    }
    return removed;
}
