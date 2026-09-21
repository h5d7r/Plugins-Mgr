#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "server.h"
#include "ini.h"
#include "fs.h"
#include "log.h"
#include "html.h"

/* SCE network stubs provided by ps4-payload-dev/sdk. */
int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetSocketClose(int fd);
int sceNetSetsockopt(int fd, int level, int optname, const void *optval,
                     socklen_t optlen);
int sceNetBind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int sceNetListen(int fd, int backlog);
int sceNetAccept(int fd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t sceNetSend(int fd, const void *buf, size_t len, int flags);
ssize_t sceNetRecv(int fd, void *buf, size_t len, int flags);
uint16_t sceNetHtons(uint16_t hostshort);

void sceKernelSleep(unsigned int seconds);

#define BACKLOG      5
#define HDR_MAX      16384
#define JSON_MAX      8192
#define UPLOAD_MAX   (8u * 1024u * 1024u)

static volatile int g_running = 1;

void server_stop(void) {
    g_running = 0;
}

/* ---------- tiny JSON helpers ---------- */

static void json_escape(const char *src, char *dst, size_t dstsz) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dstsz; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dstsz) {
                break;
            }
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= dstsz) {
                break;
            }
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if ((unsigned char)c < 0x20) {
            continue;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

/* Extract "key":"value" (string) from a flat JSON object. Returns 0 on success. */
static int json_get_str(const char *json, const char *key, char *out, size_t outsz) {
    char pat[96];
    const char *p, *q;
    size_t n;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '"') {
        p++;
        q = p;
        n = 0;
        while (*q && *q != '"' && n + 1 < outsz) {
            if (*q == '\\' && *(q + 1)) {
                q++;
                out[n++] = *q++;
            } else {
                out[n++] = *q++;
            }
        }
        out[n] = '\0';
        return (*q == '"') ? 0 : -1;
    }
    /* bare true/false/number: copy token */
    q = p;
    n = 0;
    while (*q && *q != ',' && *q != '}' && !isspace((unsigned char)*q) && n + 1 < outsz) {
        out[n++] = *q++;
    }
    out[n] = '\0';
    return (n > 0) ? 0 : -1;
}

/* ---------- HTTP helpers ---------- */

static void send_all(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t r = sceNetSend(fd, (const char *)buf + sent, len - sent, 0);
        if (r <= 0) {
            break;
        }
        sent += (size_t)r;
    }
}

static void send_response(int fd, int code, const char *status,
                          const char *ctype, const char *body, size_t len) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "\r\n",
                      code, status, ctype, len);
    if (hl > 0 && (size_t)hl < sizeof(hdr)) {
        send_all(fd, hdr, (size_t)hl);
    }
    if (len > 0) {
        send_all(fd, body, len);
    }
}

static void send_json(int fd, const char *json) {
    send_response(fd, 200, "OK", "application/json", json, strlen(json));
}

static void send_json_err(int fd, int code, const char *msg) {
    char b[256], e[180];
    json_escape(msg, e, sizeof(e));
    snprintf(b, sizeof(b), "{\"ok\":false,\"error\":\"%s\"}", e);
    send_response(fd, code, code == 404 ? "Not Found" : "Bad Request",
                  "application/json", b, strlen(b));
}

static void send_ok(int fd, const char *extra) {
    char b[512];
    if (extra) {
        snprintf(b, sizeof(b), "{\"ok\":true,%s}", extra);
    } else {
        snprintf(b, sizeof(b), "{\"ok\":true}");
    }
    send_json(fd, b);
}

/* ---------- /api/plugins ---------- */

/* Bounded append helpers: *pos never exceeds cap-1; return -1 when truncated. */
static int jprintf(char *out, size_t cap, size_t *pos, const char *fmt, ...) {
    va_list ap;
    int n;
    if (*pos + 1 >= cap) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    if ((size_t)n >= cap - *pos) {
        *pos = cap - 1;
        out[*pos] = '\0';
        return -1;
    }
    *pos += (size_t)n;
    return 0;
}

