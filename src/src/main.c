#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 31
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _FILE_OFFSET_BITS 64

#include "config.h"
#include "sqfs_fs.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <time.h>
#include <fuse.h>

#ifndef _MODE_T_DEFINED
typedef unsigned int mode_t;
#define _MODE_T_DEFINED
#endif

#ifndef O_RDONLY
#define O_RDONLY  0
#endif
#ifndef O_WRONLY
#define O_WRONLY  1
#endif
#ifndef O_RDWR
#define O_RDWR    2
#endif
#ifndef O_ACCMODE
#define O_ACCMODE 3
#endif

typedef unsigned long long fsblkcnt_t;
typedef unsigned long long fsfilcnt_t;

static SqfsCtx g_ctx;
static struct fuse* g_fuse = NULL;
static FILE* g_log = NULL;
static log_level_t g_log_level = SQFS_LOG_INFO;
static CRITICAL_SECTION g_log_cs;
static BOOL g_log_cs_init = FALSE;
static wchar_t g_upper_rootW[MAX_PATH * 6];

typedef struct {
    char exe[MAX_PATH * 4];
    char args[2048];
    char cwd[MAX_PATH * 4];
    char mount_root[8];
} autorun_cfg_t;

static BOOL g_enable_extract_fallback = TRUE;
static HWND g_console_hwnd = NULL;
static WINDOWPLACEMENT g_console_wp;
static BOOL g_console_wp_valid = FALSE;

static void mkdir_pW(const wchar_t* path);
static BOOL launch_game(const autorun_cfg_t* a, PROCESS_INFORMATION* pi, DWORD creation_flags);
static void split_cmd(const char* cmd, char* exe, size_t exesz, char* args, size_t argssz);

typedef struct {
    DWORD pid;
    HWND hwnd;
} fg_find_t;

static BOOL CALLBACK enum_find_pid_window(HWND hwnd, LPARAM lp) {
    fg_find_t* ctx = (fg_find_t*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    ctx->hwnd = hwnd;
    return FALSE;
}

static void bring_process_to_foreground(DWORD pid) {
    if (!pid) return;
    fg_find_t ctx;
    ctx.pid = pid;
    ctx.hwnd = NULL;

    for (int i = 0; i < 40 && !ctx.hwnd; ++i) {
        EnumWindows(enum_find_pid_window, (LPARAM)&ctx);
        if (!ctx.hwnd) Sleep(50);
    }

    if (ctx.hwnd) {
        ShowWindow(ctx.hwnd, SW_SHOWMAXIMIZED);
        SetForegroundWindow(ctx.hwnd);
        BringWindowToTop(ctx.hwnd);
    }
}

static void minimize_console_window(void) {
    if (!g_console_hwnd) return;
    ShowWindow(g_console_hwnd, SW_MINIMIZE);
}

static void restore_console_window(void) {
    if (!g_console_hwnd) return;
    if (g_console_wp_valid) {
        SetWindowPlacement(g_console_hwnd, &g_console_wp);
    } else {
        ShowWindow(g_console_hwnd, SW_RESTORE);
    }
    SetForegroundWindow(g_console_hwnd);
}

typedef struct {
    BOOL valid;
    BOOL extract_mode;
    BOOL keep_extracted;
    char extract_root[MAX_PATH * 4];
    char exe[MAX_PATH * 4];
    char cwd[MAX_PATH * 4];
    char args[2048];
} game_profile_t;

static char g_config_dirA[MAX_PATH * 2] = { 0 };

static void log_open(void) {
    if (g_log) return;
    if (!g_log_cs_init) {
        InitializeCriticalSection(&g_log_cs);
        g_log_cs_init = TRUE;
    }

    char exe[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe, MAX_PATH) == 0) return;
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';

    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%s\\log.txt", exe);
    g_log = fopen(log_path, "w");
}

static void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
    if (g_log_cs_init) {
        DeleteCriticalSection(&g_log_cs);
        g_log_cs_init = FALSE;
    }
}

void mlogf_level(log_level_t level, const char* fmt, ...) {
    if (level > g_log_level) return;
    log_open();
    if (!g_log) return;

    static const char* pfx[] = { "[ERROR]", "[WARN]", "[INFO]", "[DEBUG]", "[TRACE]" };
    SYSTEMTIME st;
    GetLocalTime(&st);

    EnterCriticalSection(&g_log_cs);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] %s ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, pfx[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    LeaveCriticalSection(&g_log_cs);
}

void mlogf(const char* fmt, ...) {
    log_open();
    if (!g_log) return;

    EnterCriticalSection(&g_log_cs);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    LeaveCriticalSection(&g_log_cs);
}

static void utf8_to_wide(const char* in, wchar_t* out, size_t outsz) {
    if (!out || outsz == 0) return;
    out[0] = L'\0';
    if (!in) return;
    if (MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)outsz) == 0) {
        MultiByteToWideChar(CP_ACP, 0, in, -1, out, (int)outsz);
        out[outsz - 1] = L'\0';
    }
}

static BOOL path_existsA(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES;
}

static BOOL dir_existsA(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void game_name_from_image(const char* image_path, char* out, size_t outsz) {
    const char* base = strrchr(image_path, '\\');
    if (!base) base = strrchr(image_path, '/');
    base = base ? base + 1 : image_path;
    strncpy(out, base, outsz - 1);
    out[outsz - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    for (char* c = out; *c; ++c) {
        if (*c == '<' || *c == '>' || *c == ':' || *c == '"' || *c == '/' || *c == '\\' || *c == '|' || *c == '?' || *c == '*') *c = '_';
    }
}

static void ensure_config_dirA(void) {
    if (g_config_dirA[0]) return;
    char exe[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe, MAX_PATH) == 0) return;
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';
    snprintf(g_config_dirA, sizeof(g_config_dirA), "%s\\config", exe);
    CreateDirectoryA(g_config_dirA, NULL);
}

static void get_profile_path(const char* image_path, char* out, size_t outsz) {
    ensure_config_dirA();
    char game[260];
    game_name_from_image(image_path, game, sizeof(game));
    snprintf(out, outsz, "%s\\%s.profile", g_config_dirA, game);
}

static BOOL load_game_profile(const char* image_path, game_profile_t* p) {
    if (!image_path || !p) return FALSE;
    ZeroMemory(p, sizeof(*p));

    char pp[MAX_PATH * 3];
    get_profile_path(image_path, pp, sizeof(pp));
    FILE* f = fopen(pp, "r");
    if (!f) return FALSE;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (strncmp(line, "MODE=", 5) == 0) p->extract_mode = (_stricmp(line + 5, "EXTRACT") == 0);
        else if (strncmp(line, "KEEP=", 5) == 0) p->keep_extracted = (atoi(line + 5) != 0);
        else if (strncmp(line, "ROOT=", 5) == 0) strncpy(p->extract_root, line + 5, sizeof(p->extract_root) - 1);
        else if (strncmp(line, "EXE=", 4) == 0) strncpy(p->exe, line + 4, sizeof(p->exe) - 1);
        else if (strncmp(line, "CWD=", 4) == 0) strncpy(p->cwd, line + 4, sizeof(p->cwd) - 1);
        else if (strncmp(line, "ARGS=", 5) == 0) strncpy(p->args, line + 5, sizeof(p->args) - 1);
    }
    fclose(f);
    p->valid = TRUE;
    return TRUE;
}

static void save_game_profile(const char* image_path, const game_profile_t* p) {
    if (!image_path || !p) return;
    char pp[MAX_PATH * 3];
    get_profile_path(image_path, pp, sizeof(pp));
    FILE* f = fopen(pp, "w");
    if (!f) return;
    fprintf(f, "MODE=%s\n", p->extract_mode ? "EXTRACT" : "FUSE");
    fprintf(f, "KEEP=%d\n", p->keep_extracted ? 1 : 0);
    fprintf(f, "ROOT=%s\n", p->extract_root);
    fprintf(f, "EXE=%s\n", p->exe);
    fprintf(f, "CWD=%s\n", p->cwd);
    fprintf(f, "ARGS=%s\n", p->args);
    fclose(f);
}

static void get_extract_root(const char* image_path, char* out, size_t outsz) {
    char game[260];
    game_name_from_image(image_path, game, sizeof(game));

    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base));
    if (n == 0 || n >= sizeof(base)) {
        GetTempPathA(sizeof(base), base);
        size_t len = strlen(base);
        if (len && (base[len - 1] == '\\' || base[len - 1] == '/')) base[len - 1] = '\0';
    }
    snprintf(out, outsz, "%s\\SquashWinFS\\Extract\\%s", base, game);
}

