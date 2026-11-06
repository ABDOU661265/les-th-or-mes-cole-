/* main.c — point d'entree : base de donnees, serveur local, fenetre de l'application
 *
 * Mode normal (Windows) : icone dans la zone de notification, l'application
 * s'ouvre dans une fenetre dediee (Edge/Chrome en mode application) ; a defaut
 * dans le navigateur par defaut.
 * Mode --console : le serveur reste au premier plan (utile pour diagnostiquer).
 */
#include "app.h"

#ifndef _WIN32
#  include <pthread.h>
#endif

static sock_t g_listen = (sock_t)-1;
int  g_port = 8765;
static int g_bind_all = 0;      /* 1 : ecoute toutes les interfaces (developpement) */
static char g_url[160];

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shellapi.h>
#  include <shlobj.h>
#endif

/* ------------------------------------------------------------------ */
/* serveur                                                            */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
static DWORD WINAPI client_thread(LPVOID param)
{
    http_handle((sock_t)(uintptr_t)param);
    return 0;
}
#else
static void *client_thread(void *param)
{
    http_handle((sock_t)(intptr_t)param);
    return NULL;
}
#endif

static int start_server(int port)
{
    struct sockaddr_in addr;
    int one = 1;
    g_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen == (sock_t)-1) return -1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = g_bind_all ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    if (bind(g_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0) { CLOSESOCK(g_listen); g_listen = (sock_t)-1; return -1; }
    if (listen(g_listen, 64) != 0) { CLOSESOCK(g_listen); g_listen = (sock_t)-1; return -1; }
    return 0;
}

static int bind_server(void)
{
    int i, base = g_port > 0 ? g_port : 8765;
    for (i = 0; i < 40; i++) {
        if (start_server(base + i) == 0) { g_port = base + i; return 0; }
    }
    return -1;
}

static void serve_loop(void)
{
    for (;;) {
        struct sockaddr_in cli;
#ifdef _WIN32
        int clen = sizeof(cli);
#else
        socklen_t clen = sizeof(cli);
#endif
        sock_t c = accept(g_listen, (struct sockaddr *)&cli, &clen);
        if (c == (sock_t)-1) {
#ifdef _WIN32
            Sleep(20);
#else
            usleep(20000);
#endif
            continue;
        }
#ifdef _WIN32
        {
            HANDLE th = CreateThread(NULL, 0, client_thread, (LPVOID)(uintptr_t)c, 0, NULL);
            if (th) CloseHandle(th);
            else { http_handle(c); }
        }
#else
        {
            pthread_t th;
            if (pthread_create(&th, NULL, client_thread, (void *)(intptr_t)c) == 0) pthread_detach(th);
            else http_handle(c);
        }
#endif
    }
}

#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID unused)
{
    (void)unused;
    serve_loop();
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* ouverture du navigateur / de la fenetre applicative                */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
static int file_exists_a(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* cherche Edge puis Chrome (mode application = fenetre sans barre d'URL) */
static int find_browser(char *out, size_t cap)
{
    const char *cands[] = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
        NULL
    };
    char local[MAX_PATH];
    int i;
    for (i = 0; cands[i]; i++) {
        if (file_exists_a(cands[i])) { safe_copy(out, cap, cands[i]); return 1; }
    }
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local) == S_OK) {
        char tmp[MAX_PATH];
        snprintf(tmp, sizeof(tmp), "%s\\Microsoft\\Edge\\Application\\msedge.exe", local);
        if (file_exists_a(tmp)) { safe_copy(out, cap, tmp); return 1; }
        snprintf(tmp, sizeof(tmp), "%s\\Google\\Chrome\\Application\\chrome.exe", local);
        if (file_exists_a(tmp)) { safe_copy(out, cap, tmp); return 1; }
    }
    return 0;
}

static int shell_open(const char *url)
{
    wchar_t w[600];
    MultiByteToWideChar(CP_UTF8, 0, url, -1, w, 600);
    return (int)(uintptr_t)ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL) > 32;
}

static void local_app_dir(char *out, size_t cap)
{
    char base[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base) != S_OK) {
        safe_copy(out, cap, g_datadir);
        return;
    }
    snprintf(out, cap, "%s\\LesTheoremes", base);
    CreateDirectoryA(out, NULL);
}

/* la fenetre s'ouvre tout de suite : la page attend le serveur et se
   rafraichit seule (evite les 2 a 5 secondes de page blanche) */
static const char kWaitPage[] =
    "<!doctype html><html lang=fr><meta charset=utf-8>"
    "<script>"
    "function go(){"
    "  var x=new XMLHttpRequest();"
    "  x.open('GET',APP,true);"
    "  x.onreadystatechange=function(){if(x.readyState===4)location.replace(APP);};"
    "  try{x.send();}catch(e){}"
    "}"
    "window.onload=function(){go();setInterval(go,900);};"
    "</script>"
    "<title>Les Theoremes</title>"
    "<body style=\"margin:0;height:100vh;display:flex;flex-direction:column;"
    "align-items:center;justify-content:center;font-family:Segoe UI,sans-serif;"
    "background:#fff7f0;color:#5d4037\">"
    "<div style=\"width:34px;height:34px;border:4px solid #ffe0c2;border-top-color:#ef6c00;"
    "border-radius:50%;animation:s .9s linear infinite\"></div>"
    "<p style=\"margin:18px 0 0;font-size:15px\">Les Th&eacute;or&egrave;mes</p>"
    "<p style=\"margin:6px 0 0;font-size:12px;color:#a1887f\">Ouverture de l'espace scolaire...</p>"
    "<style>@keyframes s{to{transform:rotate(360deg)}}</style>";

