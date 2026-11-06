/* api.c — routes JSON de l'application (auth, annonces, devoirs, notes, ...) */
#include "app.h"
#include <ctype.h>

/* ------------------------- aides ------------------------- */
static cJSON *body_json(request_t *rq)
{
    if (!rq->body || !rq->body_len) return NULL;
    return cJSON_Parse(rq->body);
}

static const char *js(cJSON *o, const char *k, const char *def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!v) return def;
    if (cJSON_IsString(v)) return v->valuestring ? v->valuestring : def;
    if (cJSON_IsNumber(v)) { static char b[32]; snprintf(b, sizeof(b), "%g", v->valuedouble); return b; }
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? "1" : "0";
    return def;
}

static sqlite3_int64 jid(cJSON *o, const char *k)
{
    cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
    if (!v) return 0;
    if (cJSON_IsNumber(v)) return (sqlite3_int64)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return (sqlite3_int64)atoll(v->valuestring);
    return 0;
}

static void query_param(request_t *rq, const char *key, char *out, size_t cap)
{
    char *q = url_decode(rq->query);
    char *k;
    out[0] = 0;
    if (!q) return;
    k = q;
    while (k && *k) {
        char *amp = strchr(k, '&');
        char *eq = strchr(k, '=');
        size_t klen = eq ? (size_t)(eq - k) : strlen(k);
        if (eq && klen == strlen(key) && !strncmp(k, key, klen)) {
            char *v = eq + 1;
            size_t vlen = amp ? (size_t)(amp - v) : strlen(v);
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, v, vlen);
            out[vlen] = 0;
            break;
        }
        k = amp ? amp + 1 : NULL;
    }
    free(q);
}

static int jb(cJSON *o, const char *k, int def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!v) return def;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? 1 : 0;
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v) && v->valuestring) return atoi(v->valuestring) ? 1 : 0;
    return def;
}

typedef struct {
    int ok;
    cJSON *body;
    int sess;
    sqlite3_int64 uid;
    const char *role;
    const char *email;
} ctx_t;

static int load_ctx(request_t *rq, ctx_t *c, const char *roles)
{
    int idx;
    const char *p[1];
    char id[24];
    cJSON *rows;
    memset(c, 0, sizeof(*c));
    c->body = body_json(rq);
    idx = session_index(rq->bearer);
    if (idx < 0) { http_error(rq, 401, "session_expired"); if (c->body) cJSON_Delete(c->body); return 0; }
    c->sess = idx;
    c->uid = g_sessions[idx].uid;
    snprintf(id, sizeof(id), "%lld", (long long)c->uid);
    p[0] = id;
    rows = db_query("SELECT email,role,active,must_change FROM users WHERE id=?", 1, p);
    {
        cJSON *row = cJSON_GetArrayItem(rows, 0);
        if (!row || !jb(row, "active", 0)) {
            cJSON_Delete(rows);
            http_error(rq, 401, "account_disabled");
            if (c->body) cJSON_Delete(c->body);
            return 0;
        }
        c->role = strdup(js(row, "role", ""));   /* libere par free_ctx */
        c->email = strdup(js(row, "email", ""));
        safe_copy(g_sessions[idx].role, sizeof(g_sessions[idx].role), c->role);
    }
    cJSON_Delete(rows);
    if (roles && roles[0] && !strstr(roles, c->role)) {
        http_error(rq, 403, "forbidden");
        free((void *)c->role); free((void *)c->email);
        if (c->body) cJSON_Delete(c->body);
        return 0;
    }
    return 1;
}

static void free_ctx(ctx_t *c)
{
    if (c->body) cJSON_Delete(c->body);
    c->body = NULL;
}

static int role_can_manage(const char *role) { return !strcmp(role, "admin") || !strcmp(role, "moderator"); }

/* liste d'identifiants d'enfants du parent connecte */
static cJSON *parent_children(ctx_t *c)
{
    char id[24];
    const char *p[1];
    snprintf(id, sizeof(id), "%lld", (long long)c->uid);
    p[0] = id;
    return db_query("SELECT * FROM students WHERE parent_id=? ORDER BY first_name", 1, p);
}

static void ok_true(request_t *rq)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    http_json(rq, 200, o);
    cJSON_Delete(o);
}

static int pw_strong_enough(const char *pw, char *why, size_t cap)
{
    size_t n = pw ? strlen(pw) : 0, i;
    int lower = 0, upper = 0, digit = 0, other = 0;
    if (n < 8) { safe_copy(why, cap, "password_too_short"); return 0; }
    if (n > 128) { safe_copy(why, cap, "password_too_long"); return 0; }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)pw[i];
        if (c >= 'a' && c <= 'z') lower = 1;
        else if (c >= 'A' && c <= 'Z') upper = 1;
        else if (c >= '0' && c <= '9') digit = 1;
        else other = 1;
    }
    if (!(lower && upper && digit)) { safe_copy(why, cap, "password_weak"); return 0; }
    (void)other;
    if (!strcmp(pw, "12345678") || !strcmp(pw, "password")) { safe_copy(why, cap, "password_weak"); return 0; }
    return 1;
}

static int valid_email(const char *e)
{
    const char *at;
    if (!e) return 0;
    if (strlen(e) < 6 || strlen(e) > 120) return 0;
    at = strchr(e, '@');
    if (!at || at == e) return 0;
    if (!strchr(at + 1, '.')) return 0;
    if (strchr(e, ' ')) return 0;
    return 1;
}