static BOOL copy_fileW_simple(const wchar_t* srcW, const wchar_t* dstW) {
    HANDLE hs = CreateFileW(srcW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hs == INVALID_HANDLE_VALUE) return FALSE;

    wchar_t parent[MAX_PATH * 6];
    wcsncpy_s(parent, _countof(parent), dstW, _TRUNCATE);
    wchar_t* slash = wcsrchr(parent, L'\\');
    if (slash) {
        *slash = L'\0';
        mkdir_pW(parent);
    }

    HANDLE hd = CreateFileW(dstW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hd == INVALID_HANDLE_VALUE) {
        CloseHandle(hs);
        return FALSE;
    }

    char buf[1 << 20];
    DWORD rd = 0, wr = 0;
    BOOL ok = TRUE;
    while (ReadFile(hs, buf, sizeof(buf), &rd, NULL) && rd > 0) {
        if (!WriteFile(hd, buf, rd, &wr, NULL) || wr != rd) {
            ok = FALSE;
            break;
        }
    }
    if (GetLastError() != ERROR_SUCCESS && GetLastError() != ERROR_HANDLE_EOF) ok = FALSE;

    CloseHandle(hd);
    CloseHandle(hs);
    return ok;
}

static BOOL copy_treeW(const wchar_t* srcRootW, const wchar_t* dstRootW) {
    mkdir_pW(dstRootW);

    wchar_t patW[MAX_PATH * 6];
    _snwprintf(patW, _countof(patW) - 1, L"%s\\*", srcRootW);
    patW[_countof(patW) - 1] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(patW, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    BOOL ok = TRUE;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        wchar_t srcW[MAX_PATH * 6], dstW[MAX_PATH * 6];
        _snwprintf(srcW, _countof(srcW) - 1, L"%s\\%s", srcRootW, fd.cFileName);
        _snwprintf(dstW, _countof(dstW) - 1, L"%s\\%s", dstRootW, fd.cFileName);
        srcW[_countof(srcW) - 1] = L'\0';
        dstW[_countof(dstW) - 1] = L'\0';

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copy_treeW(srcW, dstW)) {
                ok = FALSE;
                break;
            }
        } else {
            if (!copy_fileW_simple(srcW, dstW)) {
                ok = FALSE;
                break;
            }
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return ok;
}

static void map_mounted_to_extracted(const autorun_cfg_t* a, const char* extract_root, char* out_path, size_t outsz, const char* in_path) {
    const char* rel = in_path;
    if (_strnicmp(in_path, a->mount_root, strlen(a->mount_root)) == 0) {
        rel = in_path + strlen(a->mount_root);
        if (*rel == '\\' || *rel == '/') rel++;
    }
    snprintf(out_path, outsz, "%s\\%s", extract_root, rel);
    for (char* p = out_path; *p; ++p) if (*p == '/') *p = '\\';
}

static BOOL find_autorun_in_extracted(const char* extract_root, char* out_autorun, size_t outsz, char* out_base, size_t basesz) {
    snprintf(out_autorun, outsz, "%s\\autorun.cmd", extract_root);
    if (path_existsA(out_autorun)) {
        strncpy(out_base, extract_root, basesz - 1);
        out_base[basesz - 1] = '\0';
        return TRUE;
    }

    char pattern[MAX_PATH * 4];
    snprintf(pattern, sizeof(pattern), "%s\\*", extract_root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    BOOL found = FALSE;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        char cand[MAX_PATH * 4];
        snprintf(cand, sizeof(cand), "%s\\%s\\autorun.cmd", extract_root, fd.cFileName);
        if (path_existsA(cand)) {
            strncpy(out_autorun, cand, outsz - 1);
            snprintf(out_base, basesz, "%s\\%s", extract_root, fd.cFileName);
            found = TRUE;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static BOOL resolve_extracted_launch(const char* extract_root, const autorun_cfg_t* mounted_cfg,
    char* out_exe, size_t out_exe_sz, char* out_cwd, size_t out_cwd_sz, char* out_args, size_t out_args_sz)
{
    char autorun_path[MAX_PATH * 4];
    char base_root[MAX_PATH * 4];
    if (!find_autorun_in_extracted(extract_root, autorun_path, sizeof(autorun_path), base_root, sizeof(base_root))) {
        return FALSE;
    }

    FILE* f = fopen(autorun_path, "r");
    if (!f) return FALSE;

    char dir[1024] = { 0 };
    char cmd[2048] = { 0 };
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (_strnicmp(line, "DIR=", 4) == 0) strncpy(dir, line + 4, sizeof(dir) - 1);
        else if (_strnicmp(line, "CMD=", 4) == 0) strncpy(cmd, line + 4, sizeof(cmd) - 1);
    }
    fclose(f);

    if (!dir[0] || !cmd[0]) return FALSE;

    char exe_tok[1024];
    split_cmd(cmd, exe_tok, sizeof(exe_tok), out_args, out_args_sz);
    if (!exe_tok[0]) return FALSE;

    if (strncmp(dir, "./", 2) == 0) memmove(dir, dir + 2, strlen(dir + 2) + 1);
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) {
        snprintf(out_exe, out_exe_sz, "%s\\%s", base_root, exe_tok);
        snprintf(out_cwd, out_cwd_sz, "%s", base_root);
    } else {
        snprintf(out_exe, out_exe_sz, "%s\\%s\\%s", base_root, dir, exe_tok);
        snprintf(out_cwd, out_cwd_sz, "%s\\%s", base_root, dir);
    }
    for (char* p = out_exe; *p; ++p) if (*p == '/') *p = '\\';
    for (char* p = out_cwd; *p; ++p) if (*p == '/') *p = '\\';

    if (!path_existsA(out_exe) || !dir_existsA(out_cwd)) {
        if (mounted_cfg) {
            map_mounted_to_extracted(mounted_cfg, extract_root, out_exe, out_exe_sz, mounted_cfg->exe);
            map_mounted_to_extracted(mounted_cfg, extract_root, out_cwd, out_cwd_sz, mounted_cfg->cwd);
            strncpy(out_args, mounted_cfg->args, out_args_sz - 1);
            out_args[out_args_sz - 1] = '\0';
        }
    }

    return path_existsA(out_exe) && dir_existsA(out_cwd);
}

static int CALLBACK BrowseCallbackProcA(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, lpData);
    }
    return 0;
}

static BOOL select_extraction_folderA(const char* default_path, const char* image_path, char* selected_path, size_t selected_size) {
    char game[260] = { 0 };
    if (image_path) game_name_from_image(image_path, game, sizeof(game));

    char title[512];
    if (game[0]) {
        snprintf(title, sizeof(title), "Select extraction folder for %s", game);
    } else {
        snprintf(title, sizeof(title), "Select extraction folder");
    }

    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = NULL;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    bi.lpfn = BrowseCallbackProcA;
    bi.lParam = (LPARAM)default_path;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) {
        if (SUCCEEDED(hr)) CoUninitialize();
        return FALSE;
    }

    BOOL ok = SHGetPathFromIDListA(pidl, selected_path);
    CoTaskMemFree(pidl);
    if (SUCCEEDED(hr)) CoUninitialize();
    if (!ok) return FALSE;

    selected_path[selected_size - 1] = '\0';
    return selected_path[0] != '\0';
}

