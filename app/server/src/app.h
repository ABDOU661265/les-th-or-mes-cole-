/*
 * Les Théorèmes — application scolaire
 * serveur local (HTTP + SQLite) : definitions communes
 */
#ifndef APP_H
#define APP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
   typedef SOCKET sock_t;
#  define CLOSESOCK closesocket
#  define SOCKERRNO WSAGetLastError()
#  define strncasecmp _strnicmp
#  define strcasecmp  _stricmp
#else
#  include <unistd.h>
#  include <errno.h>
#  include <strings.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
   typedef int sock_t;
#  define CLOSESOCK close
#  define SOCKERRNO errno
#endif

#include "sqlite3.h"
#include "cJSON.h"

#define APP_VERSION "1.0.0"
#define APP_NAME    "Les Theoremes"

/* ---------- util.c ---------- */
typedef struct { char *data; size_t len, cap; } sbuf;

void  sb_init(sbuf *b);
void  sb_free(sbuf *b);
void  sb_add(sbuf *b, const char *s);
void  sb_addn(sbuf *b, const char *s, size_t n);
void  sb_printf(sbuf *b, const char *fmt, ...);
char *sb_take(sbuf *b);

void  b64_encode(const unsigned char *in, size_t n, char *out);
int   b64_decode(const char *in, unsigned char *out, size_t outmax);
void  random_bytes(unsigned char *buf, size_t n);
char *json_escape(const char *s);           /* retourne une chaine allouee (sans guillemets) */
char *url_decode(const char *s);
void  log_write(const char *fmt, ...);
void  safe_copy(char *dst, size_t cap, const char *src);
long  now_ts(void);
void  date_str(char *out, size_t cap, time_t t);
int   const_eq(const char *a, const char *b);

/* ---------- db.c ---------- */
extern sqlite3 *g_db;
extern char     g_datadir[1024];
int   db_open(const char *dir);
void  db_close(void);
int   db_exec(const char *sql);
/* requete -> tableau JSON (a liberer avec cJSON_Delete) ; params = tableau de chaines */
cJSON *db_query(const char *sql, int nparams, const char **params);
int   db_count(const char *sql, int nparams, const char **params);
/* ecriture parametree simple : retourne id insere ou -1 */
sqlite3_int64 db_exec_params(const char *sql, int nparams, const char **params);
void  db_seed(void);

/* ---------- auth.c ---------- */
#define MAX_SESSIONS 128
#define SESSION_TTL  43200   /* 12 h */

typedef struct {
    char token[65];
    sqlite3_int64 uid;
    char role[16];
    time_t last;
    int used;
} session_t;

extern session_t g_sessions[MAX_SESSIONS];

void  password_hash(const char *pw, char *out, size_t outcap);
int   password_verify(const char *pw, const char *stored);
char *session_create(sqlite3_int64 uid, const char *role);
int   session_index(const char *token);
void  session_destroy(const char *token);
void  session_prune(void);
const char *login_throttle_check(const char *email);
void  login_throttle_fail(const char *email);
void  login_throttle_ok(const char *email);
void  audit(sqlite3_int64 uid, const char *action, const char *detail);

/* ---------- http.c ---------- */
typedef struct {
    sock_t sock;
    char   method[8];
    char   path[512];
    char   query[512];
    char  *body;
    size_t body_len;
    char   bearer[80];
    char   client[64];
} request_t;

void http_send(request_t *rq, int status, const char *mime, const char *data, size_t len);
void http_json(request_t *rq, int status, cJSON *obj);
void http_error(request_t *rq, int status, const char *code);
int  http_serve_static(request_t *rq);
void http_handle(sock_t s);
void http_serve_file(request_t *rq, const char *path);

/* ---------- api.c ---------- */
void api_dispatch(request_t *rq);

/* ---------- perf ---------- */
extern int g_port;
int  app_open_browser(const char *url);

/* assets generes (tools/gen_assets.py) */
typedef struct { const char *path; const char *mime; const unsigned char *data; unsigned int size; } asset_t;
extern const asset_t g_assets[];
extern const int g_assets_count;

#endif
