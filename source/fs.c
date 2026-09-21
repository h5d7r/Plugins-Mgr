#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fs.h"

int plg_ensure_dir(const char *path) {
    char tmp[256];
    size_t len;

    if (!path || strlen(path) >= sizeof(tmp)) {
        return -1;
    }
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/* Any file type is allowed; only traversal and odd chars are rejected. */
int plg_valid_file_name(const char *name) {
    size_t n;
    if (!name || (n = strlen(name)) == 0 || n >= 128) {
        return 0;
    }
    if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+')) {
            return 0;
        }
    }
    return 1;
}

void plg_basename(const char *path, char *out, size_t outsz) {
    const char *b;
    if (!path || !out || outsz == 0) {
        return;
    }
    b = strrchr(path, '/');
    b = b ? b + 1 : path;
    strncpy(out, b, outsz - 1);
    out[outsz - 1] = '\0';
}

int plg_list_files(plg_file_t *out, int max) {
    DIR *d;
    struct dirent *e;
    int n = 0;

    d = opendir(PLUGINS_DIR);
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        struct stat st;
        char full[320];
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(full, sizeof(full), "%s/%s", PLUGINS_DIR, e->d_name);
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        strncpy(out[n].name, e->d_name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].size = (long)st.st_size;
        out[n].mtime = (long)st.st_mtime;
        n++;
    }
    closedir(d);
    return n;
}

int plg_write_atomic(const char *path, const char *data, size_t len) {
    char tmp[300];
    FILE *fp;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "w");
    if (!fp) {
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void join_plugin_path(const char *name, char *out, size_t outsz) {
    snprintf(out, outsz, "%s/%s", PLUGINS_DIR, name);
}

int plg_copy_file(const char *src_name, const char *dst_name) {
    char sp[320], dp[320];
    FILE *sf, *df;
    char buf[8192];
    size_t r;

    if (!plg_valid_file_name(src_name) || !plg_valid_file_name(dst_name)) {
        return -1;
    }
    join_plugin_path(src_name, sp, sizeof(sp));
    join_plugin_path(dst_name, dp, sizeof(dp));
    if (strcmp(sp, dp) == 0) {
        return -1;
    }
    sf = fopen(sp, "rb");
    if (!sf) {
        return -1;
    }
    df = fopen(dp, "wb");
    if (!df) {
        fclose(sf);
        return -1;
    }
    while ((r = fread(buf, 1, sizeof(buf), sf)) > 0) {
        if (fwrite(buf, 1, r, df) != r) {
            fclose(sf);
            fclose(df);
            unlink(dp);
            return -1;
        }
    }
    fclose(sf);
    if (fclose(df) != 0) {
        unlink(dp);
        return -1;
    }
    return 0;
}

int plg_move_file(const char *src_name, const char *dst_name) {
    char sp[320], dp[320];
    if (!plg_valid_file_name(src_name) || !plg_valid_file_name(dst_name)) {
        return -1;
    }
    join_plugin_path(src_name, sp, sizeof(sp));
    join_plugin_path(dst_name, dp, sizeof(dp));
    if (strcmp(sp, dp) == 0) {
        return -1;
    }
    if (rename(sp, dp) == 0) {
        return 0;
    }
    /* Cross-device fallback: copy + delete. */
    if (plg_copy_file(src_name, dst_name) == 0) {
        unlink(sp);
        return 0;
    }
    return -1;
}

int plg_delete_file(const char *name) {
    char p[320];
    if (!plg_valid_file_name(name)) {
        return -1;
    }
    join_plugin_path(name, p, sizeof(p));
    return unlink(p);
}