static BOOL extract_archive_with_7z(const char* archive_path, const char* dest_path) {
    char sevenZipPath[MAX_PATH * 2] = { 0 };
    BOOL use_gui = FALSE;
    char exeDir[MAX_PATH] = { 0 };
    char localG[MAX_PATH * 2] = { 0 }, localC[MAX_PATH * 2] = { 0 }, toolsG[MAX_PATH * 2] = { 0 }, toolsC[MAX_PATH * 2] = { 0 };
    if (GetModuleFileNameA(NULL, exeDir, MAX_PATH) > 0) {
        char* s = strrchr(exeDir, '\\');
        if (s) *s = '\0';
        snprintf(localG, sizeof(localG), "%s\\7zG.exe", exeDir);
        snprintf(localC, sizeof(localC), "%s\\7z.exe", exeDir);
        snprintf(toolsG, sizeof(toolsG), "%s\\tools\\7zip\\7zG.exe", exeDir);
        snprintf(toolsC, sizeof(toolsC), "%s\\tools\\7zip\\7z.exe", exeDir);
    }

    const char* bundled_gui[] = { localG, toolsG };
    const char* bundled_cli[] = { localC, toolsC };
    const char* gui_candidates[] = {
        "C:\\Program Files\\7-Zip\\7zG.exe",
        "C:\\Program Files (x86)\\7-Zip\\7zG.exe"
    };
    const char* cli_candidates[] = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe",
        "7z.exe"
    };

    for (int i = 0; i < 2; ++i) {
        if (bundled_gui[i][0] && path_existsA(bundled_gui[i])) {
            strncpy(sevenZipPath, bundled_gui[i], sizeof(sevenZipPath) - 1);
            use_gui = TRUE;
            break;
        }
    }
    if (!sevenZipPath[0]) for (int i = 0; i < 2; ++i) {
        if (bundled_cli[i][0] && path_existsA(bundled_cli[i])) {
            strncpy(sevenZipPath, bundled_cli[i], sizeof(sevenZipPath) - 1);
            use_gui = FALSE;
            break;
        }
    }

    for (int i = 0; !sevenZipPath[0] && i < 2; ++i) {
        if (path_existsA(gui_candidates[i])) {
            strncpy(sevenZipPath, gui_candidates[i], sizeof(sevenZipPath) - 1);
            use_gui = TRUE;
            break;
        }
    }
    if (!sevenZipPath[0]) {
        for (int i = 0; i < 3; ++i) {
            if (i == 2 || path_existsA(cli_candidates[i])) {
                strncpy(sevenZipPath, cli_candidates[i], sizeof(sevenZipPath) - 1);
                use_gui = FALSE;
                break;
            }
        }
    }

    if (!sevenZipPath[0]) {
        MLOG_ERROR("[Hybrid] 7-Zip executable not found\n");
        MessageBoxA(NULL,
            "7-Zip was not found.\n\nPlease install 7-Zip to enable extraction fallback.",
            "SquashWinFS - Error",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return FALSE;
    }

    wchar_t destW[MAX_PATH * 6];
    utf8_to_wide(dest_path, destW, _countof(destW));
    mkdir_pW(destW);

    char cmd[8192];
    if (use_gui) {
        snprintf(cmd, sizeof(cmd), "\"%s\" x -y -bb1 \"-o%s\" \"%s\"", sevenZipPath, dest_path, archive_path);
    } else {
        snprintf(cmd, sizeof(cmd), "\"%s\" x -y -bb1 -bsp1 -bso1 -bse1 \"-o%s\" \"%s\"", sevenZipPath, dest_path, archive_path);
    }

    wchar_t cmdW[8192];
    utf8_to_wide(cmd, cmdW, _countof(cmdW));

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = use_gui ? SW_SHOW : SW_SHOW;

    MLOG_INFO("[Hybrid] Starting extraction with %s\n", sevenZipPath);
    MLOG_INFO("[Hybrid] Extraction destination: %s\n", dest_path);

    if (!CreateProcessW(NULL, cmdW, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MLOG_ERROR("[Hybrid] Failed to launch 7-Zip (%lu)\n", GetLastError());
        MessageBoxA(NULL,
            "Could not launch 7-Zip extraction process.",
            "SquashWinFS - Error",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return FALSE;
    }

    DWORD start = GetTickCount();
    while (1) {
        DWORD wr = WaitForSingleObject(pi.hProcess, 1000);
        if (wr == WAIT_OBJECT_0) break;
        DWORD elapsed = (GetTickCount() - start) / 1000;
        if ((elapsed % 10) == 0) {
            MLOG_INFO("[Hybrid] Extraction in progress... %lu sec\n", elapsed);
        }
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        MLOG_ERROR("[Hybrid] 7-Zip extraction failed with exit code %lu\n", code);
        MessageBoxA(NULL,
            "Extraction failed. 7-Zip returned an error.",
            "SquashWinFS - Error",
            MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        return FALSE;
    }

    MLOG_INFO("[Hybrid] Extraction completed successfully\n");
    return TRUE;
}

static BOOL try_extract_and_launch(const autorun_cfg_t* a, DWORD* out_exit, char* used_root, size_t used_root_size,
    char* used_exe, size_t used_exe_size, char* used_cwd, size_t used_cwd_size, char* used_args, size_t used_args_size)
{
    if (!g_enable_extract_fallback || !g_ctx.image_path) return FALSE;

    char default_root[MAX_PATH * 4];
    char extract_root[MAX_PATH * 4];
    get_extract_root(g_ctx.image_path, default_root, sizeof(default_root));
    strncpy(extract_root, default_root, sizeof(extract_root) - 1);

    if (!select_extraction_folderA(default_root, g_ctx.image_path, extract_root, sizeof(extract_root))) {
        MLOG_WARN("[Hybrid] Folder selection cancelled. Using default extraction path: %s\n", default_root);
        strncpy(extract_root, default_root, sizeof(extract_root) - 1);
    }

    if (used_root && used_root_size > 0) {
        strncpy(used_root, extract_root, used_root_size - 1);
        used_root[used_root_size - 1] = '\0';
    }

    char extracted_exe[MAX_PATH * 4] = { 0 }, extracted_cwd[MAX_PATH * 4] = { 0 }, extracted_args[2048] = { 0 };

    if (!resolve_extracted_launch(extract_root, a, extracted_exe, sizeof(extracted_exe), extracted_cwd, sizeof(extracted_cwd), extracted_args, sizeof(extracted_args))) {
        MLOG_WARN("[Hybrid] Building extracted cache at %s\n", extract_root);
        if (!extract_archive_with_7z(g_ctx.image_path, extract_root)) {
            MLOG_ERROR("[Hybrid] Extract fallback failed\n");
            return FALSE;
        }

        if (!resolve_extracted_launch(extract_root, a, extracted_exe, sizeof(extracted_exe), extracted_cwd, sizeof(extracted_cwd), extracted_args, sizeof(extracted_args))) {
            MLOG_ERROR("[Hybrid] Extracted files do not contain expected executable/cwd\n");
            return FALSE;
        }
    }

    autorun_cfg_t ex = *a;
    strncpy(ex.exe, extracted_exe, sizeof(ex.exe) - 1);
    strncpy(ex.cwd, extracted_cwd, sizeof(ex.cwd) - 1);
    strncpy(ex.args, extracted_args, sizeof(ex.args) - 1);

    if (used_exe && used_exe_size) {
        strncpy(used_exe, extracted_exe, used_exe_size - 1);
        used_exe[used_exe_size - 1] = '\0';
    }
    if (used_cwd && used_cwd_size) {
        strncpy(used_cwd, extracted_cwd, used_cwd_size - 1);
        used_cwd[used_cwd_size - 1] = '\0';
    }
    if (used_args && used_args_size) {
        strncpy(used_args, extracted_args, used_args_size - 1);
        used_args[used_args_size - 1] = '\0';
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!launch_game(&ex, &pi, 0)) {
        MLOG_ERROR("[Hybrid] Extract fallback launch failed (%lu)\n", GetLastError());
        return FALSE;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (out_exit) GetExitCodeProcess(pi.hProcess, out_exit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

static BOOL delete_treeW(const wchar_t* pathW) {
    if (!pathW || !pathW[0]) return FALSE;
    DWORD attrs = GetFileAttributesW(pathW);
    if (attrs == INVALID_FILE_ATTRIBUTES) return TRUE;

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(pathW, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(pathW) || GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    wchar_t patternW[MAX_PATH * 6];
    _snwprintf(patternW, _countof(patternW) - 1, L"%s\\*", pathW);
    patternW[_countof(patternW) - 1] = L'\0';

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(patternW, &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            wchar_t childW[MAX_PATH * 6];
            _snwprintf(childW, _countof(childW) - 1, L"%s\\%s", pathW, ffd.cFileName);
            childW[_countof(childW) - 1] = L'\0';
            if (!delete_treeW(childW)) {
                FindClose(hFind);
                return FALSE;
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }

    return RemoveDirectoryW(pathW) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static BOOL prompt_enable_extraction_fallback(void) {
    int res = MessageBoxA(NULL,
        "The game failed in mount mode.\n\n"
        "Do you want to switch to extraction mode for this game?",
        "SquashWinFS - Hybrid Mode",
        MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
    return res == IDYES;
}

static BOOL prompt_keep_extracted(void) {
    int res = MessageBoxA(NULL,
        "The game has finished.\n\n"
        "Do you want to keep extracted files for future launches?",
        "SquashWinFS - Cleanup",
        MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
    return res == IDYES;
}

static BOOL run_extracted_profile(const char* image_path, const game_profile_t* p, DWORD* out_exit) {
    if (!p || !p->extract_mode) return FALSE;
    if (!path_existsA(p->exe) || !dir_existsA(p->cwd)) return FALSE;

    autorun_cfg_t ex;
    ZeroMemory(&ex, sizeof(ex));
    strncpy(ex.exe, p->exe, sizeof(ex.exe) - 1);
    strncpy(ex.cwd, p->cwd, sizeof(ex.cwd) - 1);
    strncpy(ex.args, p->args, sizeof(ex.args) - 1);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!launch_game(&ex, &pi, 0)) return FALSE;
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (out_exit) GetExitCodeProcess(pi.hProcess, out_exit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    BOOL keep = prompt_keep_extracted();
    game_profile_t np = *p;
    np.keep_extracted = keep;
    if (!keep) {
        wchar_t rw[MAX_PATH * 6];
        utf8_to_wide(p->extract_root, rw, _countof(rw));
        delete_treeW(rw);
        np.extract_mode = FALSE;
        np.valid = TRUE;
    }
    save_game_profile(image_path, &np);
    return TRUE;
}

static void sanitize_nameW(const wchar_t* in, wchar_t* out, size_t outsz) {
    if (!in || !out || outsz == 0) return;
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outsz; ++i) {
        wchar_t c = in[i];
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') c = L'_';
        out[j++] = c;
    }
    out[j] = L'\0';
}

static void mkdir_pW(const wchar_t* path) {
    if (!path || !path[0]) return;
    wchar_t tmp[MAX_PATH * 6];
    wcsncpy_s(tmp, _countof(tmp), path, _TRUNCATE);

    for (wchar_t* p = tmp + 3; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t ch = *p;
            *p = L'\0';
            CreateDirectoryW(tmp, NULL);
            *p = ch;
        }
    }
    CreateDirectoryW(tmp, NULL);
}

static void get_overlay_baseW(wchar_t* outW, size_t outsz) {
    if (!outW || outsz == 0) return;
    outW[0] = L'\0';

    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", outW, (DWORD)outsz);
    if (n > 0 && n < outsz - 20) {
        wcscat_s(outW, outsz, L"\\SquashWinFS\\Overlay");
        return;
    }

    if (GetTempPathW((DWORD)outsz, outW) > 0) {
        size_t len = wcslen(outW);
        if (len && (outW[len - 1] == L'\\' || outW[len - 1] == L'/')) outW[len - 1] = L'\0';
        if (wcslen(outW) < outsz - 20) wcscat_s(outW, outsz, L"\\SquashWinFS\\Overlay");
    }
}

static void setup_upper_root(const char* image_path, const char* volname) {
    wchar_t baseW[MAX_PATH] = { 0 };
    if (image_path && image_path[0]) {
        wchar_t imgW[MAX_PATH];
        utf8_to_wide(image_path, imgW, _countof(imgW));
        const wchar_t* p = wcsrchr(imgW, L'\\');
        p = p ? p + 1 : imgW;
        wcsncpy_s(baseW, _countof(baseW), p, _TRUNCATE);
        wchar_t* dot = wcsrchr(baseW, L'.');
        if (dot) *dot = 0;
    } else if (volname && volname[0]) {
        utf8_to_wide(volname, baseW, _countof(baseW));
    } else {
        wcscpy_s(baseW, _countof(baseW), L"Game");
    }

    wchar_t safeW[MAX_PATH];
    sanitize_nameW(baseW, safeW, _countof(safeW));
    wchar_t overlay_baseW[MAX_PATH * 6];
    get_overlay_baseW(overlay_baseW, _countof(overlay_baseW));
    if (!overlay_baseW[0]) wcscpy_s(overlay_baseW, _countof(overlay_baseW), L"C:\\Temp\\SquashWinFS\\Overlay");

    swprintf_s(g_upper_rootW, _countof(g_upper_rootW), L"%s\\%ls", overlay_baseW, safeW);
    mkdir_pW(g_upper_rootW);

    char rootU8[MAX_PATH * 6];
    WideCharToMultiByte(CP_UTF8, 0, g_upper_rootW, -1, rootU8, (int)sizeof(rootU8), NULL, NULL);
    MLOG_INFO("[Overlay] Root: %s\n", rootU8);
}

static void upper_path_from_fuseW(const char* fuse_path, wchar_t* out, size_t outsz) {
    if (!fuse_path || fuse_path[0] == 0 || (fuse_path[0] == '/' && fuse_path[1] == 0)) {
        wcsncpy_s(out, outsz, g_upper_rootW, _TRUNCATE);
        return;
    }
    wchar_t subW[MAX_PATH * 6];
    utf8_to_wide(fuse_path[0] == '/' ? fuse_path + 1 : fuse_path, subW, _countof(subW));
    for (wchar_t* s = subW; *s; ++s) if (*s == L'/') *s = L'\\';
    swprintf_s(out, outsz, L"%s\\%ls", g_upper_rootW, subW);
}

static int upper_exists(const char* fuse_path) {
    wchar_t wpath[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, wpath, _countof(wpath));
    return GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES;
}

static void whiteout_path_from_fuseW(const char* fuse_path, wchar_t* out, size_t outsz) {
    wchar_t subW[MAX_PATH * 6];
    if (!fuse_path || !fuse_path[0] || (fuse_path[0] == '/' && fuse_path[1] == 0)) {
        wcscpy_s(subW, _countof(subW), L"_root");
    } else {
        utf8_to_wide((fuse_path[0] == '/') ? fuse_path + 1 : fuse_path, subW, _countof(subW));
        for (wchar_t* s = subW; *s; ++s) {
            if (*s == L'/') *s = L'\\';
        }
    }
    swprintf_s(out, outsz, L"%s\\.whiteout\\%ls.wo", g_upper_rootW, subW);
}

static void ensure_parent_dirsW(const wchar_t* pathW) {
    wchar_t tmp[MAX_PATH * 6];
    wcsncpy_s(tmp, _countof(tmp), pathW, _TRUNCATE);
    wchar_t* slash = wcsrchr(tmp, L'\\');
    if (!slash) return;
    *slash = L'\0';
    if (tmp[0]) mkdir_pW(tmp);
}

static int is_whiteouted(const char* fuse_path) {
    wchar_t woW[MAX_PATH * 6];
    whiteout_path_from_fuseW(fuse_path, woW, _countof(woW));
    return GetFileAttributesW(woW) != INVALID_FILE_ATTRIBUTES;
}

static void clear_whiteout(const char* fuse_path) {
    wchar_t woW[MAX_PATH * 6];
    whiteout_path_from_fuseW(fuse_path, woW, _countof(woW));
    DeleteFileW(woW);
}

static int mark_whiteout(const char* fuse_path) {
    wchar_t woW[MAX_PATH * 6];
    whiteout_path_from_fuseW(fuse_path, woW, _countof(woW));
    ensure_parent_dirsW(woW);

    HANDLE h = CreateFileW(woW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        MLOG_ERROR("[Overlay] whiteout create failed for %s (err=%lu)\n", fuse_path, GetLastError());
        return -EIO;
    }
    CloseHandle(h);
    return 0;
}

static int upper_is_dir(const char* fuse_path) {
    wchar_t wpath[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, wpath, _countof(wpath));
    DWORD a = GetFileAttributesW(wpath);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int upper_getattr_fuse(const char* fuse_path, struct fuse_stat* st) {
    wchar_t wpath[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, wpath, _countof(wpath));
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) return -ENOENT;

    memset(st, 0, sizeof(*st));
    ULARGE_INTEGER sz;
    sz.HighPart = fad.nFileSizeHigh;
    sz.LowPart = fad.nFileSizeLow;

    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        st->st_mode = S_IFDIR | 0777;
        st->st_size = 0;
    } else {
        st->st_mode = S_IFREG | 0666;
        st->st_size = (fuse_off_t)sz.QuadPart;
    }
    st->st_nlink = 1;
    return 0;
}

static void ensure_upper_parent_dirs(const char* fuse_path) {
    wchar_t wpath[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, wpath, _countof(wpath));
    wchar_t* slash = wcsrchr(wpath, L'\\');
    if (!slash) return;
    *slash = L'\0';
    if (!wpath[0]) return;
    mkdir_pW(wpath);
}

static int cow_copy_from_sqfs(const char* fuse_path) {
    if (upper_exists(fuse_path)) return 0;

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, fuse_path, &inode);
    if (r != 0) return r;
    if ((inode->base.mode & S_IFMT) != S_IFREG) {
        sqfs_free(inode);
        return -EISDIR;
    }

    ensure_upper_parent_dirs(fuse_path);
    wchar_t upW[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, upW, _countof(upW));
    HANDLE h = CreateFileW(upW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        MLOG_ERROR("[Overlay] COW create failed: %s (err=%lu)\n", fuse_path, err);
        sqfs_free(inode);
        return -EIO;
    }

    sqfs_u64 fsz = 0;
    sqfs_inode_get_file_size(inode, &fsz);
    const size_t CH = 1u << 20;
    char* buf = (char*)malloc(CH);
    if (!buf) {
        CloseHandle(h);
        sqfs_free(inode);
        return -ENOMEM;
    }

    size_t done = 0;
    int rc = 0;
    while (done < (size_t)fsz) {
        size_t want = (size_t)fsz - done;
        if (want > CH) want = CH;
        size_t got = 0;
        rc = sqfs_read_file_range(&g_ctx, inode, (uint64_t)done, buf, want, &got);
        if (rc != 0 || got == 0) break;
        DWORD wr = 0;
        if (!WriteFile(h, buf, (DWORD)got, &wr, NULL) || wr != (DWORD)got) {
            MLOG_ERROR("[Overlay] COW write failed: %s (err=%lu)\n", fuse_path, GetLastError());
            rc = -EIO;
            break;
        }
        done += got;
    }
    free(buf);
    CloseHandle(h);
    sqfs_free(inode);
    return rc;
}

static int open_upper_as_fd(const char* fuse_path, int oflags, struct fuse_file_info* fi) {
    wchar_t upW[MAX_PATH * 6];
    upper_path_from_fuseW(fuse_path, upW, _countof(upW));
    int fd = _wopen(upW, oflags | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        MLOG_ERROR("[Overlay] _wopen failed for %s\n", fuse_path);
        return -EIO;
    }
    fi->fh = (uint64_t)fd;
    return 0;
}

static int read_file_all(const char* squash_path, char** out_buf, size_t* out_len) {
    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, squash_path, &inode);
    if (r != 0) return r;

    sqfs_u64 fsz = 0;
    if (sqfs_inode_get_file_size(inode, &fsz) != 0) {
        sqfs_free(inode);
        return -EIO;
    }

    size_t cap = (size_t)fsz;
    char* buf = (char*)malloc(cap + 1);
    if (!buf) {
        sqfs_free(inode);
        return -ENOMEM;
    }

    size_t off = 0;
    while (off < cap) {
        size_t want = cap - off;
        if (want > (1u << 20)) want = (1u << 20);
        size_t got = 0;
        r = sqfs_read_file_range(&g_ctx, inode, off, buf + off, want, &got);
        if (r != 0) break;
        if (got == 0) break;
        off += got;
    }
    sqfs_free(inode);
    if (r != 0) {
        free(buf);
        return r;
    }

    buf[off] = '\0';
    *out_buf = buf;
    if (out_len) *out_len = off;
    return 0;
}

static void split_cmd(const char* cmd, char* exe, size_t exesz, char* args, size_t argssz) {
    exe[0] = '\0';
    args[0] = '\0';
    if (!cmd || !cmd[0]) return;

    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '"') {
        cmd++;
        const char* end = strchr(cmd, '"');
        if (!end) end = cmd + strlen(cmd);
        size_t n = (size_t)(end - cmd);
        if (n >= exesz) n = exesz - 1;
        memcpy(exe, cmd, n);
        exe[n] = '\0';
        cmd = (*end == '"') ? end + 1 : end;
    } else {
        const char* sp = strpbrk(cmd, " \t");
        if (!sp) sp = cmd + strlen(cmd);
        size_t n = (size_t)(sp - cmd);
        if (n >= exesz) n = exesz - 1;
        memcpy(exe, cmd, n);
        exe[n] = '\0';
        cmd = sp;
    }

    while (*cmd == ' ' || *cmd == '\t') cmd++;
    strncpy(args, cmd, argssz - 1);
    args[argssz - 1] = '\0';
}

static int build_autorun(const char* mount_letter, autorun_cfg_t* cfg) {
    strncpy(cfg->mount_root, mount_letter, sizeof(cfg->mount_root) - 1);
    cfg->mount_root[sizeof(cfg->mount_root) - 1] = '\0';

    char* txt = NULL;
    size_t n = 0;
    int r = read_file_all("/autorun.cmd", &txt, &n);
    if (r != 0) {
        MLOG_ERROR("[Autorun] Could not read /autorun.cmd (%d)\n", r);
        return r;
    }

    char dir[1024] = { 0 };
    char cmd[2048] = { 0 };

    char* line = txt;
    char* end = txt + n;
    while (line < end) {
        char* nl = (char*)memchr(line, '\n', (size_t)(end - line));
        if (!nl) nl = end;
        if (nl > line && nl[-1] == '\r') nl[-1] = '\0'; else *nl = '\0';

        if (_strnicmp(line, "DIR=", 4) == 0) strncpy(dir, line + 4, sizeof(dir) - 1);
        else if (_strnicmp(line, "CMD=", 4) == 0) strncpy(cmd, line + 4, sizeof(cmd) - 1);
        line = nl + 1;
    }
    free(txt);

    if (!dir[0] || !cmd[0]) {
        MLOG_ERROR("[Autorun] autorun.cmd missing DIR or CMD\n");
        return -ENOENT;
    }

    char exe_tok[1024];
    split_cmd(cmd, exe_tok, sizeof(exe_tok), cfg->args, sizeof(cfg->args));
    if (!exe_tok[0]) return -ENOENT;

    if (strncmp(dir, "./", 2) == 0) memmove(dir, dir + 2, strlen(dir + 2) + 1);
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) {
        snprintf(cfg->exe, sizeof(cfg->exe), "%s\\%s", mount_letter, exe_tok);
        snprintf(cfg->cwd, sizeof(cfg->cwd), "%s\\", mount_letter);
    } else {
        snprintf(cfg->exe, sizeof(cfg->exe), "%s\\%s\\%s", mount_letter, dir, exe_tok);
        snprintf(cfg->cwd, sizeof(cfg->cwd), "%s\\%s", mount_letter, dir);
    }
    for (char* p = cfg->exe; *p; ++p) if (*p == '/') *p = '\\';
    for (char* p = cfg->cwd; *p; ++p) if (*p == '/') *p = '\\';
    return 0;
}

static void map_sqfs_stat_to_fuse(const SqfsStat* src, struct fuse_stat* dst) {
    memset(dst, 0, sizeof(*dst));
    dst->st_mode = src->mode;
    dst->st_nlink = ((src->mode & S_IFMT) == S_IFDIR) ? 2 : src->nlink;
    dst->st_size = src->size;
    dst->st_ino = (fuse_ino_t)src->ino;
    dst->st_blksize = 4096;
    dst->st_blocks = (fuse_blkcnt_t)((src->size + 511) / 512);
    time_t t = (time_t)src->sq_mtime;
    dst->st_mtim.tv_sec = t;
    dst->st_atim.tv_sec = t;
    dst->st_ctim.tv_sec = t;
}

static int op_getattr(const char* path, struct fuse_stat* st) {
    if (is_whiteouted(path)) return -ENOENT;

    if (upper_exists(path)) {
        int ur = upper_getattr_fuse(path, st);
        if (ur == 0) return 0;
    }

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;

    SqfsStat ss;
    sqfs_stat_from_inode(&g_ctx, inode, &ss);
    sqfs_free(inode);
    map_sqfs_stat_to_fuse(&ss, st);
    return 0;
}

static int op_readlink(const char* path, char* buf, size_t size) {
    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;
    r = sqfs_read_symlink(&g_ctx, inode, buf, size);
    sqfs_free(inode);
    return r;
}

static int op_access(const char* path, int mask) {
    (void)mask;
    if (is_whiteouted(path)) return -ENOENT;
    if (upper_exists(path)) return 0;
    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r == 0) sqfs_free(inode);
    return r;
}

static int op_opendir(const char* path, struct fuse_file_info* fi) {
    (void)fi;
    if (is_whiteouted(path)) return -ENOENT;
    if (upper_is_dir(path)) return 0;
    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;
    if ((inode->base.mode & S_IFMT) != S_IFDIR) {
        sqfs_free(inode);
        return -ENOTDIR;
    }
    sqfs_free(inode);
    return 0;
}

static int op_releasedir(const char* path, struct fuse_file_info* fi) {
    (void)path;
    (void)fi;
    return 0;
}

static int op_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
    fuse_off_t off, struct fuse_file_info* fi)
{
    (void)off;
    (void)fi;

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;
    if ((inode->base.mode & S_IFMT) != S_IFDIR) {
        sqfs_free(inode);
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    dir_lock();
    if (sqfs_dir_reader_open_dir(g_ctx.dir, inode, 0) == 0) {
        while (1) {
            sqfs_dir_entry_t* dent = NULL;
            int rr = sqfs_dir_reader_read(g_ctx.dir, &dent);
            if (rr != 0) {
                if (dent) sqfs_free(dent);
                break;
            }

            size_t nlen = (size_t)dent->size + 1;
            char name[2048];
            if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
            memcpy(name, dent->name, nlen);
            name[nlen] = '\0';
            sqfs_free(dent);

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            char child[4096];
            if (strcmp(path, "/") == 0) snprintf(child, sizeof(child), "/%s", name);
            else snprintf(child, sizeof(child), "%s/%s", path, name);
            if (is_whiteouted(child)) continue;
            filler(buf, name, NULL, 0);
        }
    }
    dir_unlock();
    sqfs_free(inode);

    wchar_t upW[MAX_PATH * 6], patW[MAX_PATH * 6];
    upper_path_from_fuseW(path, upW, _countof(upW));
    _snwprintf(patW, _countof(patW) - 1, L"%s\\*", upW);
    patW[_countof(patW) - 1] = L'\0';

    WIN32_FIND_DATAW fdw;
    HANDLE h = FindFirstFileW(patW, &fdw);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fdw.cFileName, L".") == 0 || wcscmp(fdw.cFileName, L"..") == 0) continue;
            char nameU8[1024];
            if (WideCharToMultiByte(CP_UTF8, 0, fdw.cFileName, -1, nameU8, (int)sizeof(nameU8), NULL, NULL) > 0) {
                filler(buf, nameU8, NULL, 0);
            }
        } while (FindNextFileW(h, &fdw));
        FindClose(h);
    }

    return 0;
}