static int write_file_all(const char *path, const char *data, int len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}

static int open_app_window(void)
{
    char exe[MAX_PATH];
    char args[1200];
    char profile[1024];
    char page[1100];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    const char *url = g_url;

    local_app_dir(profile, sizeof(profile));
    /* petite page d'attente locale, reutilisee d'un lancement a l'autre */
    snprintf(page, sizeof(page), "%s\\demarrage.html", profile);
    {
        char full[2400];
        int n = snprintf(full, sizeof(full), "<script>var APP=%c%s%c;</script>", 34, g_url, 34);
        n += snprintf(full + n, sizeof(full) - (size_t)n, "%s", kWaitPage);
        if (!write_file_all(page, full, n)) page[0] = 0;
    }
    if (page[0]) {
        static char fileurl[1200];
        snprintf(fileurl, sizeof(fileurl), "file:///%s", page);
        {
            char *c;
            for (c = fileurl; *c; c++) if (*c == '\\') *c = '/';
        }
        url = fileurl;
    }

    if (!find_browser(exe, sizeof(exe))) return shell_open(url);

    snprintf(args, sizeof(args),
             "\"%s\" --app=%s --window-size=470,880 --window-position=120,40 "
             "--user-data-dir=\"%s\\ui\" --no-first-run --no-default-browser-check "
             "--disable-features=Translate,ChromeWhatsNewUI",
             exe, url, profile);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return shell_open(url);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
#else /* ------- Linux / macOS : navigateur par defaut (developpement) ------- */
static int open_app_window(void)
{
    char cmd[600];
#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "open '%s' >/dev/null 2>&1 &", g_url);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", g_url);
#endif
    return system(cmd) == 0;
}
#endif

/* ------------------------------------------------------------------ */
/* icone de notification + menu (Windows uniquement)                  */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#define WM_TRAY (WM_APP + 1)
#define IDM_OPEN 40001
#define IDM_FOLDER 40002
#define IDM_QUIT 40003
#define IDM_ABOUT 40004

static HWND g_hwnd = NULL;
static HINSTANCE g_inst = NULL;
static NOTIFYICONDATAA g_nid;
static int g_have_tray = 0;
static HICON g_icon = NULL;

static void show_menu(void)
{
    POINT pt;
    HMENU menu;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, IDM_OPEN, "Ouvrir l'application");
    AppendMenuA(menu, MF_STRING, IDM_FOLDER, "Ouvrir le dossier des données");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_ABOUT, "A propos");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_QUIT, "Quitter");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
        case WM_TRAY:
            if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) show_menu();
            else if (l == WM_LBUTTONDBLCLK || l == WM_LBUTTONUP) open_app_window();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case IDM_OPEN: open_app_window(); return 0;
                case IDM_FOLDER: shell_open(g_datadir); return 0;
                case IDM_ABOUT:
                    MessageBoxA(h,
                        "Les Théorèmes — Espace scolaire  (version " APP_VERSION ")\n\n"
                        "Application locale : les données restent sur cet ordinateur.\n"
                        "Groupe Scolaire Les Théorèmes — Génie Zéralda, Alger\n"
                        "+213 (0) 665 906 257",
                        "A propos", MB_OK | MB_ICONINFORMATION);
                    return 0;
                case IDM_QUIT:
                    DestroyWindow(h);
                    return 0;
            }
            return 0;
        case WM_DESTROY:
            if (g_have_tray) { Shell_NotifyIconA(NIM_DELETE, &g_nid); g_have_tray = 0; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void tray_install(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = g_inst;
    wc.lpszClassName = "TheoremesTrayWnd";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, "TheoremesTrayWnd", "Les Theoremes", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, g_inst, NULL);
    if (!g_hwnd) return;

    g_icon = (HICON)LoadImageA(g_inst, MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!g_icon) g_icon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);

    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_icon;
    safe_copy(g_nid.szTip, sizeof(g_nid.szTip), "Les Theoremes - Espace scolaire");
    if (Shell_NotifyIconA(NIM_ADD, &g_nid)) g_have_tray = 1;
}

/* le serveur repond-il deja ? (instance deja lancee) */
static int already_running(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        sock_t s;
        struct sockaddr_in a;
        char req[128], buf[32];
        int r;
        int port = 8765 + i;
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == (sock_t)-1) return 0;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        {
            DWORD tv = 400;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
        }
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0) {
            snprintf(req, sizeof(req), "GET /api/config HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
            send(s, req, (int)strlen(req), 0);
            r = recv(s, buf, sizeof(buf) - 1, 0);
            CLOSESOCK(s);
            if (r > 12 && !strncmp(buf, "HTTP/1.1 200", 12)) { g_port = port; return 1; }
        } else {
            CLOSESOCK(s);
        }
    }
    return 0;
}
#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* demarrage commun                                                   */
/* ------------------------------------------------------------------ */
static void resolve_datadir(char *out, size_t cap)
{
#ifdef _WIN32
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n > 0) {
        char *slash = strrchr(exe, '\\');
        if (slash) *slash = 0;
        safe_copy(out, cap, exe);
        return;
    }
    GetCurrentDirectoryA((DWORD)cap, out);