static void serve_plugins(int fd) {
    static char out[128 * 1024];
    static ini_doc_t doc;
    static plg_file_t files[PLG_MAX_FILES];
    size_t pos = 0;
    int nf, i, s;
    int ini_exists = 0;
    FILE *t = fopen(PLUGINS_INI, "r");

    if (t) {
        ini_exists = 1;
        fclose(t);
    }
    ini_load(&doc);
    nf = plg_list_files(files, PLG_MAX_FILES);

    if (jprintf(out, sizeof(out), &pos,
                "{\"ok\":true,\"ini_exists\":%s,\"sections\":[",
                ini_exists ? "true" : "false") != 0) {
        goto done;
    }
    for (s = 0; s < doc.nsects; s++) {
        char esc[INI_SECT_SZ * 2];
        int first = 1;
        json_escape(doc.sects[s], esc, sizeof(esc));
        if (jprintf(out, sizeof(out), &pos, "%s{\"id\":\"%s\",\"entries\":[",
                    s ? "," : "", esc) != 0) {
            goto done;
        }
        for (i = 0; i < doc.count; i++) {
            ini_line_t *l = &doc.lines[i];
            char ep[INI_PATH_SZ * 2], eb[192];
            char base[128];
            if (l->type != LINE_ENTRY || strcmp(l->section, doc.sects[s]) != 0) {
                continue;
            }
            ini_path_file_name(l->path, base, sizeof(base));
            json_escape(l->path, ep, sizeof(ep));
            json_escape(base, eb, sizeof(eb));
            if (jprintf(out, sizeof(out), &pos,
                        "%s{\"path\":\"%s\",\"file\":\"%s\",\"enabled\":%s}",
                        first ? "" : ",", ep, eb,
                        l->enabled ? "true" : "false") != 0) {
                goto done;
            }
            first = 0;
        }
        if (jprintf(out, sizeof(out), &pos, "]}") != 0) {
            goto done;
        }
    }
    if (jprintf(out, sizeof(out), &pos, "],\"files\":[") != 0) {
        goto done;
    }
    if (nf < 0) {
        nf = 0;
    }
    for (i = 0; i < nf; i++) {
        int ref = 0, k;
        char en[256];
        json_escape(files[i].name, en, sizeof(en));
        for (k = 0; k < doc.count; k++) {
            ini_line_t *l = &doc.lines[k];
            char b[128];
            if (l->type != LINE_ENTRY) {
                continue;
            }
            ini_path_file_name(l->path, b, sizeof(b));
            if (strcmp(b, files[i].name) == 0) {
                ref = 1;
                break;
            }
        }
        if (jprintf(out, sizeof(out), &pos,
                    "%s{\"name\":\"%s\",\"size\":%ld,\"mtime\":%ld,\"referenced\":%s}",
                    i ? "," : "", en, files[i].size, files[i].mtime,
                    ref ? "true" : "false") != 0) {
            goto done;
        }
    }
done:
    jprintf(out, sizeof(out), &pos, "]}");
    send_json(fd, out);
}

static void serve_log(int fd) {
    FILE *fp = fopen(LOG_PATH, "r");
    static char buf[64 * 1024];
    size_t len = 0;
    if (fp) {
        len = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        buf[len] = '\0';
        send_response(fd, 200, "OK", "text/plain; charset=UTF-8", buf, len);
    } else {
        const char *e = "No log entries.\n";
        send_response(fd, 200, "OK", "text/plain", e, strlen(e));
    }
}

/* ---------- request reading ---------- */

typedef struct {
    char method[16];
    char target[512];
    char path[384];
    char query[256];
    long content_length;
    char ctype[128];
} req_info_t;

static int recv_headers(int fd, char *hdr, size_t cap, size_t *hdr_len,
                        size_t *body_avail) {
    size_t total = 0;
    ssize_t r;
    while (total + 1 < cap) {
        r = sceNetRecv(fd, hdr + total, cap - 1 - total, 0);
        if (r <= 0) {
            break;
        }
        total += (size_t)r;
        hdr[total] = '\0';
        {
            char *end = strstr(hdr, "\r\n\r\n");
            if (end) {
                *hdr_len = (size_t)(end + 4 - hdr);
                *body_avail = total - *hdr_len;
                return 0;
            }
        }
        if (total + 1 >= cap) {
            break;
        }
    }
    return -1;
}

