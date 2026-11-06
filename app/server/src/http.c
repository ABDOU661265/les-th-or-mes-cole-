/* http.c — mini serveur HTTP/1.1 (thread par connexion), fichiers statiques + assets embarques */
#include "app.h"
#include <ctype.h>

#define MAX_BODY (4 * 1024 * 1024)

static void send_all(sock_t s, const char *d, size_t n)
{
    size_t off = 0;
    while (off < n) {
        int r = send(s, d + off, (int)(n - off), 0);
        if (r <= 0) return;
        off += (size_t)r;
    }
}

void http_send(request_t *rq, int status, const char *mime, const char *data, size_t len)
{
    char head[512];
    const char *reason = "OK";
    int n;
    switch (status) {
        case 200: reason = "OK"; break;
        case 201: reason = "Created"; break;
        case 400: reason = "Bad Request"; break;
        case 401: reason = "Unauthorized"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 413: reason = "Payload Too Large"; break;
        case 429: reason = "Too Many Requests"; break;
        case 500: reason = "Internal Server Error"; break;
        default:  reason = "OK"; break;
    }
    n = snprintf(head, sizeof(head),
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %lu\r\n"
                 "Cache-Control: no-store\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "X-Frame-Options: SAMEORIGIN\r\n"
                 "Referrer-Policy: no-referrer\r\n"
                 "Connection: close\r\n\r\n",
                 status, reason, mime, (unsigned long)len);
    send_all(rq->sock, head, (size_t)n);
    if (data && len) send_all(rq->sock, data, len);
}

void http_json(request_t *rq, int status, cJSON *obj)
{
    char *txt = cJSON_PrintUnformatted(obj);
    if (!txt) { http_send(rq, 500, "application/json", "{\"error\":\"internal\"}", 20); return; }
    http_send(rq, status, "application/json; charset=utf-8", txt, strlen(txt));
    free(txt);
}

void http_error(request_t *rq, int status, const char *code)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 0);
    cJSON_AddStringToObject(o, "error", code);
    http_json(rq, status, o);
    cJSON_Delete(o);
}

static const char *mime_of(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcmp(ext, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(ext, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(ext, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(ext, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(ext, ".svg"))  return "image/svg+xml";
    if (!strcmp(ext, ".png"))  return "image/png";
    if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcmp(ext, ".ico"))  return "image/x-icon";
    if (!strcmp(ext, ".woff2")) return "font/woff2";
    if (!strcmp(ext, ".webmanifest")) return "application/manifest+json";
    return "application/octet-stream";
}

/* empeche toute sortie du dossier web (../, chemins absolus, lecteurs) */
static int path_is_safe(const char *p)
{
    if (!p || !p[0]) return 0;
    if (p[0] == '/' || p[0] == '\\') return 0;
    if (strstr(p, "..")) return 0;
    if (strchr(p, ':')) return 0;
    return 1;
}

void http_serve_file(request_t *rq, const char *relpath)
{
    char full[1200];
    FILE *f;
    long size;
    char *buf;
    if (!path_is_safe(relpath)) { http_error(rq, 404, "not_found"); return; }
    snprintf(full, sizeof(full), "%s/web/%s", g_datadir, relpath);
    f = fopen(full, "rb");
    if (!f) { http_error(rq, 404, "not_found"); return; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 32 * 1024 * 1024) { fclose(f); http_error(rq, 404, "not_found"); return; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); http_error(rq, 500, "internal"); return; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); http_error(rq, 500, "internal"); return; }
    fclose(f);
    http_send(rq, 200, mime_of(relpath), buf, (size_t)size);
    free(buf);
}

int http_serve_static(request_t *rq)
{
    const char *p = rq->path;
    int i;
    if (!strcmp(p, "/")) p = "/index.html";
    if (p[0] == '/') p++;
    /* emoji : fichier autonome a la racine du dossier web */
    if (!strcmp(p, "favicon.ico") || !strcmp(p, "logo.png")) { p = p; }
    /* 1) fichier sur disque (developpement / mise a jour a chaud) */
    {
        char full[1200];
        FILE *f;
        if (path_is_safe(p)) {
            snprintf(full, sizeof(full), "%s/web/%s", g_datadir, p);
            f = fopen(full, "rb");
            if (f) { fclose(f); http_serve_file(rq, p); return 1; }
        }
    }
    /* 2) ressource embarquee dans l'executable */
    for (i = 0; i < g_assets_count; i++) {
        if (!strcmp(g_assets[i].path, p)) {
            http_send(rq, 200, g_assets[i].mime, (const char *)g_assets[i].data, g_assets[i].size);
            return 1;
        }
    }
    return 0;
}

/* ---------------- analyse de la requete ---------------- */
static size_t content_length(const char *headers)
{
    const char *p = headers;
    while (p && *p) {
        if (!strncasecmp(p, "Content-Length:", 15)) return (size_t)strtoul(p + 15, NULL, 10);
        p = strstr(p, "\r\n");
        if (p) p += 2;
    }
    return 0;
}