#else
    if (!getcwd(out, cap)) safe_copy(out, cap, ".");
#endif
}

static int app_init(const char *datadir, int port)
{
    char dir[1024];
    safe_copy(dir, sizeof(dir), datadir);
    safe_copy(g_datadir, sizeof(g_datadir), dir);

#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    }
#endif

    if (db_open(dir) != 0) {
#ifdef _WIN32
        /* dossier non accessible en ecriture (ex. Program Files) : on bascule
           vers le profil de l'utilisateur */
        char local[MAX_PATH], sub[1024];
        if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local) == S_OK) {
            snprintf(sub, sizeof(sub), "%s\\LesTheoremes", local);
            CreateDirectoryA(sub, NULL);
            safe_copy(g_datadir, sizeof(g_datadir), sub);
            if (db_open(sub) == 0) { db_seed(); goto db_ready; }
        }
#endif
        log_write("[db] base de donnees inaccessible");
        return -1;
    }
    db_seed();
db_ready:

    if (port > 0) g_port = port;
    if (bind_server() != 0) return -1;
    snprintf(g_url, sizeof(g_url), "http://127.0.0.1:%d/", g_port);
    log_write("serveur : %s", g_url);
    return 0;
}

static void usage(void)
{
    printf("%s %s\n", APP_NAME, APP_VERSION);
    printf("usage: Les-Theoremes.exe [--port N] [--datadir DIR] [--console] [--no-window] [--bind-all]\n");
    printf("  --console    affiche la console (diagnostic)\n");
    printf("  --no-window  demarre le serveur sans ouvrir la fenetre\n");
    printf("  --datadir    dossier des donnees (base + fichiers)\n");
}

/* ------------------------------------------------------------------ */
/* entree                                                             */
/* ------------------------------------------------------------------ */
#ifdef _WIN32

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    char datadir[1024] = "";
    int i, console = 0, open_win = 1, port = 0;
    (void)prev; (void)show;
    g_inst = inst;

    for (i = 1; i < __argc; i++) {
        const char *a = __argv[i];
        if (!strcmp(a, "--datadir") && i + 1 < __argc) safe_copy(datadir, sizeof(datadir), __argv[++i]);
        else if (!strcmp(a, "--port") && i + 1 < __argc) port = atoi(__argv[++i]);
        else if (!strcmp(a, "--console")) console = 1;
        else if (!strcmp(a, "--no-window") || !strcmp(a, "--no-browser")) open_win = 0;
        else if (!strcmp(a, "--bind-all")) g_bind_all = 1;
        else if (!strcmp(a, "--help")) {
            AllocConsole();
            freopen("CONOUT$", "w", stdout);
            usage();
            return 0;
        }
    }
    (void)cmdline;

    if (console) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    /* une instance tourne deja ? on re-ouvre simplement la fenetre */
    if (!console && already_running()) {
        snprintf(g_url, sizeof(g_url), "http://127.0.0.1:%d/", g_port);
        safe_copy(g_datadir, sizeof(g_datadir), ".");
        if (open_win) open_app_window();
        return 0;
    }

    if (!datadir[0]) resolve_datadir(datadir, sizeof(datadir));

    log_write("%s %s", APP_NAME, APP_VERSION);
    if (app_init(datadir, port) != 0) {
        MessageBoxA(NULL, "Impossible de demarrer le serveur local (port occupe ou dossier non accessible).",
                    "Les Theoremes", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (console) {
        if (open_win) open_app_window();
        serve_loop();
        return 0;
    }

    tray_install();
    if (open_win) open_app_window();
    {
        HANDLE th = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    {
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    db_close();
    return 0;
}

#else

int main(int argc, char **argv)
{
    char datadir[1024] = "";
    int i, console = 0, open_win = 1, port = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--datadir") && i + 1 < argc) safe_copy(datadir, sizeof(datadir), argv[++i]);
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--console") || !strcmp(argv[i], "--serve")) console = 1;
        else if (!strcmp(argv[i], "--no-window") || !strcmp(argv[i], "--no-browser")) open_win = 0;
        else if (!strcmp(argv[i], "--bind-all")) g_bind_all = 1;
        else if (!strcmp(argv[i], "--version")) { printf("%s\n", APP_VERSION); return 0; }
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
    }
    (void)console;

    if (!datadir[0]) resolve_datadir(datadir, sizeof(datadir));
    log_write("%s %s", APP_NAME, APP_VERSION);
    if (app_init(datadir, port) != 0) { log_write("demarrage impossible"); return 1; }
    if (open_win) open_app_window();
    serve_loop();
    return 0;
}

#endif