static void parse_req_line(const char *hdr, req_info_t *ri) {
    char target[512] = "/";
    char *q;
    memset(ri, 0, sizeof(*ri));
    sscanf(hdr, "%15s %511s", ri->method, target);
    strncpy(ri->target, target, sizeof(ri->target) - 1);
    q = strchr(target, '?');
    if (q) {
        size_t plen = (size_t)(q - target);
        if (plen >= sizeof(ri->path)) {
            plen = sizeof(ri->path) - 1;
        }
        memcpy(ri->path, target, plen);
        ri->path[plen] = '\0';
        strncpy(ri->query, q + 1, sizeof(ri->query) - 1);
    } else {
        strncpy(ri->path, target, sizeof(ri->path) - 1);
    }
    {
        const char *cl = strstr(hdr, "Content-Length:");
        if (!cl) {
            cl = strstr(hdr, "content-length:");
        }
        ri->content_length = cl ? atol(cl + 15) : 0;
        if (ri->content_length < 0) {
            ri->content_length = 0;
        }
    }
    {
        const char *ct = strstr(hdr, "Content-Type:");
        if (ct) {
            ct += 13;
            while (*ct == ' ') {
                ct++;
            }
            size_t i = 0;
            while (*ct && *ct != '\r' && *ct != '\n' && i + 1 < sizeof(ri->ctype)) {
                ri->ctype[i++] = *ct++;
            }
            ri->ctype[i] = '\0';
        }
    }
}

/* query=...&filename=... helper: get key value (no url-decoding except +/%%). */
static int query_get(const char *q, const char *key, char *out, size_t outsz) {
    size_t kl = strlen(key);
    const char *p = q;
    while (p && *p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t i = 0;
            while (*v && *v != '&' && i + 1 < outsz) {
                if (*v == '%' && isxdigit((unsigned char)v[1]) &&
                    isxdigit((unsigned char)v[2])) {
                    char hx[3] = { v[1], v[2], 0 };
                    out[i++] = (char)strtol(hx, NULL, 16);
                    v += 3;
                } else if (*v == '+') {
                    out[i++] = ' ';
                    v++;
                } else {
                    out[i++] = *v++;
                }
            }
            out[i] = '\0';
            return 0;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    return -1;
}

/* Read exactly `expected` body bytes into buf (body may be partly in hdr). */
static long read_body(int fd, const char *hdr, size_t hdr_len, size_t avail,
                      char *buf, size_t cap, long expected) {
    size_t total = avail;
    if (expected < 0) {
        expected = 0;
    }
    if ((size_t)expected >= cap) {
        return -1;
    }
    if (avail > (size_t)expected) {
        total = (size_t)expected;
    }
    memmove(buf, hdr + hdr_len, total);
    while (total < (size_t)expected) {
        ssize_t r = sceNetRecv(fd, buf + total,
                               (size_t)expected - total, 0);
        if (r <= 0) {
            break;
        }
        total += (size_t)r;
    }
    buf[total] = '\0';
    return (long)total;
}

static void handle_upload(int fd, const req_info_t *ri, const char *hdr,
                          size_t hdr_len, size_t avail) {
    char fname[160] = "";
    char full[320];
    FILE *fp;
    long remaining;
    size_t pre = avail;

    if (query_get(ri->query, "filename", fname, sizeof(fname)) != 0) {
        /* fallback: last path segment of /api/upload/<name>? */
        const char *slash = strrchr(ri->path, '/');
        if (slash && strcmp(ri->path, "/api/upload") != 0) {
            strncpy(fname, slash + 1, sizeof(fname) - 1);
        }
    }
    if (!plg_valid_file_name(fname)) {
        send_json_err(fd, 400, "invalid filename");
        /* drain to keep connection sane */
        return;
    }
    if (ri->content_length <= 0 || (unsigned long)ri->content_length > UPLOAD_MAX) {
        send_json_err(fd, 400, "bad Content-Length (max 8MB)");
        return;
    }
    snprintf(full, sizeof(full), "%s/%s", PLUGINS_DIR, fname);
    plg_ensure_dir(PLUGINS_DIR);
    fp = fopen(full, "wb");
    if (!fp) {
        send_json_err(fd, 400, "cannot write plugins dir");
        plg_log("Upload failed: cannot open %s", full);
        return;
    }
    /* write bytes already in header buffer */
    if (pre > 0) {
        size_t w = pre;
        if (w > (size_t)ri->content_length) {
            w = (size_t)ri->content_length;
        }
        if (fwrite(hdr + hdr_len, 1, w, fp) != w) {
            fclose(fp);
            unlink(full);
            send_json_err(fd, 400, "write failed");
            return;
        }
    }
    remaining = ri->content_length - (long)pre;
    while (remaining > 0) {
        char chunk[8192];
        size_t want = sizeof(chunk);
        ssize_t r;
        if ((long)want > remaining) {
            want = (size_t)remaining;
        }
        r = sceNetRecv(fd, chunk, want, 0);
        if (r <= 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)r, fp) != (size_t)r) {
            fclose(fp);
            unlink(full);
            send_json_err(fd, 400, "write failed");
            return;
        }
        remaining -= r;
    }
    fclose(fp);
    if (remaining != 0) {
        unlink(full);
        send_json_err(fd, 400, "truncated upload");
        return;
    }
    plg_log("Uploaded %s (%ld bytes)", fname, ri->content_length);
    {
        char extra[256], esc[160];
        json_escape(fname, esc, sizeof(esc));
        snprintf(extra, sizeof(extra), "\"name\":\"%s\",\"size\":%ld",
                 esc, ri->content_length);
        send_ok(fd, extra);
    }
}