static const char *header_value(const char *headers, const char *name)
{
    static char value[256];
    const char *p = headers;
    size_t nl = strlen(name);

    while (p && *p && !(p[0] == '\r' && p[1] == '\n')) {
        const char *eol = strstr(p, "\r\n");
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        const char *colon = memchr(p, ':', linelen);
        if (colon) {
            size_t namelen = (size_t)(colon - p);
            if (namelen == nl) {
                size_t i;
                int same = 1;
                for (i = 0; i < nl; i++)
                    if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) { same = 0; break; }
                if (same) {
                    const char *v = colon + 1;
                    size_t vl;
                    while (v < p + linelen && *v == ' ') v++;
                    vl = (size_t)(p + linelen - v);
                    if (vl >= sizeof(value)) vl = sizeof(value) - 1;
                    memcpy(value, v, vl);
                    value[vl] = 0;
                    while (vl && (value[vl - 1] == ' ' || value[vl - 1] == '\t')) value[--vl] = 0;
                    return value;
                }
            }
        }
        p = eol ? eol + 2 : NULL;
    }
    return NULL;
}

/* lit une requete complete ; retourne 1 si ok */
static int parse_request(sock_t s, request_t *rq)
{
    sbuf raw;
    char chunk[8192];
    size_t header_end = 0, total = 0, body_len = 0;
    int header_done = 0;

    memset(rq, 0, sizeof(*rq));
    rq->sock = s;
    sb_init(&raw);

    while (total < MAX_BODY) {
        int r = recv(s, chunk, (int)sizeof(chunk), 0);
        if (r <= 0) break;
        sb_addn(&raw, chunk, (size_t)r);
        total += (size_t)r;
        {
            char *he = strstr(raw.data, "\r\n\r\n");
            if (he) { header_end = (size_t)(he - raw.data) + 4; header_done = 1; break; }
        }
        if (total > 65536) break;
    }
    if (!header_done || !raw.data) { sb_free(&raw); return 0; }

    body_len = content_length(raw.data);
    if (body_len > MAX_BODY) { sb_free(&raw); return 0; }
    while (raw.len < header_end + body_len) {
        int r = recv(s, chunk, (int)sizeof(chunk), 0);
        if (r <= 0) break;
        sb_addn(&raw, chunk, (size_t)r);
    }

    {
        /* copie de la ligne de requete (sans toucher au buffer brut) */
        char reqline[700];
        size_t i = 0;
        while (i < raw.len && i < sizeof(reqline) - 1 && raw.data[i] != '\r' && raw.data[i] != '\n') {
            reqline[i] = raw.data[i];
            i++;
        }
        reqline[i] = 0;
        if (i == 0) { sb_free(&raw); return 0; }
        {
            char method[8] = "", target[600] = "", ver[16] = "";
            if (sscanf(reqline, "%7s %599s %15s", method, target, ver) < 2) { sb_free(&raw); return 0; }
            safe_copy(rq->method, sizeof(rq->method), method);
            {
                char *q = strchr(target, '?');
                if (q) {
                    *q = 0;
                    safe_copy(rq->query, sizeof(rq->query), q + 1);
                }
                safe_copy(rq->path, sizeof(rq->path), target);
            }
        }
    }
    /* entetes */
    {
        const char *auth = header_value(raw.data, "Authorization");
        if (auth && !strncasecmp(auth, "Bearer ", 7)) safe_copy(rq->bearer, sizeof(rq->bearer), auth + 7);
        else {
            const char *tok = header_value(raw.data, "X-Token");
            if (tok) safe_copy(rq->bearer, sizeof(rq->bearer), tok);
        }
        {
            const char *cl = header_value(raw.data, "X-Client");
            safe_copy(rq->client, sizeof(rq->client), cl ? cl : "web");
        }
    }
    if (body_len && header_end + body_len <= raw.len) {
        rq->body = (char *)malloc(body_len + 1);
        memcpy(rq->body, raw.data + header_end, body_len);
        rq->body[body_len] = 0;
        rq->body_len = body_len;
    }
    sb_free(&raw);
    return 1;
}

void http_handle(sock_t s)
{
    request_t rq;
    if (!parse_request(s, &rq)) { CLOSESOCK(s); return; }

    if (getenv("APP_DEBUG")) log_write("[req] %s %s bearer=[%s] auth_hdr=[%s]", rq.method, rq.path, rq.bearer, "(hidden)");
    if (!strcmp(rq.path, "/api") || !strncasecmp(rq.path, "/api/", 5)) {
        api_dispatch(&rq);
    } else if (!strcmp(rq.method, "GET")) {
        if (!http_serve_static(&rq)) http_error(&rq, 404, "not_found");
    } else {
        http_error(&rq, 404, "not_found");
    }

    if (rq.body) free(rq.body);
    /* laisse le client lire la reponse avant fermeture */
#ifdef _WIN32
    shutdown(s, SD_SEND);
#else
    shutdown(s, SHUT_WR);
#endif
    CLOSESOCK(s);
}
