/* auth.c — hachage des mots de passe (PBKDF2-HMAC-SHA256), sessions, anti-bruteforce */
#include "app.h"

/* ------------------------- SHA-256 ------------------------- */
typedef struct {
    unsigned int  h[8];
    unsigned long long len;
    unsigned char buf[64];
    size_t buflen;
} sha256_ctx;

static const unsigned int K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    unsigned int w[64], a, b, cc, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
               ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        unsigned int s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void sha256_init(sha256_ctx *c)
{
    static const unsigned int iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(c->h, iv, sizeof(iv));
    c->len = 0;
    c->buflen = 0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    c->len += (unsigned long long)n * 8;
    while (n) {
        size_t take = 64 - c->buflen;
        if (take > n) take = n;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take; p += take; n -= take;
        if (c->buflen == 64) { sha256_block(c, c->buf); c->buflen = 0; }
    }
}

static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    unsigned long long bits = c->len;
    unsigned char pad = 0x80;
    int i;
    sha256_update(c, &pad, 1);
    pad = 0x00;
    while (c->buflen != 56) sha256_update(c, &pad, 1);
    {
        unsigned char lb[8];
        for (i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
        sha256_update(c, lb, 8);
    }
    for (i = 0; i < 8; i++) {
        out[i * 4]     = (unsigned char)(c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(c->h[i]);
    }
}

static void hmac_sha256(const unsigned char *key, size_t keylen,
                        const unsigned char *msg, size_t msglen, unsigned char out[32])
{
    unsigned char k[64], ipad[64], opad[64], inner[32];
    sha256_ctx c;
    size_t i;
    memset(k, 0, sizeof(k));
    if (keylen > 64) { sha256_init(&c); sha256_update(&c, key, keylen); sha256_final(&c, k); }
    else memcpy(k, key, keylen);
    for (i = 0; i < 64; i++) { ipad[i] = (unsigned char)(k[i] ^ 0x36); opad[i] = (unsigned char)(k[i] ^ 0x5c); }
    sha256_init(&c); sha256_update(&c, ipad, 64); sha256_update(&c, msg, msglen); sha256_final(&c, inner);
    sha256_init(&c); sha256_update(&c, opad, 64); sha256_update(&c, inner, 32); sha256_final(&c, out);
}

#define PBKDF2_ITER 100000
#define SALT_LEN 16

static void pbkdf2(const char *pw, const unsigned char *salt, size_t saltlen,
                   int iter, unsigned char out[32])
{
    unsigned char u[32], t[32], block[4];
    size_t i;
    int it;
    block[0] = block[1] = block[2] = 0; block[3] = 1;
    /* HMAC(PBKDF2) : la cle est le mot de passe, ramene a 64 octets si besoin */
    {
        unsigned char key[64];
        size_t kl = strlen(pw);
        if (kl > 64) kl = 64;
        memset(key, 0, sizeof(key));
        memcpy(key, pw, kl);
        /* U1 = HMAC(P, salt || INT) */
        {
            unsigned char *msg = (unsigned char *)malloc(saltlen + 4);
            memcpy(msg, salt, saltlen);
            memcpy(msg + saltlen, block, 4);
            hmac_sha256(key, kl, msg, saltlen + 4, u);
            free(msg);
        }
        memcpy(t, u, 32);
        for (it = 1; it < iter; it++) {
            hmac_sha256(key, kl, u, 32, u);
            for (i = 0; i < 32; i++) t[i] ^= u[i];
        }
        memcpy(out, t, 32);
    }
}

void password_hash(const char *pw, char *out, size_t outcap)
{
    unsigned char salt[SALT_LEN], dk[32];
    char sb64[64], hb64[64];
    random_bytes(salt, sizeof(salt));
    pbkdf2(pw, salt, sizeof(salt), PBKDF2_ITER, dk);
    b64_encode(salt, sizeof(salt), sb64);
    b64_encode(dk, 32, hb64);
    snprintf(out, outcap, "pbkdf2$%d$%s$%s", PBKDF2_ITER, sb64, hb64);
}

int password_verify(const char *pw, const char *stored)
{
    char tmp[512], *p, *salt_b64, *hash_b64;
    unsigned char salt[64], dk[32], expect[32];
    int iter, sl, hl;
    if (!stored || strncmp(stored, "pbkdf2$", 7) != 0) return 0;
    safe_copy(tmp, sizeof(tmp), stored + 7);
    p = strchr(tmp, '$');
    if (!p) return 0;
    *p = 0;
    iter = atoi(tmp);
    salt_b64 = p + 1;
    p = strchr(salt_b64, '$');
    if (!p) return 0;
    *p = 0;
    hash_b64 = p + 1;
    if (iter < 1000 || iter > 1000000) return 0;
    sl = b64_decode(salt_b64, salt, sizeof(salt));
    hl = b64_decode(hash_b64, expect, sizeof(expect));
    if (sl <= 0 || hl != 32) return 0;
    pbkdf2(pw, salt, (size_t)sl, iter, dk);
    {
        unsigned char diff = 0;
        int i;
        for (i = 0; i < 32; i++) diff |= (unsigned char)(dk[i] ^ expect[i]);
        return diff == 0;
    }
}

/* ------------------------- sessions ------------------------- */
session_t g_sessions[MAX_SESSIONS];

char *session_create(sqlite3_int64 uid, const char *role)
{
    unsigned char raw[32];
    char hex[65];
    int i, idx = -1;
    for (i = 0; i < MAX_SESSIONS; i++) if (!g_sessions[i].used) { idx = i; break; }
    if (idx < 0) {
        session_prune();
        for (i = 0; i < MAX_SESSIONS; i++) if (!g_sessions[i].used) { idx = i; break; }
        if (idx < 0) return NULL;
    }
    random_bytes(raw, sizeof(raw));
    for (i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", raw[i]);
    hex[64] = 0;
    memset(&g_sessions[idx], 0, sizeof(session_t));
    g_sessions[idx].used = 1;
    safe_copy(g_sessions[idx].token, sizeof(g_sessions[idx].token), hex);
    g_sessions[idx].uid = uid;
    safe_copy(g_sessions[idx].role, sizeof(g_sessions[idx].role), role);
    g_sessions[idx].last = time(NULL);
    return g_sessions[idx].token;
}

int session_index(const char *token)
{
    int i;
    if (!token || strlen(token) != 64) return -1;
    for (i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].used && const_eq(g_sessions[i].token, token)) {
            if (time(NULL) - g_sessions[i].last > SESSION_TTL) {
                g_sessions[i].used = 0;
                return -1;
            }
            g_sessions[i].last = time(NULL);
            return i;
        }
    }
    return -1;
}

