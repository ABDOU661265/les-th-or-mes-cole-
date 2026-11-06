/* db.c — base SQLite locale, schema et donnees de demonstration */
#include "app.h"

sqlite3 *g_db = NULL;
char     g_datadir[1024] = ".";

static const char *SCHEMA =
"PRAGMA journal_mode=WAL;"
"PRAGMA foreign_keys=ON;"
"CREATE TABLE IF NOT EXISTS users("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  email TEXT NOT NULL UNIQUE,"
"  pass TEXT NOT NULL,"
"  role TEXT NOT NULL,"
"  first_name TEXT NOT NULL DEFAULT '',"
"  last_name TEXT NOT NULL DEFAULT '',"
"  phone TEXT NOT NULL DEFAULT '',"
"  active INTEGER NOT NULL DEFAULT 1,"
"  must_change INTEGER NOT NULL DEFAULT 0,"
"  created_at TEXT NOT NULL DEFAULT '',"
"  created_by INTEGER,"
"  last_login TEXT NOT NULL DEFAULT '',"
"  fails INTEGER NOT NULL DEFAULT 0,"
"  locked_until INTEGER NOT NULL DEFAULT 0"
");"
"CREATE TABLE IF NOT EXISTS students("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  parent_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
"  first_name TEXT NOT NULL,"
"  last_name TEXT NOT NULL,"
"  birth TEXT NOT NULL DEFAULT '',"
"  level TEXT NOT NULL DEFAULT '',"
"  class_name TEXT NOT NULL DEFAULT '',"
"  bus_route TEXT NOT NULL DEFAULT '',"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS announcements("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  title_fr TEXT NOT NULL, title_ar TEXT DEFAULT '', title_en TEXT DEFAULT '',"
"  body_fr TEXT DEFAULT '', body_ar TEXT DEFAULT '', body_en TEXT DEFAULT '',"
"  category TEXT NOT NULL DEFAULT 'info',"
"  audience TEXT NOT NULL DEFAULT 'all',"
"  pinned INTEGER NOT NULL DEFAULT 0,"
"  author_id INTEGER,"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS schedule("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  class_name TEXT NOT NULL,"
"  day INTEGER NOT NULL,"
"  slot INTEGER NOT NULL,"
"  subject TEXT NOT NULL,"
"  teacher TEXT DEFAULT '',"
"  room TEXT DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS homework("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  class_name TEXT NOT NULL,"
"  subject TEXT NOT NULL,"
"  title_fr TEXT NOT NULL, title_ar TEXT DEFAULT '', title_en TEXT DEFAULT '',"
"  body_fr TEXT DEFAULT '', body_ar TEXT DEFAULT '', body_en TEXT DEFAULT '',"
"  due TEXT NOT NULL DEFAULT '',"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS absences("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE CASCADE,"
"  date TEXT NOT NULL,"
"  kind TEXT NOT NULL DEFAULT 'absence',"
"  reason TEXT DEFAULT '',"
"  justified INTEGER NOT NULL DEFAULT 0,"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS grades("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE CASCADE,"
"  subject TEXT NOT NULL,"
"  evaluation TEXT DEFAULT '',"
"  note REAL NOT NULL DEFAULT 0,"
"  max REAL NOT NULL DEFAULT 20,"
"  coef REAL NOT NULL DEFAULT 1,"
"  term TEXT NOT NULL DEFAULT 'T1',"
"  date TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS fees("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  student_id INTEGER NOT NULL REFERENCES students(id) ON DELETE CASCADE,"
"  label_fr TEXT NOT NULL DEFAULT '', label_ar TEXT DEFAULT '', label_en TEXT DEFAULT '',"
"  amount INTEGER NOT NULL DEFAULT 0,"
"  due_date TEXT NOT NULL DEFAULT '',"
"  paid INTEGER NOT NULL DEFAULT 0,"
"  paid_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS messages("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  from_id INTEGER NOT NULL,"
"  to_id INTEGER NOT NULL,"
"  student_id INTEGER,"
"  subject TEXT NOT NULL DEFAULT '',"
"  body TEXT NOT NULL DEFAULT '',"
"  created_at TEXT NOT NULL DEFAULT '',"
"  is_read INTEGER NOT NULL DEFAULT 0"
");"
"CREATE TABLE IF NOT EXISTS photos("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  title TEXT NOT NULL DEFAULT '',"
"  icon TEXT NOT NULL DEFAULT 'photo',"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS menu("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  day INTEGER NOT NULL,"
"  dish_fr TEXT NOT NULL DEFAULT '', dish_ar TEXT DEFAULT '', dish_en TEXT DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS bus("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  route TEXT NOT NULL DEFAULT '',"
"  driver TEXT NOT NULL DEFAULT '',"
"  phone TEXT NOT NULL DEFAULT '',"
"  morning TEXT NOT NULL DEFAULT '',"
"  evening TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS audit("
"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
"  user_id INTEGER,"
"  email TEXT DEFAULT '',"
"  action TEXT NOT NULL,"
"  detail TEXT DEFAULT '',"
"  created_at TEXT NOT NULL DEFAULT ''"
");"
"CREATE TABLE IF NOT EXISTS totals("
"  k TEXT PRIMARY KEY, v TEXT"
");"
"CREATE INDEX IF NOT EXISTS ix_students_parent ON students(parent_id);"
"CREATE INDEX IF NOT EXISTS ix_abs_student ON absences(student_id);"
"CREATE INDEX IF NOT EXISTS ix_grades_student ON grades(student_id);"
"CREATE INDEX IF NOT EXISTS ix_msg_to ON messages(to_id);"
"CREATE INDEX IF NOT EXISTS ix_audit_date ON audit(created_at);"
;

int db_exec(const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(g_db, sql, NULL, NULL, &err) != SQLITE_OK) {
        log_write("[db] erreur: %s", err ? err : "?");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

int db_open(const char *dir)
{
    char path[1200];
    sqlite3_stmt *st = NULL;
    safe_copy(g_datadir, sizeof(g_datadir), dir);
    snprintf(path, sizeof(path), "%s/school.db", dir);
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        log_write("[db] impossible d'ouvrir %s", path);
        return -1;
    }
    sqlite3_busy_timeout(g_db, 4000);
    /* NORMAL : bien plus rapide que FULL pour une seule ecriture a la fois,
       et reste sans corruption (mode WAL). */
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    /* tables deja en memoire : les requetes repetees ne relisent pas le disque */
    sqlite3_exec(g_db, "PRAGMA cache_size=-8000;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA temp_store=MEMORY;", NULL, NULL, NULL);
    /* les cles etrangeres ne sont pas actives par defaut */
    if (sqlite3_prepare_v2(g_db, "PRAGMA foreign_keys=ON;", -1, &st, NULL) == SQLITE_OK) sqlite3_step(st);
    sqlite3_finalize(st);
    if (db_exec(SCHEMA) != 0) return -1;
    log_write("[db] base ouverte : %s", path);
    return 0;
}

void db_close(void) { if (g_db) { sqlite3_close(g_db); g_db = NULL; } }

static void bind_params(sqlite3_stmt *st, int nparams, const char **params)
{
    int i;
    for (i = 0; i < nparams; i++) {
        if (params[i]) sqlite3_bind_text(st, i + 1, params[i], -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(st, i + 1);
    }
}

cJSON *db_query(const char *sql, int nparams, const char **params)
{
    sqlite3_stmt *st = NULL;
    cJSON *rows = cJSON_CreateArray();
    if (!rows) return NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) {
        log_write("[db] prepare: %s (%s)", sqlite3_errmsg(g_db), sql);
        return rows;
    }
    bind_params(st, nparams, params);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int n = sqlite3_column_count(st), c;
        cJSON *row = cJSON_CreateObject();
        for (c = 0; c < n; c++) {
            const char *name = sqlite3_column_name(st, c);
            int t = sqlite3_column_type(st, c);
            if (t == SQLITE_INTEGER) cJSON_AddNumberToObject(row, name, (double)sqlite3_column_int64(st, c));
            else if (t == SQLITE_FLOAT) cJSON_AddNumberToObject(row, name, sqlite3_column_double(st, c));
            else if (t == SQLITE_NULL) cJSON_AddNullToObject(row, name);
            else {
                const unsigned char *v = sqlite3_column_text(st, c);
                cJSON_AddStringToObject(row, name, v ? (const char *)v : "");
            }
        }
        cJSON_AddItemToArray(rows, row);
    }
    sqlite3_finalize(st);
    return rows;
}

int db_count(const char *sql, int nparams, const char **params)
{
    sqlite3_stmt *st = NULL;
    int v = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    bind_params(st, nparams, params);
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

sqlite3_int64 db_exec_params(const char *sql, int nparams, const char **params)
{
    sqlite3_stmt *st = NULL;
    sqlite3_int64 id = -1;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) {
        log_write("[db] prepare write: %s (%s)", sqlite3_errmsg(g_db), sql);
        return -1;
    }
    bind_params(st, nparams, params);
    if (sqlite3_step(st) == SQLITE_DONE) id = sqlite3_last_insert_rowid(g_db);
    else log_write("[db] write: %s", sqlite3_errmsg(g_db));
    sqlite3_finalize(st);
    return id;
}

/* ---------------- donnees de demonstration ---------------- */
static void seed_user(const char *email, const char *pw, const char *role,
                      const char *fn, const char *ln, int must_change, sqlite3_int64 *out)
{
    const char *p[7];
    char hash[256], today[32];
    password_hash(pw, hash, sizeof(hash));
    date_str(today, sizeof(today), time(NULL));
    p[0] = email; p[1] = hash; p[2] = role; p[3] = fn; p[4] = ln; p[5] = today;
    p[6] = must_change ? "1" : "0";
    *out = db_exec_params("INSERT INTO users(email,pass,role,first_name,last_name,created_at,must_change)"
                          " VALUES(?,?,?,?,?,?,?)", 7, p);
}

void db_seed(void)
{
    sqlite3_int64 admin_id, mod_id, parent_id, stu1, stu2;
    const char *p[16];
    char today[32], d[32];
    time_t t = time(NULL);

    if (db_count("SELECT COUNT(*) FROM users", 0, NULL) > 0) return;
    log_write("[db] premiere initialisation : creation des comptes et donnees de demo");

    date_str(today, sizeof(today), t);
    seed_user("admin@lestheoremes.dz", "Admin@2026", "admin", "Directrice", "Mme Amel", 0, &admin_id);
    seed_user("moderateur@lestheoremes.dz", "Modo@2026", "moderator", "Nadia", "B.", 1, &mod_id);
    seed_user("parent@lestheoremes.dz", "Parent@2026", "parent", "Mohamed Adel", "Dahmani", 0, &parent_id);

    /* eleves (rattaches au compte parent de demonstration) */
    {
        static char pb[24];
        snprintf(pb, sizeof(pb), "%lld", (long long)parent_id);
        p[0] = pb;
        p[1] = "Yacine";
        p[2] = "Dahmani"; p[3] = "2015-04-12"; p[4] = "Primaire"; p[5] = "3AP";
        p[6] = "Ligne A - Génie Zéralda"; p[7] = today;
    }
    stu1 = db_exec_params("INSERT INTO students(parent_id,first_name,last_name,birth,level,class_name,bus_route,created_at)"
                          " VALUES(?,?,?,?,?,?,?,?)", 8, p);
    p[1] = "Lina"; p[3] = "2012-09-03"; p[4] = "Collège"; p[5] = "1AM"; p[6] = "Ligne B - Douéra";
    stu2 = db_exec_params("INSERT INTO students(parent_id,first_name,last_name,birth,level,class_name,bus_route,created_at)"
                          " VALUES(?,?,?,?,?,?,?,?)", 8, p);

    /* annonces */
    {
        char aid[24], mid[24], x = 0;
        snprintf(aid, sizeof(aid), "%lld", (long long)admin_id);
        snprintf(mid, sizeof(mid), "%lld", (long long)mod_id);
        (void)x;
        const char *a[12];
        a[0] = "Réunion parents d'élèves";
        a[1] = "اجتماع أولياء التلاميذ";
        a[2] = "Parents meeting";
        a[3] = "Une réunion d'information aura lieu samedi à 10h00 dans la salle polyvalente. "
               "Ordre du jour : résultats du premier trimestre, cantine et transport scolaire.";
        a[4] = "سيعقد لقاء إعلامي يوم السبت على الساعة 10:00 في القاعة المتعددة الاستعمالات. "
               "جدول الأعمال: نتائج الفصل الأول، الإطعام والنقل المدرسي.";
        a[5] = "An information meeting will be held on Saturday at 10:00 in the main hall. "
               "Agenda: first term results, canteen and school transport.";
        a[6] = "event"; a[7] = "1"; a[8] = aid; a[9] = "2026-11-04";
        db_exec_params("INSERT INTO announcements(title_fr,title_ar,title_en,body_fr,body_ar,body_en,category,pinned,author_id,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, a);
        a[0] = "Inscription 2026 / 2027 ouverte";
        a[1] = "التسجيل 2026 / 2027 مفتوح";
        a[2] = "Enrolment 2026 / 2027 open";
        a[3] = "Les dossiers d'inscription sont disponibles à l'administration du dimanche au jeudi, de 9h à 15h.";
        a[4] = "ملفات التسجيل متوفرة بالإدارة من الأحد إلى الخميس من 9:00 إلى 15:00.";
        a[5] = "Enrolment files are available at the office from Sunday to Thursday, 9:00 to 15:00.";
        a[6] = "admin"; a[7] = "0"; a[8] = mid; a[9] = "2026-10-28";
        db_exec_params("INSERT INTO announcements(title_fr,title_ar,title_en,body_fr,body_ar,body_en,category,pinned,author_id,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, a);
        a[0] = "Sortie pédagogique au Jardin d'Essai";
        a[1] = "خرجة بيداغوجية إلى حديقة التجارب";
        a[2] = "School trip to the Botanical Garden";
        a[3] = "Sortie prévue mardi pour les classes de 3AP et 4AP. Départ 8h30, retour 15h30. Pique-nique à prévoir.";
        a[4] = "رحلة مبرمجة يوم الثلاثاء لأقسام السنة الثالثة والرابعة ابتدائي. الانطلاق 8:30 والعودة 15:30.";
        a[5] = "Trip planned on Tuesday for 3AP and 4AP classes. Departure 8:30, return 15:30.";
        a[6] = "event"; a[7] = "0"; a[8] = aid; a[9] = "2026-10-20";
        db_exec_params("INSERT INTO announcements(title_fr,title_ar,title_en,body_fr,body_ar,body_en,category,pinned,author_id,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, a);
    }

    /* emploi du temps 3AP */
    {
        int day, slot;
        const char *subs[6][7] = {
            /* dim */ { "Arabe", "Français", "Mathématiques", "Sciences", "Anglais", "Sport", "Soroban" },
            /* lun */ { "Français", "Mathématiques", "Arabe", "Éducation islamique", "Arts", "Informatique", "" },
            /* mar */ { "Mathématiques", "Français", "Sciences", "Anglais", "Arabe", "Musique", "" },
            /* mer */ { "Arabe", "Mathématiques", "Français", "Histoire & Géo", "Sport", "", "" },
            /* jeu */ { "Français", "Sciences", "Anglais", "Éducation civique", "Soroban", "", "" }
        };
        for (day = 0; day < 5; day++)
            for (slot = 0; slot < 7; slot++) {
                char ds[8], ss[8];
                const char *q[6];
                if (!subs[day][slot][0]) continue;
                snprintf(ds, sizeof(ds), "%d", day);
                snprintf(ss, sizeof(ss), "%d", slot);
                q[0] = "3AP"; q[1] = ds; q[2] = ss; q[3] = subs[day][slot]; q[4] = "Mme Salima"; q[5] = "Salle 12";
                db_exec_params("INSERT INTO schedule(class_name,day,slot,subject,teacher,room) VALUES(?,?,?,?,?,?)", 6, q);
            }
        for (day = 0; day < 5; day++)
            for (slot = 0; slot < 6; slot++) {
                static const char *subs2[5][6] = {
                    { "Mathématiques", "Français", "Arabe", "Physique", "Anglais", "Sport" },
                    { "Français", "Mathématiques", "Sciences", "Arabe", "Anglais", "Informatique" },
                    { "Arabe", "Physique", "Mathématiques", "Français", "Histoire & Géo", "Arts" },
                    { "Mathématiques", "Français", "Anglais", "Sciences", "Éducation islamique", "" },
                    { "Français", "Arabe", "Mathématiques", "Physique", "Anglais", "" }
                };
                char ds[8], ss[8];
                const char *q[6];
                if (!subs2[day][slot][0]) continue;
                snprintf(ds, sizeof(ds), "%d", day);
                snprintf(ss, sizeof(ss), "%d", slot);
                q[0] = "1AM"; q[1] = ds; q[2] = ss; q[3] = subs2[day][slot]; q[4] = "M. Karim"; q[5] = "Salle 7";
                db_exec_params("INSERT INTO schedule(class_name,day,slot,subject,teacher,room) VALUES(?,?,?,?,?,?)", 6, q);
            }
    }

    /* devoirs */
    {
        const char *h[12];
        char cls[8], stu[8];
        snprintf(cls, sizeof(cls), "3AP"); snprintf(stu, sizeof(stu), "%lld", (long long)stu1);
        (void)stu;
        h[0] = cls; h[1] = "Mathématiques";
        h[2] = "Exercices 4 et 5 page 38";
        h[3] = "التمارين 4 و 5 صفحة 38";
        h[4] = "Exercises 4 and 5 page 38";
        h[5] = "Réviser les tables de multiplication de 2 et 3.";
        h[6] = "مراجعة جدول الضرب 2 و 3.";
        h[7] = "Review the 2 and 3 times tables.";
        h[8] = "2026-11-08"; h[9] = "2026-11-06";
        db_exec_params("INSERT INTO homework(class_name,subject,title_fr,title_ar,title_en,body_fr,body_ar,body_en,due,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, h);
        h[1] = "Français";
        h[2] = "Lire le texte « Le petit jardin »";
        h[3] = "قراءة نص «الحديقة الصغيرة»";
        h[4] = "Read the text “The little garden”";
        h[5] = "Lire à voix haute deux fois, puis recopier les mots difficiles.";
        h[6] = "القراءة بصوت عال مرتين ثم كتابة الكلمات الصعبة.";
        h[7] = "Read aloud twice, then copy the difficult words.";
        h[8] = "2026-11-09";
        db_exec_params("INSERT INTO homework(class_name,subject,title_fr,title_ar,title_en,body_fr,body_ar,body_en,due,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, h);
        h[0] = "1AM"; h[1] = "Sciences";
        h[2] = "Compte rendu d'expérience";
        h[3] = "تقرير تجربة";
        h[4] = "Experiment report";
        h[5] = "Rédiger le compte rendu de l'expérience sur la conductivité (1 page).";
        h[6] = "كتابة تقرير تجربة التوصيل الكهربائي (صفحة واحدة).";
        h[7] = "Write the report of the conductivity experiment (1 page).";
        h[8] = "2026-11-10";
        db_exec_params("INSERT INTO homework(class_name,subject,title_fr,title_ar,title_en,body_fr,body_ar,body_en,due,created_at)"
                       " VALUES(?,?,?,?,?,?,?,?,?,?)", 10, h);
    }

    /* absences */
    {
        const char *a[6];
        char s1[8], s2[8];
        snprintf(s1, sizeof(s1), "%lld", (long long)stu1);
        snprintf(s2, sizeof(s2), "%lld", (long long)stu2);
        a[0] = s1; a[1] = "2026-10-14"; a[2] = "absence"; a[3] = "Maladie (certificat fourni)"; a[4] = "1"; a[5] = today;
        db_exec_params("INSERT INTO absences(student_id,date,kind,reason,justified,created_at) VALUES(?,?,?,?,?,?)", 6, a);
        a[0] = s1; a[1] = "2026-10-27"; a[2] = "retard"; a[3] = "Transport"; a[4] = "0";
        db_exec_params("INSERT INTO absences(student_id,date,kind,reason,justified,created_at) VALUES(?,?,?,?,?,?)", 6, a);
        a[0] = s2; a[1] = "2026-10-09"; a[2] = "absence"; a[3] = "Rendez-vous médical"; a[4] = "1";
        db_exec_params("INSERT INTO absences(student_id,date,kind,reason,justified,created_at) VALUES(?,?,?,?,?,?)", 6, a);
    }

    /* notes */
    {
        const char *g[7];
        char s1[8], s2[8];
        static const char *subs1[] = { "Mathématiques", "Français", "Arabe", "Anglais", "Sciences" };
        static const char *evals[] = { "Devoir 1", "Devoir 2", "Composition" };
        static const double notes1[5][3] = { { 16, 17.5, 18 }, { 14, 15, 16.5 }, { 15, 16, 17 }, { 17, 18, 18.5 }, { 13, 15.5, 16 } };
        static const char *subs2[] = { "Mathématiques", "Français", "Physique", "Anglais", "Sciences" };
        static const double notes2[5][3] = { { 12, 13.5, 14 }, { 15, 16, 15.5 }, { 11, 13, 12.5 }, { 16, 17, 17.5 }, { 14, 15, 15.5 } };
        int i, j;
        char buf[16];
        snprintf(s1, sizeof(s1), "%lld", (long long)stu1);
        snprintf(s2, sizeof(s2), "%lld", (long long)stu2);
        for (i = 0; i < 5; i++)
            for (j = 0; j < 3; j++) {
                snprintf(buf, sizeof(buf), "%.2f", notes1[i][j]);
                g[0] = s1; g[1] = subs1[i]; g[2] = evals[j]; g[3] = buf; g[4] = "20"; g[5] = "T1"; g[6] = "2026-10-15";
                db_exec_params("INSERT INTO grades(student_id,subject,evaluation,note,max,term,date) VALUES(?,?,?,CAST(? AS REAL),CAST(? AS REAL),?,?)", 7, g);
                snprintf(buf, sizeof(buf), "%.2f", notes2[i][j]);
                g[0] = s2; g[1] = subs2[i];
                g[6] = "2026-10-18";
                db_exec_params("INSERT INTO grades(student_id,subject,evaluation,note,max,term,date) VALUES(?,?,?,CAST(? AS REAL),CAST(? AS REAL),?,?)", 7, g);
            }
    }

    /* frais de scolarite */
    {
        const char *f[8];
        char s1[8], s2[8];
        snprintf(s1, sizeof(s1), "%lld", (long long)stu1);
        snprintf(s2, sizeof(s2), "%lld", (long long)stu2);
        f[0] = s1;
        f[1] = "Scolarité — 1er trimestre"; f[2] = "التمدرس — الفصل الأول"; f[3] = "Tuition — 1st term";
        f[4] = "45000"; f[5] = "2026-10-05"; f[6] = "1";
        db_exec_params("INSERT INTO fees(student_id,label_fr,label_ar,label_en,amount,due_date,paid) VALUES(?,?,?,?,CAST(? AS INTEGER),?,?)", 7, f);
        f[1] = "Cantine — Novembre"; f[2] = "الإطعام — نوفمبر"; f[3] = "Canteen — November";
        f[4] = "6000"; f[5] = "2026-11-15"; f[6] = "0";
        db_exec_params("INSERT INTO fees(student_id,label_fr,label_ar,label_en,amount,due_date,paid) VALUES(?,?,?,?,CAST(? AS INTEGER),?,?)", 7, f);
        f[0] = s2;
        f[1] = "Scolarité — 1er trimestre"; f[2] = "التمدرس — الفصل الأول"; f[3] = "Tuition — 1st term";
        f[4] = "52000"; f[5] = "2026-10-05"; f[6] = "0";
        db_exec_params("INSERT INTO fees(student_id,label_fr,label_ar,label_en,amount,due_date,paid) VALUES(?,?,?,?,CAST(? AS INTEGER),?,?)", 7, f);
        f[1] = "Transport — Novembre"; f[2] = "النقل — نوفمبر"; f[3] = "Transport — November";
        f[4] = "4000"; f[5] = "2026-11-15";
        db_exec_params("INSERT INTO fees(student_id,label_fr,label_ar,label_en,amount,due_date,paid) VALUES(?,?,?,?,CAST(? AS INTEGER),?,?)", 7, f);
    }

    /* messages */
    {
        const char *m[6];
        char pa[8], ad[8];
        snprintf(pa, sizeof(pa), "%lld", (long long)parent_id);
        snprintf(ad, sizeof(ad), "%lld", (long long)mod_id);
        m[0] = ad; m[1] = pa;
        m[2] = "Carnet de correspondance";
        m[3] = "Bonjour, le carnet de Yacine doit être signé pour lundi. Merci de vérifier les absences du 27 octobre.";
        m[4] = "2026-11-05 09:12";
        db_exec_params("INSERT INTO messages(from_id,to_id,subject,body,created_at) VALUES(?,?,?,?,?)", 5, m);
        m[0] = pa; m[1] = ad; m[2] = "Réponse : carnet";
        m[3] = "Bonjour, le carnet sera signé ce soir. Le retard du 27 octobre était dû au transport scolaire. Merci.";
        m[4] = "2026-11-05 18:40";
        db_exec_params("INSERT INTO messages(from_id,to_id,subject,body,created_at) VALUES(?,?,?,?,?)", 5, m);
    }

    /* galerie */
    {
        const char *ph[3];
        ph[0] = "Rentrée scolaire"; ph[1] = "school"; ph[2] = "2026-09-15";
        db_exec_params("INSERT INTO photos(title,icon,created_at) VALUES(?,?,?)", 3, ph);
        ph[0] = "Atelier Soroban"; ph[1] = "math";
        db_exec_params("INSERT INTO photos(title,icon,created_at) VALUES(?,?,?)", 3, ph);
        ph[0] = "Sortie pédagogique"; ph[1] = "bus";
        db_exec_params("INSERT INTO photos(title,icon,created_at) VALUES(?,?,?)", 3, ph);
        ph[0] = "Fête de fin d'année"; ph[1] = "star";
        db_exec_params("INSERT INTO photos(title,icon,created_at) VALUES(?,?,?)", 3, ph);
    }

    /* cantine */
    {
        static const char *dish_fr[] = { "Couscous poulet", "Chorba + gratin", "Riz à la viande", "Lentilles + salade", "Poisson + purée" };
        static const char *dish_ar[] = { "كسكس بالدجاج", "شربة + غراتان", "أرز باللحم", "عدس + سلطة", "سمك + بطاطا مهروسة" };
        static const char *dish_en[] = { "Chicken couscous", "Soup & gratin", "Rice with meat", "Lentils & salad", "Fish & mash" };
        int i;
        for (i = 0; i < 5; i++) {
            char ds[8];
            const char *q[4];
            snprintf(ds, sizeof(ds), "%d", i);
            q[0] = ds; q[1] = dish_fr[i]; q[2] = dish_ar[i]; q[3] = dish_en[i];
            db_exec_params("INSERT INTO menu(day,dish_fr,dish_ar,dish_en) VALUES(?,?,?,?)", 4, q);
        }
    }

    /* transport */
    {
        const char *b[6];
        b[0] = "Ligne A - Génie Zéralda"; b[1] = "M. Ali"; b[2] = "+213 665 90 62 57"; b[3] = "07:10"; b[4] = "16:40"; b[5] = "";
        db_exec_params("INSERT INTO bus(route,driver,phone,morning,evening) VALUES(?,?,?,?,?)", 5, b);
        b[0] = "Ligne B - Douéra"; b[1] = "M. Rachid"; b[2] = "+213 781 96 30 81"; b[3] = "06:55"; b[4] = "16:45";
        db_exec_params("INSERT INTO bus(route,driver,phone,morning,evening) VALUES(?,?,?,?,?)", 5, b);
    }

    snprintf(d, sizeof(d), "%lld", (long long)admin_id);
    (void)d;
    log_write("[db] comptes de demo : admin@lestheoremes.dz / Admin@2026");
}