static int op_open(const char* path, struct fuse_file_info* fi) {
    if (is_whiteouted(path)) return -ENOENT;

    int flags = fi->flags & O_ACCMODE;
    if (flags == O_WRONLY || flags == O_RDWR || (fi->flags & O_TRUNC)) {
        if (!upper_exists(path) && !(fi->flags & O_TRUNC)) {
            int rc = cow_copy_from_sqfs(path);
            if (rc != 0 && rc != -EISDIR && rc != -ENOENT) return rc;
        }
        ensure_upper_parent_dirs(path);
        int of = (flags == O_RDWR) ? _O_RDWR : _O_WRONLY;
        if (fi->flags & O_TRUNC) of |= _O_TRUNC;
        if (!upper_exists(path)) of |= _O_CREAT;
        clear_whiteout(path);
        return open_upper_as_fd(path, of, fi);
    }

    if (upper_exists(path)) {
        return open_upper_as_fd(path, _O_RDONLY, fi);
    }

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;
    if ((inode->base.mode & S_IFMT) != S_IFREG) {
        sqfs_free(inode);
        return -EISDIR;
    }
    sqfs_free(inode);
    return 0;
}

static int op_read(const char* path, char* buf, size_t size, fuse_off_t off,
    struct fuse_file_info* fi)
{
    if (fi && fi->fh) {
        int fd = (int)fi->fh;
        if (_lseeki64(fd, off, SEEK_SET) < 0) return -EIO;
        int rd = _read(fd, buf, (unsigned int)size);
        return (rd >= 0) ? rd : -EIO;
    }

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r != 0) return r;

    size_t got = 0;
    r = sqfs_read_file_range(&g_ctx, inode, (uint64_t)off, buf, size, &got);
    sqfs_free(inode);
    if (r != 0) return r;
    return (int)got;
}