static void handle_client(int fd) {
    static char hdr[HDR_MAX];
    size_t hdr_len = 0, avail = 0;
    req_info_t ri;

    memset(hdr, 0, sizeof(hdr));
    if (recv_headers(fd, hdr, sizeof(hdr), &hdr_len, &avail) != 0) {
        return;
    }
    parse_req_line(hdr, &ri);
    plg_log("Request: %s %s", ri.method, ri.target);

    if (strcmp(ri.method, "GET") == 0 && strcmp(ri.path, "/") == 0) {
        send_response(fd, 200, "OK", "text/html; charset=UTF-8",
                      HTML_PAGE, strlen(HTML_PAGE));
        return;
    }
    if (strcmp(ri.method, "GET") == 0 && strcmp(ri.path, "/api/plugins") == 0) {
        serve_plugins(fd);
        return;
    }
    if (strcmp(ri.method, "GET") == 0 && strcmp(ri.path, "/api/log") == 0) {
        serve_log(fd);
        return;
    }
    if (strcmp(ri.method, "POST") == 0 && strcmp(ri.path, "/api/stop") == 0) {
        send_ok(fd, "\"stopped\":true");
        plg_log("Stop requested via /api/stop");
        g_running = 0;
        return;
    }
    if ((strcmp(ri.path, "/api/upload") == 0 ||
         strncmp(ri.path, "/api/upload/", 12) == 0) &&
        (strcmp(ri.method, "POST") == 0 || strcmp(ri.method, "PUT") == 0)) {
        handle_upload(fd, &ri, hdr, hdr_len, avail);
        return;
    }
    if (strcmp(ri.method, "POST") == 0 &&
        (strcmp(ri.path, "/api/toggle") == 0 ||
         strcmp(ri.path, "/api/entry") == 0 ||
         strcmp(ri.path, "/api/file") == 0)) {
        static char jb[JSON_MAX];
        static ini_doc_t doc;
        long got;

        if (ri.content_length <= 0 || ri.content_length >= (long)sizeof(jb) - 1) {
            send_json_err(fd, 400, "missing JSON body");
            return;
        }
        got = read_body(fd, hdr, hdr_len, avail, jb, sizeof(jb),
                        ri.content_length);
        if (got < 0 || got < ri.content_length) {
            send_json_err(fd, 400, "truncated body");
            return;
        }

        if (strcmp(ri.path, "/api/toggle") == 0) {
            char section[INI_SECT_SZ], path[INI_PATH_SZ];
            if (json_get_str(jb, "section", section, sizeof(section)) != 0 ||
                json_get_str(jb, "path", path, sizeof(path)) != 0) {
                send_json_err(fd, 400, "need section+path");
                return;
            }
            ini_load(&doc);
            if (ini_toggle(&doc, section, path) != 0) {
                send_json_err(fd, 404, "entry not found");
                return;
            }
            if (ini_commit(&doc) != 0) {
                send_json_err(fd, 400, "write failed");
                return;
            }
            plg_log("Toggled %s :: %s", section, path);
            send_ok(fd, NULL);
            return;
        }
        if (strcmp(ri.path, "/api/entry") == 0) {
            char section[INI_SECT_SZ], path[INI_PATH_SZ], op[16];
            if (json_get_str(jb, "section", section, sizeof(section)) != 0 ||
                json_get_str(jb, "path", path, sizeof(path)) != 0 ||
                json_get_str(jb, "op", op, sizeof(op)) != 0) {
                send_json_err(fd, 400, "need section+path+op");
                return;
            }
            ini_load(&doc);
            if (strcmp(op, "add") == 0) {
                int r = ini_add(&doc, section, path);
                if (r < 0) {
                    send_json_err(fd, 400, "invalid section/path");
                    return;
                }
                if (r == 0 && ini_commit(&doc) != 0) {
                    send_json_err(fd, 400, "write failed");
                    return;
                }
                plg_log("Entry add %s :: %s", section, path);
                send_ok(fd, r == 1 ? "\"exists\":true" : NULL);
            } else if (strcmp(op, "remove") == 0) {
                if (ini_remove(&doc, section, path) != 0) {
                    send_json_err(fd, 404, "entry not found");
                    return;
                }
                if (ini_commit(&doc) != 0) {
                    send_json_err(fd, 400, "write failed");
                    return;
                }
                plg_log("Entry remove %s :: %s", section, path);
                send_ok(fd, NULL);
            } else {
                send_json_err(fd, 400, "op must be add|remove");
            }
            return;
        }
        /* /api/file */
        {
            char name[160], op[16], dest[160] = "", clean[16] = "";
            int clean_ini = 0;
            if (json_get_str(jb, "name", name, sizeof(name)) != 0 ||
                json_get_str(jb, "op", op, sizeof(op)) != 0) {
                send_json_err(fd, 400, "need name+op");
                return;
            }
            json_get_str(jb, "dest", dest, sizeof(dest));
            if (json_get_str(jb, "clean_ini", clean, sizeof(clean)) == 0) {
                clean_ini = (strcmp(clean, "true") == 0 || strcmp(clean, "1") == 0);
            }
            if (!plg_valid_file_name(name)) {
                send_json_err(fd, 400, "invalid name");
                return;
            }
            if (strcmp(op, "delete") == 0) {
                if (plg_delete_file(name) != 0) {
                    send_json_err(fd, 404, "delete failed");
                    return;
                }
                if (clean_ini) {
                    ini_load(&doc);
                    ini_remove_by_basename(&doc, name);
                    ini_commit(&doc);
                }
                plg_log("Deleted %s", name);
                send_ok(fd, NULL);
            } else if (strcmp(op, "copy") == 0 || strcmp(op, "move") == 0) {
                int rc;
                if (!plg_valid_file_name(dest)) {
                    send_json_err(fd, 400, "invalid dest");
                    return;
                }
                rc = (strcmp(op, "copy") == 0)
                         ? plg_copy_file(name, dest)
                         : plg_move_file(name, dest);
                if (rc != 0) {
                    send_json_err(fd, 400, "copy/move failed");
                    return;
                }
                plg_log("%s %s -> %s", op, name, dest);
                send_ok(fd, NULL);
            } else {
                send_json_err(fd, 400, "op must be delete|copy|move");
            }
            return;
        }
    }

    {
        const char *m = "Not Found";
        send_response(fd, 404, m, "text/plain", m, strlen(m));
    }
}

int server_run(void) {
    struct sockaddr_in saddr;
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int reuse = 1;
    int srv;

    srv = sceNetSocket("plugins-mgr", AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        plg_log("Error: socket init failed");
        return -1;
    }
    sceNetSetsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = sceNetHtons(SERVER_PORT);

    if (sceNetBind(srv, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        plg_log("Error: bind failed on %d", SERVER_PORT);
        sceNetSocketClose(srv);
        return -1;
    }
    if (sceNetListen(srv, BACKLOG) < 0) {
        plg_log("Error: listen failed");
        sceNetSocketClose(srv);
        return -1;
    }
    plg_log("Server live on port %d", SERVER_PORT);

    while (g_running) {
        int cli = sceNetAccept(srv, (struct sockaddr *)&caddr, &clen);
        if (cli < 0) {
            sceKernelSleep(1);
            clen = sizeof(caddr);
            continue;
        }
        handle_client(cli);
        sceNetSocketClose(cli);
        clen = sizeof(caddr);
        if (!g_running) {
            break;
        }
        sceKernelSleep(1);
    }
    sceNetSocketClose(srv);
    plg_log("Server stopped on port %d", SERVER_PORT);
    return 0;
}