void session_destroy(const char *token)
{
    int i = session_index(token);
    if (i >= 0) { memset(&g_sessions[i], 0, sizeof(session_t)); }
}

void session_prune(void)
{
    int i;
    time_t now = time(NULL);
    for (i = 0; i < MAX_SESSIONS; i++)
        if (g_sessions[i].used && now - g_sessions[i].last > SESSION_TTL) memset(&g_sessions[i], 0, sizeof(session_t));
}

/* ------------------------- anti bruteforce ------------------------- */
typedef struct { char key[128]; int fails; time_t until; } throttle_t;
static throttle_t g_thr[64];

const char *login_throttle_check(const char *email)
{
    int i;
    time_t now = time(NULL);
    for (i = 0; i < 64; i++) {
        if (g_thr[i].key[0] && const_eq(g_thr[i].key, email)) {
            if (g_thr[i].until > now) {
                static char msg[96];
                snprintf(msg, sizeof(msg), "Trop de tentatives. Reessayez dans %ld s.",
                         (long)(g_thr[i].until - now));
                return msg;
            }
        }
    }
    return NULL;
}

void login_throttle_fail(const char *email)
{
    int i, slot = -1;
    time_t now = time(NULL);
    for (i = 0; i < 64; i++) {
        if (g_thr[i].key[0] && const_eq(g_thr[i].key, email)) { slot = i; break; }
        if (!g_thr[i].key[0] && slot < 0) slot = i;
    }
    if (slot < 0) slot = (int)(now % 64);
    if (!const_eq(g_thr[slot].key, email)) {
        safe_copy(g_thr[slot].key, sizeof(g_thr[slot].key), email);
        g_thr[slot].fails = 0;
    }
    g_thr[slot].fails++;
    /* 5 essais, puis blocage progressif : 30 s, 60 s, 120 s ... max 15 min */
    if (g_thr[slot].fails >= 5) {
        long extra = 30L * (1L << ((g_thr[slot].fails - 5) > 5 ? 5 : (g_thr[slot].fails - 5)));
        if (extra > 900) extra = 900;
        g_thr[slot].until = now + extra;
    }
}

void login_throttle_ok(const char *email)
{
    int i;
    for (i = 0; i < 64; i++)
        if (g_thr[i].key[0] && const_eq(g_thr[i].key, email)) { g_thr[i].fails = 0; g_thr[i].until = 0; g_thr[i].key[0] = 0; }
}

/* ------------------------- journal d'audit ------------------------- */
void audit(sqlite3_int64 uid, const char *action, const char *detail)
{
    char ts[32], id[24];
    const char *p[4];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) return;
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    snprintf(id, sizeof(id), "%lld", (long long)uid);
    p[0] = id; p[1] = action; p[2] = detail ? detail : ""; p[3] = ts;
    db_exec_params("INSERT INTO audit(user_id,action,detail,created_at) VALUES(CAST(?1 AS INTEGER),?2,?3,?4)", 4, p);
    log_write("[audit] %s %s %s", action, detail ? detail : "", ts);
}