static int op_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void)mode;
    ensure_upper_parent_dirs(path);
    clear_whiteout(path);
    return open_upper_as_fd(path, _O_CREAT | _O_TRUNC | _O_RDWR, fi);
}

static int op_write(const char* path, const char* buf, size_t size, fuse_off_t off, struct fuse_file_info* fi) {
    int fd = fi && fi->fh ? (int)fi->fh : -1;
    if (fd < 0) {
        ensure_upper_parent_dirs(path);
        wchar_t upW[MAX_PATH * 6];
        upper_path_from_fuseW(path, upW, _countof(upW));
        fd = _wopen(upW, _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd < 0) {
            MLOG_ERROR("[Overlay] write open failed: %s\n", path);
            return -EIO;
        }
        fi->fh = (uint64_t)fd;
    }

    if (_lseeki64(fd, off, SEEK_SET) < 0) return -EIO;
    int wr = _write(fd, buf, (unsigned int)size);
    if (wr < 0) {
        MLOG_ERROR("[Overlay] write failed: %s\n", path);
        return -EIO;
    }
    return wr;
}

static int op_truncate(const char* path, fuse_off_t size) {
    if (!upper_exists(path)) {
        if (size == 0) {
            ensure_upper_parent_dirs(path);
            wchar_t upW[MAX_PATH * 6];
            upper_path_from_fuseW(path, upW, _countof(upW));
            int fd0 = _wopen(upW, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
            if (fd0 < 0) return -EIO;
            _close(fd0);
            return 0;
        }
        int rc = cow_copy_from_sqfs(path);
        if (rc != 0) return rc;
    }

    wchar_t upW[MAX_PATH * 6];
    upper_path_from_fuseW(path, upW, _countof(upW));
    int fd = _wopen(upW, _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return -EIO;
    int rc = (_chsize_s(fd, (__int64)size) == 0) ? 0 : -EIO;
    _close(fd);
    return rc;
}

static int op_release(const char* path, struct fuse_file_info* fi) {
    (void)path;
    if (fi && fi->fh) {
        _close((int)fi->fh);
        fi->fh = 0;
    }
    return 0;
}

static int op_mkdir(const char* path, mode_t mode) {
    (void)mode;
    ensure_upper_parent_dirs(path);
    clear_whiteout(path);
    wchar_t upW[MAX_PATH * 6];
    upper_path_from_fuseW(path, upW, _countof(upW));
    return (CreateDirectoryW(upW, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -EIO;
}

static int op_unlink(const char* path) {
    if (upper_exists(path)) {
        wchar_t upW[MAX_PATH * 6];
        upper_path_from_fuseW(path, upW, _countof(upW));
        if (!DeleteFileW(upW)) return -EIO;
        return mark_whiteout(path);
    }

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r == 0) {
        sqfs_free(inode);
        return mark_whiteout(path);
    }
    return r;
}

static int op_rmdir(const char* path) {
    if (upper_is_dir(path)) {
        wchar_t upW[MAX_PATH * 6];
        upper_path_from_fuseW(path, upW, _countof(upW));
        if (!RemoveDirectoryW(upW)) return -EIO;
        return mark_whiteout(path);
    }

    sqfs_inode_generic_t* inode = NULL;
    int r = sqfs_lookup_path(&g_ctx, path, &inode);
    if (r == 0) {
        sqfs_free(inode);
        return mark_whiteout(path);
    }
    return r;
}

static int op_rename(const char* from, const char* to) {
    if (!upper_exists(from)) {
        int rc = cow_copy_from_sqfs(from);
        if (rc != 0) return rc;
    }
    ensure_upper_parent_dirs(to);
    wchar_t oldW[MAX_PATH * 6], newW[MAX_PATH * 6];
    upper_path_from_fuseW(from, oldW, _countof(oldW));
    upper_path_from_fuseW(to, newW, _countof(newW));
    if (!MoveFileExW(oldW, newW, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return -EIO;
    clear_whiteout(to);
    return mark_whiteout(from);
}

static int op_utimens(const char* path, const struct timespec tv[2]) {
    (void)path;
    (void)tv;
    return 0;
}

static int op_statfs(const char* path, struct fuse_statvfs* st) {
    (void)path;
    memset(st, 0, sizeof(*st));
    st->f_bsize = 4096;
    st->f_frsize = 4096;

    wchar_t rootW[MAX_PATH * 6];
    upper_path_from_fuseW("/", rootW, _countof(rootW));
    ULARGE_INTEGER avail = { 0 }, total = { 0 }, freeb = { 0 };
    if (GetDiskFreeSpaceExW(rootW, &avail, &total, &freeb)) {
        st->f_blocks = (fsblkcnt_t)(total.QuadPart / st->f_frsize);
        st->f_bfree = (fsblkcnt_t)(freeb.QuadPart / st->f_frsize);
        st->f_bavail = (fsblkcnt_t)(avail.QuadPart / st->f_frsize);
    } else {
        st->f_blocks = (fsblkcnt_t)((1ull << 40) / st->f_frsize);
        st->f_bfree = st->f_blocks / 2;
        st->f_bavail = st->f_bfree;
    }
    st->f_files = (fsfilcnt_t)(1u << 30);
    st->f_ffree = st->f_files / 2;
    st->f_namemax = 32767;
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  SquashWinFS.exe -i <image.wsquashfs> [-m X:] [-o VolName] [--mount-auto] [--debug|--trace]\n"
        "Notes:\n"
        "  - By Josuloku\n"
        "  - Support: https://ko-fi.com/josuloku\n"
        "  - Hybrid fallback: if fast mount launch fails, asks to extract and saves per-game profile\n");
}

static char* pick_free_drive(char* out, size_t outsz, const int* used) {
    if (outsz < 3) return NULL;
    DWORD mask = GetLogicalDrives();
    for (char c = 'Z'; c >= 'D'; --c) {
        int idx = c - 'A';
        if (used && used[idx]) continue;
        if ((mask & (1u << idx)) == 0) {
            snprintf(out, outsz, "%c:", c);
            return out;
        }
    }
    return NULL;
}

static BOOL normalize_mount(const char* in, char* out, size_t outsz) {
    if (!in || !in[0] || outsz < 3) return FALSE;
    char c = in[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    if (c < 'A' || c > 'Z') return FALSE;
    out[0] = c;
    out[1] = ':';
    out[2] = '\0';
    return TRUE;
}

static void quoteW(const wchar_t* in, wchar_t* out, size_t outsz) {
    if (!out || outsz == 0) return;
    if (!in || !in[0]) {
        out[0] = L'\0';
        return;
    }
    _snwprintf(out, outsz - 1, L"\"%s\"", in);
    out[outsz - 1] = L'\0';
}

static void build_injected_pathW(const wchar_t* cwdW, wchar_t* injected, size_t injected_sz) {
    if (!injected || injected_sz == 0) return;
    injected[0] = L'\0';
    if (!cwdW || !cwdW[0]) return;

    wchar_t system32W[MAX_PATH * 2];
    system32W[0] = L'\0';

    
    if (cwdW[1] == L':' && cwdW[2] == L'\\') {
        swprintf_s(system32W, _countof(system32W), L"%c:\\drive_c\\windows\\system32", cwdW[0]);
        if (GetFileAttributesW(system32W) != INVALID_FILE_ATTRIBUTES) {
            _snwprintf(injected, injected_sz - 1, L"%s;%s", cwdW, system32W);
            injected[injected_sz - 1] = L'\0';
            return;
        }
    }

    wcsncpy_s(injected, injected_sz, cwdW, _TRUNCATE);
}

static BOOL launch_with_prepended_pathW(const wchar_t* cmdlineW, const wchar_t* cwdW,
    DWORD flags, PROCESS_INFORMATION* pPi)
{
    wchar_t oldPath[32768];
    oldPath[0] = 0;
    DWORD oldLen = GetEnvironmentVariableW(L"PATH", oldPath, _countof(oldPath));
    wchar_t newPath[32768];

    if (cwdW && cwdW[0]) {
        wchar_t injected[32768];
        build_injected_pathW(cwdW, injected, _countof(injected));
        if (oldLen > 0) {
            _snwprintf(newPath, _countof(newPath) - 1, L"%s;%s", injected, oldPath);
        } else {
            _snwprintf(newPath, _countof(newPath) - 1, L"%s", injected);
        }
        newPath[_countof(newPath) - 1] = L'\0';
        SetEnvironmentVariableW(L"PATH", newPath);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMAXIMIZED;

    BOOL ok = CreateProcessW(NULL, (wchar_t*)cmdlineW, NULL, NULL, TRUE,
        flags | CREATE_UNICODE_ENVIRONMENT, NULL, cwdW, &si, pPi);

    if (oldLen > 0) SetEnvironmentVariableW(L"PATH", oldPath);
    else SetEnvironmentVariableW(L"PATH", NULL);

    return ok;
}

static BOOL launch_shell_execute_with_pathW(const wchar_t* exeW, const wchar_t* argsW,
    const wchar_t* cwdW, PROCESS_INFORMATION* pPi)
{
    wchar_t oldPath[32768];
    oldPath[0] = 0;
    DWORD oldLen = GetEnvironmentVariableW(L"PATH", oldPath, _countof(oldPath));
    wchar_t newPath[32768];

    if (cwdW && cwdW[0]) {
        wchar_t injected[32768];
        build_injected_pathW(cwdW, injected, _countof(injected));
        if (oldLen > 0) {
            _snwprintf(newPath, _countof(newPath) - 1, L"%s;%s", injected, oldPath);
        } else {
            _snwprintf(newPath, _countof(newPath) - 1, L"%s", injected);
        }
        newPath[_countof(newPath) - 1] = L'\0';
        SetEnvironmentVariableW(L"PATH", newPath);
    }

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpFile = exeW;
    sei.lpParameters = (argsW && argsW[0]) ? argsW : NULL;
    sei.lpDirectory = (cwdW && cwdW[0]) ? cwdW : NULL;
    sei.nShow = SW_SHOWMAXIMIZED;

    BOOL ok = ShellExecuteExW(&sei);

    if (oldLen > 0) SetEnvironmentVariableW(L"PATH", oldPath);
    else SetEnvironmentVariableW(L"PATH", NULL);

    if (ok && pPi) {
        ZeroMemory(pPi, sizeof(*pPi));
        pPi->hProcess = sei.hProcess;
        pPi->dwProcessId = sei.hProcess ? GetProcessId(sei.hProcess) : 0;
    }
    return ok;
}

static BOOL launch_game(const autorun_cfg_t* a, PROCESS_INFORMATION* pi, DWORD creation_flags) {
    wchar_t exeW[MAX_PATH * 4], argsW[2048], cmdW[4096], exeQuoted[MAX_PATH * 4];
    wchar_t cwdW[MAX_PATH * 4];
    utf8_to_wide(a->exe, exeW, _countof(exeW));
    utf8_to_wide(a->args, argsW, _countof(argsW));
    utf8_to_wide(a->cwd, cwdW, _countof(cwdW));

    quoteW(exeW, exeQuoted, _countof(exeQuoted));
    if (argsW[0]) _snwprintf(cmdW, _countof(cmdW) - 1, L"%s %s", exeQuoted, argsW);
    else _snwprintf(cmdW, _countof(cmdW) - 1, L"%s", exeQuoted);
    cmdW[_countof(cmdW) - 1] = L'\0';

    ZeroMemory(pi, sizeof(*pi));
    return launch_with_prepended_pathW(cmdW, cwdW, creation_flags, pi);
}

static BOOL wait_for_path_readyA(const char* path, DWORD timeout_ms) {
    DWORD start = GetTickCount();
    while (1) {
        DWORD attr = GetFileAttributesA(path);
        if (attr != INVALID_FILE_ATTRIBUTES) return TRUE;

        if ((GetTickCount() - start) >= timeout_ms) return FALSE;
        Sleep(100);
    }
}

static BOOL launch_game_shell_fallback(const autorun_cfg_t* a, DWORD* out_exit) {
    wchar_t exeW[MAX_PATH * 4];
    wchar_t cwdW[MAX_PATH * 4];
    wchar_t argsW[2048];
    utf8_to_wide(a->exe, exeW, _countof(exeW));
    utf8_to_wide(a->cwd, cwdW, _countof(cwdW));
    utf8_to_wide(a->args, argsW, _countof(argsW));

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    if (!launch_shell_execute_with_pathW(exeW, argsW, cwdW, &pi)) {
        MLOG_ERROR("[Autorun] ShellExecuteEx fallback failed (%lu)\n", GetLastError());
        return FALSE;
    }

    bring_process_to_foreground(pi.dwProcessId);

    if (pi.hProcess) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (out_exit) GetExitCodeProcess(pi.hProcess, out_exit);
        CloseHandle(pi.hProcess);
    }
    return TRUE;
}

static DWORD WINAPI autorun_thread(void* p) {
    autorun_cfg_t* a = (autorun_cfg_t*)p;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    HANDLE hJob = NULL;
    HANDLE hPort = NULL;

    MLOG_INFO("[Autorun] EXE: %s\n", a->exe);
    MLOG_INFO("[Autorun] CWD: %s\n", a->cwd);
    MLOG_INFO("[Autorun] ARGS: %s\n", a->args[0] ? a->args : "(none)");

    minimize_console_window();

    if (!wait_for_path_readyA(a->cwd, 15000)) {
        MLOG_ERROR("[Autorun] CWD not ready after mount: %s\n", a->cwd);
        free(a);
        if (g_fuse) fuse_exit(g_fuse);
        return 0;
    }
    if (!wait_for_path_readyA(a->exe, 15000)) {
        MLOG_ERROR("[Autorun] EXE not ready after mount: %s\n", a->exe);
        free(a);
        if (g_fuse) fuse_exit(g_fuse);
        return 0;
    }

    if (!launch_game(a, &pi, CREATE_SUSPENDED)) {
        DWORD err = GetLastError();
        MLOG_ERROR("[Autorun] CreateProcess failed (%lu), trying ShellExecute fallback\n", err);
        DWORD sec = 0;
        if (!launch_game_shell_fallback(a, &sec)) {
            restore_console_window();
            free(a);
            if (g_fuse) fuse_exit(g_fuse);
            return 0;
        }

        MLOG_INFO("[Autorun] Game exit code (ShellExecute): 0x%08X\n", sec);
        restore_console_window();
        free(a);
        if (g_fuse) fuse_exit(g_fuse);
        return 0;
    }

    hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

        hPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
        if (hPort) {
            JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp;
            acp.CompletionKey = hJob;
            acp.CompletionPort = hPort;
            SetInformationJobObject(hJob, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp));
        }

        if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
            MLOG_WARN("[Autorun] AssignProcessToJobObject failed (%lu), fallback to process wait\n", GetLastError());
            ResumeThread(pi.hThread);
        } else {
            ResumeThread(pi.hThread);
        }
        bring_process_to_foreground(pi.dwProcessId);
    } else {
        MLOG_WARN("[Autorun] CreateJobObject failed (%lu), fallback to process wait\n", GetLastError());
        ResumeThread(pi.hThread);
        bring_process_to_foreground(pi.dwProcessId);
    }

    DWORD ec = 0;
    DWORD start_tick = GetTickCount();
    if (hJob && hPort) {
        for (;;) {
            DWORD msg = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED ov = NULL;
            BOOL ok = GetQueuedCompletionStatus(hPort, &msg, &key, &ov, 30000);
            if (!ok && GetLastError() == WAIT_TIMEOUT) continue;
            if (msg == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
                GetExitCodeProcess(pi.hProcess, &ec);
                break;
            }
            if (!ok) break;
        }
    } else {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &ec);
    }

    DWORD elapsed = GetTickCount() - start_tick;
    if (ec != 0 && elapsed < 15000) {
        MLOG_WARN("[Autorun] Fast non-zero exit (0x%08X, %lu ms). Retrying without Job/Suspend...\n", ec, elapsed);

        PROCESS_INFORMATION pi2;
        ZeroMemory(&pi2, sizeof(pi2));
        if (launch_game(a, &pi2, 0)) {
            WaitForSingleObject(pi2.hProcess, INFINITE);
            GetExitCodeProcess(pi2.hProcess, &ec);
            MLOG_INFO("[Autorun] Retry exit code (CreateProcess plain): 0x%08X\n", ec);
            CloseHandle(pi2.hThread);
            CloseHandle(pi2.hProcess);
        }

        if (ec != 0) {
            MLOG_WARN("[Autorun] Still failing. Retrying with ShellExecute...\n");
            DWORD sec = 0;
            if (launch_game_shell_fallback(a, &sec)) {
                ec = sec;
                MLOG_INFO("[Autorun] Retry exit code (ShellExecute): 0x%08X\n", ec);
            }
        }
    }

    BOOL should_offer_extraction = FALSE;
    const char* extraction_reason = "";

    if (g_enable_extract_fallback) {
        if (ec != 0) {
            should_offer_extraction = TRUE;
            extraction_reason = "non-zero final exit code";
        }
    }

    if (should_offer_extraction) {
        if (prompt_enable_extraction_fallback()) {
            DWORD ex = ec;
            char chosen_root[MAX_PATH * 4] = { 0 };
            MLOG_WARN("[Hybrid] Triggering extraction fallback (%s). Exit=0x%08X, runtime=%lu ms\n", extraction_reason, ec, elapsed);
            char chosen_exe[MAX_PATH * 4] = { 0 };
            char chosen_cwd[MAX_PATH * 4] = { 0 };
            char chosen_args[2048] = { 0 };
            if (try_extract_and_launch(a, &ex, chosen_root, sizeof(chosen_root), chosen_exe, sizeof(chosen_exe), chosen_cwd, sizeof(chosen_cwd), chosen_args, sizeof(chosen_args))) {
                ec = ex;
                MLOG_INFO("[Hybrid] Extraction fallback exit code: 0x%08X\n", ec);

                game_profile_t prof;
                ZeroMemory(&prof, sizeof(prof));
                prof.valid = TRUE;
                prof.extract_mode = TRUE;
                if (chosen_root[0]) strncpy(prof.extract_root, chosen_root, sizeof(prof.extract_root) - 1);
                else get_extract_root(g_ctx.image_path, prof.extract_root, sizeof(prof.extract_root));
                if (chosen_exe[0]) strncpy(prof.exe, chosen_exe, sizeof(prof.exe) - 1);
                else map_mounted_to_extracted(a, prof.extract_root, prof.exe, sizeof(prof.exe), a->exe);
                if (chosen_cwd[0]) strncpy(prof.cwd, chosen_cwd, sizeof(prof.cwd) - 1);
                else map_mounted_to_extracted(a, prof.extract_root, prof.cwd, sizeof(prof.cwd), a->cwd);
                if (chosen_args[0]) strncpy(prof.args, chosen_args, sizeof(prof.args) - 1);
                else strncpy(prof.args, a->args, sizeof(prof.args) - 1);

                prof.keep_extracted = prompt_keep_extracted();
                if (!prof.keep_extracted) {
                    wchar_t rw[MAX_PATH * 6];
                    utf8_to_wide(prof.extract_root, rw, _countof(rw));
                    delete_treeW(rw);
                    prof.extract_mode = FALSE;
                }
                save_game_profile(g_ctx.image_path, &prof);
            } else {
                MLOG_WARN("[Hybrid] Extraction fallback could not run\n");
            }
        } else {
            MLOG_INFO("[Hybrid] User declined extraction fallback\n");
        }
    }

    MLOG_INFO("[Autorun] Final exit code: 0x%08X\n", ec);

    restore_console_window();

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (hPort) CloseHandle(hPort);
    if (hJob) CloseHandle(hJob);
    free(a);

    if (g_fuse) fuse_exit(g_fuse);
    return 0;
}

int main(int argc, char** argv) {
    const char* img = NULL;
    const char* vol = "SquashWinFS";
    const char* mnt_in = NULL;
    BOOL mount_auto = FALSE;

    static char img_buf[MAX_PATH * 4];
    static char mnt_buf[16];
    static char vol_buf[256];

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"SquashWinFS - libsquash mount");
    g_console_hwnd = GetConsoleWindow();
    ZeroMemory(&g_console_wp, sizeof(g_console_wp));
    g_console_wp.length = sizeof(g_console_wp);
    if (g_console_hwnd && GetWindowPlacement(g_console_hwnd, &g_console_wp)) {
        g_console_wp_valid = TRUE;
    }
    log_open();

    fprintf(stderr, "By Josuloku\n");
    fprintf(stderr, "Support: https://ko-fi.com/josuloku\n");

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            strncpy(img_buf, argv[++i], sizeof(img_buf) - 1);
            img = img_buf;
            continue;
        }
        if ((!strcmp(argv[i], "-m") || _stricmp(argv[i], "--mount") == 0) && i + 1 < argc) {
            strncpy(mnt_buf, argv[++i], sizeof(mnt_buf) - 1);
            mnt_in = mnt_buf;
            continue;
        }
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            strncpy(vol_buf, argv[++i], sizeof(vol_buf) - 1);
            vol = vol_buf;
            continue;
        }
        if (_stricmp(argv[i], "--mount-auto") == 0) {
            mount_auto = TRUE;
            continue;
        }
        if (_stricmp(argv[i], "--debug") == 0 || !strcmp(argv[i], "-d")) {
            g_log_level = SQFS_LOG_DEBUG;
            continue;
        }
        if (_stricmp(argv[i], "--trace") == 0 || !strcmp(argv[i], "-v")) {
            g_log_level = SQFS_LOG_TRACE;
            continue;
        }
    }

    if (!img && argc == 2 && argv[1][0] != '-') img = argv[1];
    if (!img) {
        usage();
        log_close();
        return 2;
    }

    game_profile_t prof;
    if (load_game_profile(img, &prof) && prof.extract_mode) {
        MLOG_INFO("[Profile] Extract profile found. Trying direct extracted launch...\n");
        DWORD pec = 0;
        if (run_extracted_profile(img, &prof, &pec)) {
            MLOG_INFO("[Profile] Extracted launch exit code: 0x%08X\n", pec);
            log_close();
            return (int)pec;
        }
        MLOG_WARN("[Profile] Extracted profile invalid, falling back to mount mode\n");
    }

    char mount[8] = { 0 };
    if (mnt_in) {
        if (!normalize_mount(mnt_in, mount, sizeof(mount))) {
            MLOG_ERROR("[Main] Invalid mount point: %s\n", mnt_in);
            log_close();
            return 2;
        }
    } else {
        if (!pick_free_drive(mount, sizeof(mount), NULL)) {
            MLOG_ERROR("[Main] No free drive letter\n");
            log_close();
            return 2;
        }
        mount_auto = TRUE;
    }

    MLOG_INFO("[Main] Image: %s\n", img);
    MLOG_INFO("[Main] Mount: %s\n", mount);
    MLOG_INFO("[Main] Volume: %s\n", vol);

    if (sqfs_ctx_open(&g_ctx, img, vol) != 0) {
        MLOG_ERROR("[Main] sqfs_ctx_open failed\n");
        log_close();
        return 3;
    }

    setup_upper_root(img, vol);

    struct fuse_operations ops;
    memset(&ops, 0, sizeof(ops));
    ops.getattr = op_getattr;
    ops.readlink = op_readlink;
    ops.access = op_access;
    ops.opendir = op_opendir;
    ops.releasedir = op_releasedir;
    ops.readdir = op_readdir;
    ops.open = op_open;
    ops.create = op_create;
    ops.read = op_read;
    ops.write = op_write;
    ops.truncate = op_truncate;
    ops.release = op_release;
    ops.mkdir = op_mkdir;
    ops.unlink = op_unlink;
    ops.rmdir = op_rmdir;
    ops.rename = op_rename;
    ops.statfs = op_statfs;
    ops.utimens = op_utimens;

    char optbuf[512];
    snprintf(optbuf, sizeof(optbuf),
        "volname=%s,fsname=NTFS,LocalDisk,fileindex,case_insens,attr_timeout=0,PreferredRequestSize=1048576", vol);

    char* fargv[] = { argv[0], "-f", "-o", optbuf, NULL };
    struct fuse_args fargs;
    fargs.argc = 4;
    fargs.argv = fargv;
    fargs.allocated = 0;

    struct fuse_chan* ch = NULL;
    int used[26] = { 0 };
    char active[8];
    strncpy(active, mount, sizeof(active) - 1);

    while (1) {
        char c = active[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        if (c >= 'A' && c <= 'Z') used[c - 'A'] = 1;

        MLOG_INFO("[Main] Trying mount %s\n", active);
        ch = fuse_mount(active, &fargs);
        if (ch) {
            strncpy(mount, active, sizeof(mount) - 1);
            break;
        }

        DWORD err = GetLastError();
        MLOG_ERROR("[Main] fuse_mount(%s) failed (%lu)\n", active, err);
        if (!mount_auto || !pick_free_drive(active, sizeof(active), used)) {
            sqfs_ctx_close(&g_ctx);
            log_close();
            return 5;
        }
    }

    g_fuse = fuse_new(ch, &fargs, &ops, sizeof(ops), NULL);
    if (!g_fuse) {
        MLOG_ERROR("[Main] fuse_new failed\n");
        fuse_unmount(mount, ch);
        sqfs_ctx_close(&g_ctx);
        log_close();
        return 4;
    }

    autorun_cfg_t ar;
    memset(&ar, 0, sizeof(ar));
    HANDLE th = NULL;

    if (build_autorun(mount, &ar) == 0) {
        autorun_cfg_t* p = (autorun_cfg_t*)malloc(sizeof(*p));
        if (p) {
            *p = ar;
            th = CreateThread(NULL, 0, autorun_thread, p, 0, NULL);
            if (!th) free(p);
        }
    } else {
        MLOG_WARN("[Main] autorun.cmd not usable; mounting only\n");
    }

    fprintf(stderr, "[INFO] Mounted on %s (Vol: %s). Press Ctrl-C to unmount.\n", mount, vol);
    int ret = fuse_loop(g_fuse);

    if (th) {
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }

    fuse_unmount(mount, ch);
    fuse_destroy(g_fuse);
    g_fuse = NULL;
    sqfs_ctx_close(&g_ctx);
    log_close();
    return ret;
}