/* ------------------------- AUTH ------------------------- */
static void route_config(request_t *rq)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "app", APP_NAME);
    cJSON_AddStringToObject(o, "version", APP_VERSION);
    cJSON_AddStringToObject(o, "school_name", "Groupe Scolaire Les Théorèmes");
    cJSON_AddStringToObject(o, "school_address", "Lotissement Est Villa N°54, Génie Zéralda — Alger");
    cJSON_AddStringToObject(o, "school_phone", "+213 (0) 665 906 257");
    cJSON_AddStringToObject(o, "school_phone2", "+213 (0) 781 963 081");
    cJSON_AddStringToObject(o, "school_landline", "+213 (0) 28 52 92 53");
    cJSON_AddStringToObject(o, "school_email", "lestheoremesetablissement@gmail.com");
    cJSON_AddStringToObject(o, "school_facebook", "https://www.facebook.com/ecole.privee.les.theoremes");
    cJSON_AddStringToObject(o, "school_maps", "https://maps.app.goo.gl/DPbTvgcN5V6zVmqp9");
    cJSON_AddNumberToObject(o, "db_users", db_count("SELECT COUNT(*) FROM users", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
}

static void route_login(request_t *rq)
{
    cJSON *b = body_json(rq);
    char email[160], ts[32];
    const char *pw, *thr;
    const char *p[3];
    cJSON *rows;
    if (!b) { http_error(rq, 400, "bad_request"); return; }
    safe_copy(email, sizeof(email), js(b, "email", ""));
    pw = js(b, "password", "");
    {
        size_t k;
        for (k = 0; email[k]; k++) email[k] = (char)tolower((unsigned char)email[k]);
    }
    if (!valid_email(email) || !pw[0]) { cJSON_Delete(b); http_error(rq, 400, "invalid_credentials"); return; }

    thr = login_throttle_check(email);
    if (thr) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 0);
        cJSON_AddStringToObject(o, "error", "too_many_attempts");
        cJSON_AddStringToObject(o, "message", thr);
        http_json(rq, 429, o);
        cJSON_Delete(o);
        cJSON_Delete(b);
        return;
    }

    p[0] = email;
    rows = db_query("SELECT id,pass,role,first_name,last_name,active,must_change,locked_until FROM users WHERE email=?", 1, p);
    {
        cJSON *row = cJSON_GetArrayItem(rows, 0);
        if (!row) {
            /* temps constant : on fait quand meme un calcul de hachage */
            password_verify(pw, "pbkdf2$100000$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
            login_throttle_fail(email);
            audit(0, "login_fail", email);
            cJSON_Delete(rows);
            cJSON_Delete(b);
            http_error(rq, 401, "invalid_credentials");
            return;
        }
        if (!password_verify(pw, js(row, "pass", ""))) {
            login_throttle_fail(email);
            audit(0, "login_fail", email);
            cJSON_Delete(rows);
            cJSON_Delete(b);
            http_error(rq, 401, "invalid_credentials");
            return;
        }
        if (!jb(row, "active", 1)) {
            cJSON_Delete(rows); cJSON_Delete(b);
            http_error(rq, 403, "account_disabled");
            return;
        }
        {
            char token_src[24];
            char *token;
            cJSON *o;
            sqlite3_int64 uid = (sqlite3_int64)cJSON_GetObjectItem(row, "id")->valuedouble;
            const char *role = js(row, "role", "parent");
            struct tm *tm;
            time_t t = time(NULL);
            tm = localtime(&t);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
            snprintf(token_src, sizeof(token_src), "%lld", (long long)uid);
            token = session_create(uid, role);
            if (!token) { cJSON_Delete(rows); cJSON_Delete(b); http_error(rq, 500, "internal"); return; }
            p[0] = ts; p[1] = token_src;
            db_exec_params("UPDATE users SET last_login=?, fails=0, locked_until=0 WHERE id=CAST(?2 AS INTEGER)", 2, p);
            login_throttle_ok(email);
            audit(uid, "login_ok", email);
            o = cJSON_CreateObject();
            cJSON_AddBoolToObject(o, "ok", 1);
            cJSON_AddStringToObject(o, "token", token);
            cJSON_AddStringToObject(o, "role", role);
            cJSON_AddNumberToObject(o, "uid", (double)uid);
            cJSON_AddStringToObject(o, "first_name", js(row, "first_name", ""));
            cJSON_AddStringToObject(o, "last_name", js(row, "last_name", ""));
            cJSON_AddBoolToObject(o, "must_change", jb(row, "must_change", 0) != 0);
            http_json(rq, 200, o);
            cJSON_Delete(o);
        }
    }
    cJSON_Delete(rows);
    cJSON_Delete(b);
}

static void route_me(request_t *rq)
{
    ctx_t c;
    char id[24];
    const char *p[1];
    cJSON *rows, *o;
    if (!load_ctx(rq, &c, NULL)) return;
    snprintf(id, sizeof(id), "%lld", (long long)c.uid);
    p[0] = id;
    rows = db_query("SELECT id,email,role,first_name,last_name,phone,must_change,last_login,created_at"
                    " FROM users WHERE id=?", 1, p);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "user", cJSON_GetArrayItem(rows, 0) ? cJSON_Duplicate(cJSON_GetArrayItem(rows, 0), 1) : cJSON_CreateObject());
    cJSON_Delete(rows);
    if (!strcmp(c.role, "parent")) {
        cJSON_AddItemToObject(o, "children", parent_children(&c));
    }
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c);
    free((void *)c.role); free((void *)c.email);
}

