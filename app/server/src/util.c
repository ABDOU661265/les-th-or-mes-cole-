/* util.c — petites briques utilitaires (chaine dynamique, base64, aleatoire, log) */
#include "app.h"
#include <stdarg.h>

void sb_init(sbuf *b) { b->cap = 256; b->len = 0; b->data = (char *)malloc(b->cap); if (b->data) b->data[0] = 0; }
void sb_free(sbuf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static void sb_reserve(sbuf *b, size_t need)
{
    if (b->len + need + 1 <= b->cap) return;
    while (b->len + need + 1 > b->cap) b->cap *= 2;
    b->data = (char *)realloc(b->data, b->cap);
}

void sb_addn(sbuf *b, const char *s, size_t n)
{
    sb_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

void sb_add(sbuf *b, const char *s) { if (s) sb_addn(b, s, strlen(s)); }

void sb_printf(sbuf *b, const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_add(b, tmp);
}

char *sb_take(sbuf *b) { char *d = b->data; b->data = NULL; b->len = b->cap = 0; return d; }

/* ---------------- base64 ---------------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void b64_encode(const unsigned char *in, size_t n, char *out)
{
    size_t i;
    for (i = 0; i + 2 < n; i += 3) {
        *out++ = B64[in[i] >> 2];
        *out++ = B64[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
        *out++ = B64[((in[i + 1] & 15) << 2) | (in[i + 2] >> 6)];
        *out++ = B64[in[i + 2] & 63];
    }
    if (i < n) {
        *out++ = B64[in[i] >> 2];
        if (i + 1 < n) {
            *out++ = B64[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
            *out++ = B64[(in[i + 1] & 15) << 2];
        } else {
            *out++ = B64[(in[i] & 3) << 4];
            *out++ = '=';
        }
        *out++ = '=';
    }
    *out = 0;
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int b64_decode(const char *in, unsigned char *out, size_t outmax)
{
    size_t n = 0;
    int quad[4], qn = 0;
    while (*in && n < outmax) {
        int v;
        if (*in == '=') break;
        v = b64val(*in++);
        if (v < 0) continue;
        quad[qn++] = v;
        if (qn == 4) {
            if (n + 2 < outmax) {
                out[n++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
                out[n++] = (unsigned char)(((quad[1] & 15) << 4) | (quad[2] >> 2));
                out[n++] = (unsigned char)(((quad[2] & 3) << 6) | quad[3]);
            }
            qn = 0;
        }
    }
    if (qn == 2 && n < outmax) out[n++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
    else if (qn == 3 && n + 1 < outmax) {
        out[n++] = (unsigned char)((quad[0] << 2) | (quad[1] >> 4));
        out[n++] = (unsigned char)(((quad[1] & 15) << 4) | (quad[2] >> 2));
    }
    return (int)n;
}

/* ---------------- recuperation aleatoire systeme ---------------- */
void random_bytes(unsigned char *buf, size_t n)
{
    size_t i;
#ifdef _WIN32
    {
        /* BCryptGenRandom est disponible depuis Windows Vista */
        typedef NTSTATUS (WINAPI *pBCryptGenRandom)(void *, unsigned char *, unsigned long, unsigned long);
        HMODULE h = LoadLibraryA("bcrypt.dll");
        if (h) {
            pBCryptGenRandom f = (pBCryptGenRandom)GetProcAddress(h, "BCryptGenRandom");
            if (f && f(NULL, buf, (unsigned long)n, 2 /* SYSTEM_PREFERRED_RNG */) == 0) return;
        }
    }
#endif
    {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            if (fread(buf, 1, n, f) == n) { fclose(f); return; }
            fclose(f);
        }
    }
    /* secours : jamais ideal, mais evite un blocage complet */
    srand((unsigned)time(NULL) ^ (unsigned)(size_t)buf);
    for (i = 0; i < n; i++) buf[i] = (unsigned char)(rand() & 0xff);
}

char *url_decode(const char *s)
{
    size_t n = strlen(s), i = 0, o = 0;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    while (i < n) {
        if (s[i] == '%' && i + 2 < n) {
            int hi = s[i + 1], lo = s[i + 2];
            hi = (hi >= '0' && hi <= '9') ? hi - '0' : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 : -1;
            lo = (lo >= '0' && lo <= '9') ? lo - '0' : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 : -1;
            if (hi >= 0 && lo >= 0) { out[o++] = (char)((hi << 4) | lo); i += 3; continue; }
        }
        out[o++] = (s[i] == '+') ? ' ' : s[i];
        i++;
    }
    out[o] = 0;
    return out;
}

char *json_escape(const char *s)
{
    sbuf b;
    if (!s) return NULL;
    sb_init(&b);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  sb_add(&b, "\\\""); break;
            case '\\': sb_add(&b, "\\\\"); break;
            case '\n': sb_add(&b, "\\n"); break;
            case '\r': sb_add(&b, "\\r"); break;
            case '\t': sb_add(&b, "\\t"); break;
            default:
                if (c < 0x20) sb_printf(&b, "\\u%04x", c);
                else sb_addn(&b, (const char *)&c, 1);
        }
    }
    return sb_take(&b);
}

void log_write(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("%s\n", line);
    fflush(stdout);
}

long now_ts(void) { return (long)time(NULL); }

void date_str(char *out, size_t cap, time_t t)
{
    struct tm *tm = localtime(&t);
    if (!tm) { safe_copy(out, cap, ""); return; }
    strftime(out, cap, "%Y-%m-%d", tm);
}

void safe_copy(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

int const_eq(const char *a, const char *b)
{
    size_t la, lb, i;
    unsigned char d = 0;
    if (!a || !b) return 0;
    la = strlen(a); lb = strlen(b);
    if (la != lb) return 0;
    for (i = 0; i < la; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