static void route_logout(request_t *rq)
{
    session_destroy(rq->bearer);
    ok_true(rq);
}

static void route_change_password(request_t *rq)
{
    ctx_t c;
    char hash[256], id[24];
    const char *p[2];
    const char *oldp, *newp;
    char whybuf[48] = "";
    cJSON *rows;
    int idx;
    if (!load_ctx(rq, &c, NULL)) return;
    /* un parent ne change pas son mot de passe lui-meme :
       seule la direction (ou un moderateur) peut lui en redonner un */
    if (!strcmp(c.role, "parent")) {
        http_error(rq, 403, "password_admin_only");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    oldp = js(c.body, "old", "");
    newp = js(c.body, "new", "");
    if (!pw_strong_enough(newp, whybuf, sizeof(whybuf))) {
        http_error(rq, 400, whybuf);
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    snprintf(id, sizeof(id), "%lld", (long long)c.uid);
    p[0] = id;
    rows = db_query("SELECT pass FROM users WHERE id=?", 1, p);
    if (!rows || !cJSON_GetArrayItem(rows, 0) || !password_verify(oldp, js(cJSON_GetArrayItem(rows, 0), "pass", ""))) {
        cJSON_Delete(rows);
        http_error(rq, 400, "wrong_password");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    cJSON_Delete(rows);
    password_hash(newp, hash, sizeof(hash));
    p[0] = hash; p[1] = id;
    db_exec_params("UPDATE users SET pass=?, must_change=0 WHERE id=CAST(?2 AS INTEGER)", 2, p);
    /* deconnecte toutes les autres sessions de cet utilisateur */
    for (idx = 0; idx < MAX_SESSIONS; idx++)
        if (g_sessions[idx].used && g_sessions[idx].uid == c.uid && idx != c.sess)
            memset(&g_sessions[idx], 0, sizeof(session_t));
    audit(c.uid, "password_change", c.email);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- COMPTES (admin) ------------------------- */
static void route_users_list(request_t *rq)
{
    ctx_t c;
    cJSON *rows, *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    rows = db_query("SELECT id,email,role,first_name,last_name,phone,active,must_change,created_at,last_login,"
                    " (SELECT s.first_name || ' ' || s.last_name FROM students s WHERE s.parent_id=users.id"
                    "   ORDER BY s.id LIMIT 1) AS child_name,"
                    " (SELECT COUNT(*) FROM students s WHERE s.parent_id=users.id) AS child_count"
                    " FROM users ORDER BY role DESC, last_name", 0, NULL);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "users", rows);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------------------------------------------------
   creation d'un compte par la direction
   - role parent  : un eleve est OBLIGATOIRE (sinon le parent ne verrait rien)
   - tout se fait dans une seule transaction : soit tout, soit rien
   ------------------------------------------------------------------ */
static void route_users_create(request_t *rq)
{
    ctx_t c;
    char email[160], hash[256], ts[32], id[24];
    const char *role, *fn, *ln, *phone, *pw;
    const char *kfn, *kln, *level, *cls;
    char whybuf[48] = "";
    const char *p[9];
    sqlite3_int64 newid, kid = 0;
    cJSON *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    role = js(c.body, "role", "parent");
    if (strcmp(role, "parent") && strcmp(role, "moderator")) {
        http_error(rq, 403, "role_not_allowed");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    /* seul l'admin peut creer / toucher aux moderateurs */
    if (!strcmp(role, "moderator") && strcmp(c.role, "admin")) {
        http_error(rq, 403, "forbidden");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    safe_copy(email, sizeof(email), js(c.body, "email", ""));
    {
        size_t k;
        for (k = 0; email[k]; k++) email[k] = (char)tolower((unsigned char)email[k]);
    }
    fn = js(c.body, "first_name", "");
    ln = js(c.body, "last_name", "");
    phone = js(c.body, "phone", "");
    pw = js(c.body, "password", "");
    if (!valid_email(email) || !fn[0] || !ln[0]) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    /* un compte parent doit obligatoirement avoir au moins un enfant */
    kfn = js(c.body, "child_first_name", "");
    kln = js(c.body, "child_last_name", "");
    level = js(c.body, "child_level", "");
    cls = js(c.body, "child_class", "");
    if (!strcmp(role, "parent") && (!kfn[0] || !kln[0] || (!level[0] && !cls[0]))) {
        http_error(rq, 400, "child_required");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    if (!pw_strong_enough(pw, whybuf, sizeof(whybuf))) {
        http_error(rq, 400, whybuf);
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    p[0] = email;
    if (db_count("SELECT COUNT(*) FROM users WHERE email=?", 1, p) > 0) {
        http_error(rq, 409, "email_taken");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    password_hash(pw, hash, sizeof(hash));
    date_str(ts, sizeof(ts), time(NULL));
    snprintf(id, sizeof(id), "%lld", (long long)c.uid);

    db_exec("BEGIN");
    /* must_change seulement pour le personnel : un parent ne change pas son mot de passe */
    p[0] = email; p[1] = hash; p[2] = role; p[3] = fn; p[4] = ln; p[5] = phone; p[6] = ts; p[7] = id;
    p[8] = strcmp(role, "parent") ? "1" : "0";
    newid = db_exec_params("INSERT INTO users(email,pass,role,first_name,last_name,phone,created_at,created_by,must_change)"
                           " VALUES(?,?,?,?,?,?,?,CAST(?8 AS INTEGER),CAST(?9 AS INTEGER))", 9, p);
    if (newid < 0) {
        db_exec("ROLLBACK");
        http_error(rq, 500, "internal");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }

    /* l'enfant est cree tout de suite, rattache a ce parent */
    if (!strcmp(role, "parent")) {
        char pid[24];
        snprintf(pid, sizeof(pid), "%lld", (long long)newid);
        p[0] = pid;
        p[1] = kfn;
        p[2] = kln;
        p[3] = level;
        p[4] = cls;
        p[5] = ts;
        kid = db_exec_params("INSERT INTO students(parent_id,first_name,last_name,level,class_name,created_at)"
                             " VALUES(CAST(?1 AS INTEGER),?,?,?,?,?)", 6, p);
        if (kid < 0) {
            db_exec("ROLLBACK");
            http_error(rq, 500, "internal");
            free_ctx(&c); free((void *)c.role); free((void *)c.email);
            return;
        }
    }
    db_exec("COMMIT");

    audit(c.uid, "user_create", email);
    if (kid > 0) audit(c.uid, "student_create", kfn);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddNumberToObject(o, "id", (double)newid);
    if (kid > 0) {
        cJSON_AddNumberToObject(o, "student_id", (double)kid);
        cJSON_AddStringToObject(o, "child_name", kfn);
    }
    http_json(rq, 201, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_users_update(request_t *rq)
{
    ctx_t c;
    const char *p[6];
    char target[24];
    sqlite3_int64 tid;
    cJSON *rows;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    tid = jid(c.body, "id");
    if (tid <= 0) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(target, sizeof(target), "%lld", (long long)tid);
    p[0] = target;
    rows = db_query("SELECT role FROM users WHERE id=?", 1, p);
    if (!cJSON_GetArrayItem(rows, 0)) {
        cJSON_Delete(rows);
        http_error(rq, 404, "not_found");
        free_ctx(&c); free((void *)c.role); free((void *)c.email);
        return;
    }
    {
        const char *trole = js(cJSON_GetArrayItem(rows, 0), "role", "");
        cJSON_Delete(rows);
        if (!strcmp(trole, "admin")) {
            http_error(rq, 403, "cannot_modify_admin");
            free_ctx(&c); free((void *)c.role); free((void *)c.email);
            return;
        }
        if (!strcmp(trole, "moderator") && strcmp(c.role, "admin")) {
            http_error(rq, 403, "forbidden");
            free_ctx(&c); free((void *)c.role); free((void *)c.email);
            return;
        }
    }
    /* champs autorises : identite, etat, mot de passe */
    {
        cJSON *pwitem = cJSON_GetObjectItem(c.body, "password");
        if (pwitem && cJSON_IsString(pwitem) && pwitem->valuestring[0]) {
            char hash[256], whybuf[48] = "";
            if (!pw_strong_enough(pwitem->valuestring, whybuf, sizeof(whybuf))) {
                http_error(rq, 400, whybuf);
                free_ctx(&c); free((void *)c.role); free((void *)c.email);
                return;
            }
            password_hash(pwitem->valuestring, hash, sizeof(hash));
            p[0] = hash; p[1] = target;
            db_exec_params("UPDATE users SET pass=?, must_change=1 WHERE id=CAST(?2 AS INTEGER)", 2, p);
            audit(c.uid, "user_password_reset", target);
        }
    }
    if (cJSON_GetObjectItem(c.body, "active")) {
        p[0] = jb(c.body, "active", 1) ? "1" : "0"; p[1] = target;
        db_exec_params("UPDATE users SET active=CAST(?1 AS INTEGER) WHERE id=CAST(?2 AS INTEGER)", 2, p);
        audit(c.uid, "user_active_toggle", target);
    }
    if (cJSON_GetObjectItem(c.body, "first_name")) {
        p[0] = js(c.body, "first_name", ""); p[1] = target;
        db_exec_params("UPDATE users SET first_name=? WHERE id=CAST(?2 AS INTEGER)", 2, p);
    }
    if (cJSON_GetObjectItem(c.body, "last_name")) {
        p[0] = js(c.body, "last_name", ""); p[1] = target;
        db_exec_params("UPDATE users SET last_name=? WHERE id=CAST(?2 AS INTEGER)", 2, p);
    }
    if (cJSON_GetObjectItem(c.body, "phone")) {
        p[0] = js(c.body, "phone", ""); p[1] = target;
        db_exec_params("UPDATE users SET phone=? WHERE id=CAST(?2 AS INTEGER)", 2, p);
    }
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_users_delete(request_t *rq)
{
    ctx_t c;
    char target[24];
    const char *p[1];
    sqlite3_int64 tid;
    cJSON *rows;
    if (!load_ctx(rq, &c, "admin")) return;   /* suppression : admin seulement */
    tid = jid(c.body, "id");
    if (tid <= 0) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    if (tid == c.uid) {
        http_error(rq, 400, "cannot_delete_self");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(target, sizeof(target), "%lld", (long long)tid);
    p[0] = target;
    rows = db_query("SELECT role FROM users WHERE id=?", 1, p);
    if (!cJSON_GetArrayItem(rows, 0)) {
        cJSON_Delete(rows); http_error(rq, 404, "not_found");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    if (!strcmp(js(cJSON_GetArrayItem(rows, 0), "role", ""), "admin")) {
        cJSON_Delete(rows); http_error(rq, 403, "cannot_delete_admin");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    cJSON_Delete(rows);
    db_exec_params("DELETE FROM users WHERE id=CAST(?1 AS INTEGER)", 1, p);
    audit(c.uid, "user_delete", target);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- ELEVES ------------------------- */
static void route_students(request_t *rq)
{
    ctx_t c;
    cJSON *rows, *o;
    if (!load_ctx(rq, &c, NULL)) return;
    if (!strcmp(c.role, "parent")) rows = parent_children(&c);
    else rows = db_query("SELECT s.*, u.first_name || ' ' || u.last_name AS parent_name, u.email AS parent_email"
                         " FROM students s JOIN users u ON u.id=s.parent_id ORDER BY s.class_name, s.last_name", 0, NULL);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "students", rows);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_students_create(request_t *rq)
{
    ctx_t c;
    const char *p[8];
    char parent[24], ts[32];
    sqlite3_int64 pid, id;
    cJSON *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    pid = jid(c.body, "parent_id");
    if (pid <= 0) {
        http_error(rq, 400, "invalid_parent");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(parent, sizeof(parent), "%lld", (long long)pid);
    p[0] = parent;
    if (db_count("SELECT COUNT(*) FROM users WHERE id=? AND role='parent'", 1, p) == 0) {
        http_error(rq, 400, "invalid_parent");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    date_str(ts, sizeof(ts), time(NULL));
    p[0] = parent;
    p[1] = js(c.body, "first_name", "");
    p[2] = js(c.body, "last_name", "");
    p[3] = js(c.body, "birth", "");
    p[4] = js(c.body, "level", "");
    p[5] = js(c.body, "class_name", "");
    p[6] = js(c.body, "bus_route", "");
    p[7] = ts;
    if (!p[1][0] || !p[2][0]) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    id = db_exec_params("INSERT INTO students(parent_id,first_name,last_name,birth,level,class_name,bus_route,created_at)"
                        " VALUES(CAST(?1 AS INTEGER),?,?,?,?,?,?,?)", 8, p);
    audit(c.uid, "student_create", p[2]);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddNumberToObject(o, "id", (double)id);
    http_json(rq, 201, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- ANNONCES ------------------------- */
static void route_announcements(request_t *rq)
{
    ctx_t c;
    cJSON *rows, *o;
    if (!load_ctx(rq, &c, NULL)) return;
    rows = db_query("SELECT a.*, u.first_name || ' ' || u.last_name AS author"
                    " FROM announcements a LEFT JOIN users u ON u.id=a.author_id"
                    " ORDER BY a.pinned DESC, a.created_at DESC", 0, NULL);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "announcements", rows);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_announcements_save(request_t *rq)
{
    ctx_t c;
    const char *p[10];
    char author[24], ts[32], idbuf[24];
    sqlite3_int64 id, aid = 0;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    id = jid(c.body, "id");
    if (!js(c.body, "title_fr", "")[0]) {
        http_error(rq, 400, "title_required");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(author, sizeof(author), "%lld", (long long)c.uid);
    date_str(ts, sizeof(ts), time(NULL));
    p[0] = js(c.body, "title_fr", "");
    p[1] = js(c.body, "title_ar", "");
    p[2] = js(c.body, "title_en", "");
    p[3] = js(c.body, "body_fr", "");
    p[4] = js(c.body, "body_ar", "");
    p[5] = js(c.body, "body_en", "");
    p[6] = js(c.body, "category", "info");
    p[7] = jb(c.body, "pinned", 0) ? "1" : "0";
    p[8] = author;
    p[9] = ts;
    if (id > 0) {
        snprintf(idbuf, sizeof(idbuf), "%lld", (long long)id);
        p[8] = idbuf;
        db_exec_params("UPDATE announcements SET title_fr=?,title_ar=?,title_en=?,body_fr=?,body_ar=?,body_en=?,"
                       "category=?,pinned=CAST(? AS INTEGER) WHERE id=CAST(?9 AS INTEGER)", 9, p);
        audit(c.uid, "announcement_update", p[0]);
        aid = id;
    } else {
        aid = db_exec_params("INSERT INTO announcements(title_fr,title_ar,title_en,body_fr,body_ar,body_en,"
                             "category,pinned,author_id,created_at)"
                             " VALUES(?,?,?,?,?,?,?,CAST(?8 AS INTEGER),CAST(?9 AS INTEGER),?)", 10, p);
        audit(c.uid, "announcement_create", p[0]);
    }
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddNumberToObject(o, "id", (double)aid);
        http_json(rq, 200, o);
        cJSON_Delete(o);
    }
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_announcement_delete(request_t *rq)
{
    ctx_t c;
    const char *p[1];
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    p[0] = js(c.body, "id", "0");
    db_exec_params("DELETE FROM announcements WHERE id=CAST(?1 AS INTEGER)", 1, p);
    audit(c.uid, "announcement_delete", p[0]);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- EMPLOI DU TEMPS / DEVOIRS ------------------------- */
static void route_schedule(request_t *rq)
{
    ctx_t c;
    cJSON *rows, *o;
    const char *p[1];
    char cls[32] = "";
    if (!load_ctx(rq, &c, NULL)) return;
    query_param(rq, "class", cls, sizeof(cls));
    if (!cls[0] && !strcmp(c.role, "parent")) {
        cJSON *kids = parent_children(&c);
        cJSON *k0 = cJSON_GetArrayItem(kids, 0);
        if (k0) safe_copy(cls, sizeof(cls), js(k0, "class_name", ""));
        cJSON_Delete(kids);
    }
    if (cls[0]) {
        p[0] = cls;
        rows = db_query("SELECT * FROM schedule WHERE class_name=? ORDER BY day, slot", 1, p);
    } else {
        rows = db_query("SELECT * FROM schedule ORDER BY class_name, day, slot", 0, NULL);
    }
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddStringToObject(o, "class_name", cls);
    cJSON_AddItemToObject(o, "schedule", rows);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_homework(request_t *rq)
{
    ctx_t c;
    cJSON *rows, *o;
    char cls[32] = "";
    if (!load_ctx(rq, &c, NULL)) return;
    query_param(rq, "class", cls, sizeof(cls));
    if (!cls[0] && !strcmp(c.role, "parent")) {
        cJSON *kids = parent_children(&c);
        cJSON *k0 = cJSON_GetArrayItem(kids, 0);
        if (k0) safe_copy(cls, sizeof(cls), js(k0, "class_name", ""));
        cJSON_Delete(kids);
    }
    if (cls[0]) {
        const char *p[1];
        p[0] = cls;
        rows = db_query("SELECT * FROM homework WHERE class_name=? ORDER BY due", 1, p);
    } else rows = db_query("SELECT * FROM homework ORDER BY due", 0, NULL);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "homework", rows);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_homework_create(request_t *rq)
{
    ctx_t c;
    const char *p[10];
    char ts[32];
    cJSON *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    if (!js(c.body, "title_fr", "")[0] || !js(c.body, "class_name", "")[0]) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    date_str(ts, sizeof(ts), time(NULL));
    p[0] = js(c.body, "class_name", "");
    p[1] = js(c.body, "subject", "");
    p[2] = js(c.body, "title_fr", "");
    p[3] = js(c.body, "title_ar", "");
    p[4] = js(c.body, "title_en", "");
    p[5] = js(c.body, "body_fr", "");
    p[6] = js(c.body, "body_ar", "");
    p[7] = js(c.body, "body_en", "");
    p[8] = js(c.body, "due", "");
    p[9] = ts;
    db_exec_params("INSERT INTO homework(class_name,subject,title_fr,title_ar,title_en,body_fr,body_ar,body_en,due,created_at)"
                   " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, p);
    audit(c.uid, "homework_create", p[2]);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    http_json(rq, 201, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- NOTES ------------------------- */
static void route_grades(request_t *rq)
{
    ctx_t c;
    cJSON *o, *list;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    if (!strcmp(c.role, "parent")) {
        cJSON *kids = parent_children(&c);
        cJSON *kid;
        list = cJSON_CreateArray();
        cJSON_ArrayForEach(kid, kids) {
            cJSON *entry = cJSON_CreateObject();
            char sid[24];
            const char *p[1];
            sqlite3_int64 id = (sqlite3_int64)cJSON_GetObjectItem(kid, "id")->valuedouble;
            snprintf(sid, sizeof(sid), "%lld", (long long)id);
            p[0] = sid;
            cJSON_AddItemToObject(entry, "student", cJSON_Duplicate(kid, 1));
            cJSON_AddItemToObject(entry, "grades",
                db_query("SELECT * FROM grades WHERE student_id=? ORDER BY subject, date", 1, p));
            cJSON_AddItemToArray(list, entry);
        }
        cJSON_Delete(kids);
    } else {
        sqlite3_int64 sid = (sqlite3_int64)atoi(js(c.body, "student_id", "0"));
        char b[24];
        const char *p[1];
        snprintf(b, sizeof(b), "%lld", (long long)sid);
        p[0] = b;
        list = db_query("SELECT g.*, s.first_name || ' ' || s.last_name AS student_name, s.class_name"
                        " FROM grades g JOIN students s ON s.id=g.student_id"
                        " WHERE g.student_id=? ORDER BY g.subject, g.date", 1, p);
    }
    cJSON_AddItemToObject(o, "results", list);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- ABSENCES ------------------------- */
static void route_absences(request_t *rq)
{
    ctx_t c;
    cJSON *o, *list;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    if (!strcmp(c.role, "parent")) {
        const char *p[1];
        char pid[24];
        snprintf(pid, sizeof(pid), "%lld", (long long)c.uid);
        p[0] = pid;
        list = db_query("SELECT a.*, s.first_name || ' ' || s.last_name AS student_name, s.class_name"
                        " FROM absences a JOIN students s ON s.id=a.student_id"
                        " WHERE s.parent_id=? ORDER BY a.date DESC", 1, p);
    } else {
        list = db_query("SELECT a.*, s.first_name || ' ' || s.last_name AS student_name, s.class_name"
                        " FROM absences a JOIN students s ON s.id=a.student_id"
                        " ORDER BY a.date DESC", 0, NULL);
    }
    cJSON_AddItemToObject(o, "absences", list);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_absence_create(request_t *rq)
{
    ctx_t c;
    const char *p[6];
    char ts[32], sid[24];
    cJSON *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    date_str(ts, sizeof(ts), time(NULL));
    if (jid(c.body, "student_id") <= 0) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(sid, sizeof(sid), "%lld", (long long)jid(c.body, "student_id"));
    p[0] = sid;
    p[1] = js(c.body, "date", ts);
    p[2] = js(c.body, "kind", "absence");
    p[3] = js(c.body, "reason", "");
    p[4] = jb(c.body, "justified", 0) ? "1" : "0";
    p[5] = ts;
    db_exec_params("INSERT INTO absences(student_id,date,kind,reason,justified,created_at)"
                   " VALUES(CAST(?1 AS INTEGER),?,?,?,CAST(?5 AS INTEGER),?)", 6, p);
    audit(c.uid, "absence_create", sid);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    http_json(rq, 201, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- FRAIS ------------------------- */
static void route_fees(request_t *rq)
{
    ctx_t c;
    cJSON *o, *list;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    if (!strcmp(c.role, "parent")) {
        char pid[24];
        const char *p[1];
        snprintf(pid, sizeof(pid), "%lld", (long long)c.uid);
        p[0] = pid;
        list = db_query("SELECT f.*, s.first_name || ' ' || s.last_name AS student_name, s.class_name"
                        " FROM fees f JOIN students s ON s.id=f.student_id"
                        " WHERE s.parent_id=? ORDER BY f.paid, f.due_date", 1, p);
    } else {
        list = db_query("SELECT f.*, s.first_name || ' ' || s.last_name AS student_name, s.class_name"
                        " FROM fees f JOIN students s ON s.id=f.student_id ORDER BY f.paid, f.due_date", 0, NULL);
    }
    cJSON_AddItemToObject(o, "fees", list);
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_fee_toggle(request_t *rq)
{
    ctx_t c;
    char id[24], ts[32];
    const char *p[3];
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    if (jid(c.body, "id") <= 0) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    snprintf(id, sizeof(id), "%lld", (long long)jid(c.body, "id"));
    date_str(ts, sizeof(ts), time(NULL));
    p[0] = jb(c.body, "paid", 1) ? "1" : "0";
    p[1] = ts;
    p[2] = id;
    db_exec_params("UPDATE fees SET paid=CAST(?1 AS INTEGER), paid_at=CASE WHEN CAST(?1 AS INTEGER)=1 THEN ?2 ELSE '' END"
                   " WHERE id=CAST(?3 AS INTEGER)", 3, p);
    audit(c.uid, "fee_toggle", id);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- MESSAGES ------------------------- */
static void route_messages(request_t *rq)
{
    ctx_t c;
    char uid[24];
    const char *p[1];
    cJSON *rows, *o;
    if (!load_ctx(rq, &c, NULL)) return;
    snprintf(uid, sizeof(uid), "%lld", (long long)c.uid);
    p[0] = uid;
    {
        const char *two[2] = { uid, uid };
        rows = db_query("SELECT m.*, uf.first_name || ' ' || uf.last_name AS from_name,"
                    " ut.first_name || ' ' || ut.last_name AS to_name"
                    " FROM messages m JOIN users uf ON uf.id=m.from_id JOIN users ut ON ut.id=m.to_id"
                    " WHERE m.to_id=? OR m.from_id=? ORDER BY m.id DESC LIMIT 100", 2, two);
    }
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "messages", rows);
    cJSON_AddStringToObject(o, "me", uid);
    cJSON_AddItemToObject(o, "staff", db_query("SELECT id, first_name || ' ' || last_name AS name, role"
                                               " FROM users WHERE role IN ('admin','moderator') AND active=1"
                                               " ORDER BY role", 0, NULL));
    cJSON_AddItemToObject(o, "parents", db_query("SELECT id, first_name || ' ' || last_name AS name, email"
                                                " FROM users WHERE role='parent' AND active=1 ORDER BY last_name", 0, NULL));
    cJSON_AddNumberToObject(o, "unread", db_count("SELECT COUNT(*) FROM messages WHERE to_id=CAST(?1 AS INTEGER) AND is_read=0", 1, p));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    (void)p;
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_messages_read(request_t *rq)
{
    ctx_t c;
    char uid[24];
    const char *p[1];
    if (!load_ctx(rq, &c, NULL)) return;
    snprintf(uid, sizeof(uid), "%lld", (long long)c.uid);
    p[0] = uid;
    db_exec_params("UPDATE messages SET is_read=1 WHERE to_id=CAST(?1 AS INTEGER)", 1, p);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_message_send(request_t *rq)
{
    ctx_t c;
    char from[24], ts[32];
    const char *p[6];
    sqlite3_int64 to_id;
    const char *body;
    if (!load_ctx(rq, &c, NULL)) return;
    to_id = jid(c.body, "to_id");
    body = js(c.body, "body", "");
    if (!body[0] || to_id <= 0) {
        http_error(rq, 400, "invalid_data");
        free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
    }
    /* un parent ne peut ecrire qu'a l'administration */
    {
        char tid[24];
        const char *q[1];
        cJSON *rows;
        snprintf(tid, sizeof(tid), "%lld", (long long)to_id);
        q[0] = tid;
        rows = db_query("SELECT role FROM users WHERE id=?", 1, q);
        if (!cJSON_GetArrayItem(rows, 0)) {
            cJSON_Delete(rows); http_error(rq, 404, "not_found");
            free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
        }
        if (!strcmp(c.role, "parent") && !role_can_manage(js(cJSON_GetArrayItem(rows, 0), "role", ""))) {
            cJSON_Delete(rows); http_error(rq, 403, "forbidden");
            free_ctx(&c); free((void *)c.role); free((void *)c.email); return;
        }
        cJSON_Delete(rows);
    }
    snprintf(from, sizeof(from), "%lld", (long long)c.uid);
    {
        struct tm *tm;
        time_t t = time(NULL);
        tm = localtime(&t);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    }
    p[0] = from; p[1] = js(c.body, "to_id", "0"); p[2] = js(c.body, "subject", "");
    p[3] = body; p[4] = ts;
    db_exec_params("INSERT INTO messages(from_id,to_id,subject,body,created_at)"
                   " VALUES(CAST(?1 AS INTEGER),CAST(?2 AS INTEGER),?,?,?)", 5, p);
    ok_true(rq);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- CANTINE / TRANSPORT / PHOTOS ------------------------- */
static void route_menu(request_t *rq)
{
    ctx_t c;
    cJSON *o;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "menu", db_query("SELECT * FROM menu ORDER BY day", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_bus(request_t *rq)
{
    ctx_t c;
    cJSON *o;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "bus", db_query("SELECT * FROM bus ORDER BY route", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_photos(request_t *rq)
{
    ctx_t c;
    cJSON *o;
    if (!load_ctx(rq, &c, NULL)) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "photos", db_query("SELECT * FROM photos ORDER BY created_at DESC", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- STATS / AUDIT (admin) ------------------------- */
static void route_stats(request_t *rq)
{
    ctx_t c;
    cJSON *o;
    if (!load_ctx(rq, &c, "admin,moderator")) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddNumberToObject(o, "parents", db_count("SELECT COUNT(*) FROM users WHERE role='parent'", 0, NULL));
    cJSON_AddNumberToObject(o, "moderators", db_count("SELECT COUNT(*) FROM users WHERE role='moderator'", 0, NULL));
    cJSON_AddNumberToObject(o, "students", db_count("SELECT COUNT(*) FROM students", 0, NULL));
    cJSON_AddNumberToObject(o, "announcements", db_count("SELECT COUNT(*) FROM announcements", 0, NULL));
    cJSON_AddNumberToObject(o, "unpaid", db_count("SELECT COUNT(*) FROM fees WHERE paid=0", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

static void route_audit(request_t *rq)
{
    ctx_t c;
    cJSON *o;
    if (!load_ctx(rq, &c, "admin")) return;
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddItemToObject(o, "events", db_query("SELECT * FROM audit ORDER BY id DESC LIMIT 200", 0, NULL));
    http_json(rq, 200, o);
    cJSON_Delete(o);
    free_ctx(&c); free((void *)c.role); free((void *)c.email);
}

/* ------------------------- aiguillage ------------------------- */
void api_dispatch(request_t *rq)
{
    const char *p = rq->path + 4;   /* apres /api */
    const char *m = rq->method;

    session_prune();

    if (!strcmp(p, "/config") && !strcmp(m, "GET")) return route_config(rq);
    if (!strcmp(p, "/login") && !strcmp(m, "POST")) return route_login(rq);
    if (!strcmp(p, "/logout") && !strcmp(m, "POST")) return route_logout(rq);
    if (!strcmp(p, "/me") && !strcmp(m, "GET")) return route_me(rq);
    if (!strcmp(p, "/password") && !strcmp(m, "POST")) return route_change_password(rq);

    if (!strcmp(p, "/users") && !strcmp(m, "GET")) return route_users_list(rq);
    if (!strcmp(p, "/users") && !strcmp(m, "POST")) return route_users_create(rq);
    if (!strcmp(p, "/users/update") && !strcmp(m, "POST")) return route_users_update(rq);
    if (!strcmp(p, "/users/delete") && !strcmp(m, "POST")) return route_users_delete(rq);

    if (!strcmp(p, "/students") && !strcmp(m, "GET")) return route_students(rq);
    if (!strcmp(p, "/students") && !strcmp(m, "POST")) return route_students_create(rq);

    if (!strcmp(p, "/announcements") && !strcmp(m, "GET")) return route_announcements(rq);
    if (!strcmp(p, "/announcements/save") && !strcmp(m, "POST")) return route_announcements_save(rq);
    if (!strcmp(p, "/announcements/delete") && !strcmp(m, "POST")) return route_announcement_delete(rq);

    if (!strcmp(p, "/schedule") && !strcmp(m, "GET")) return route_schedule(rq);
    if (!strcmp(p, "/homework") && !strcmp(m, "GET")) return route_homework(rq);
    if (!strcmp(p, "/homework") && !strcmp(m, "POST")) return route_homework_create(rq);

    if (!strcmp(p, "/grades") && (!strcmp(m, "GET") || !strcmp(m, "POST"))) return route_grades(rq);
    if (!strcmp(p, "/absences") && !strcmp(m, "GET")) return route_absences(rq);
    if (!strcmp(p, "/absences") && !strcmp(m, "POST")) return route_absence_create(rq);

    if (!strcmp(p, "/fees") && !strcmp(m, "GET")) return route_fees(rq);
    if (!strcmp(p, "/fees/toggle") && !strcmp(m, "POST")) return route_fee_toggle(rq);

    if (!strcmp(p, "/messages") && !strcmp(m, "GET")) return route_messages(rq);
    if (!strcmp(p, "/messages") && !strcmp(m, "POST")) return route_message_send(rq);
    if (!strcmp(p, "/messages/read") && !strcmp(m, "POST")) return route_messages_read(rq);

    if (!strcmp(p, "/menu") && !strcmp(m, "GET")) return route_menu(rq);
    if (!strcmp(p, "/bus") && !strcmp(m, "GET")) return route_bus(rq);
    if (!strcmp(p, "/photos") && !strcmp(m, "GET")) return route_photos(rq);

    if (!strcmp(p, "/stats") && !strcmp(m, "GET")) return route_stats(rq);
    if (!strcmp(p, "/audit") && !strcmp(m, "GET")) return route_audit(rq);

    http_error(rq, 404, "unknown_route");
}
