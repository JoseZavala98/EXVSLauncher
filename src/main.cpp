// EXVS Launcher - a modern launcher for EXVSIB (replacement for SnowLauncher).
//
// UI: ui/index.html (embedded as ui_html.h) shown in a WebView2 window through the webview library.
// Settings are kept in SnowLauncher.ini with the same keys SnowLauncher uses, so both launchers (and the
// EXVS Overlay) share them. On Play the game files are written the way SnowLauncher does it:
// config.ini (Client), lmnfig.ini (Live Monitor) and card.ini, then run_iboost_*_mode_v4.exe is started.
// Launcher-only options live in EXVSLauncher.ini.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>

#include "webview/webview.h"
#include "json.hpp"
#include "ui_html.h"
#include "overlay_version.h"   // OVERLAY_BUNDLED_VERSION, generated from ../EXVSOverlay/src/menu.h

using json = nlohmann::json;
static const char *VERSION = "1.0.0";

// ---------------------------------------------------------------------------------------------
// strings / paths
// ---------------------------------------------------------------------------------------------
static std::wstring widen(const std::string &s, UINT cp = CP_UTF8) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0); MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
static std::string narrow(const std::wstring &w, UINT cp = CP_UTF8) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0); WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL); return s;
}
static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static bool truthy(const std::string &s) { std::string t = trim(s); return _stricmp(t.c_str(), "true") == 0 || t == "1"; }
static const char *tf(bool b) { return b ? "True" : "False"; }

static std::wstring g_dir;   // game folder, with trailing backslash
static std::wstring path_of(const wchar_t *name) { return g_dir + name; }
static bool exists(const std::wstring &p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void log_line(const char *fmt, ...) {
    FILE *f = _wfopen(path_of(L"EXVSLauncher.log").c_str(), L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

// ---------------------------------------------------------------------------------------------
// INI files. The game's files are ANSI, so values go through the system code page; the UI uses UTF-8.
// ---------------------------------------------------------------------------------------------
struct Ini {
    std::wstring path;
    explicit Ini(const wchar_t *name) : path(path_of(name)) {}
    std::string get(const char *sec, const char *key, const char *def = "") const {
        wchar_t buf[2048];
        GetPrivateProfileStringW(widen(sec).c_str(), widen(key).c_str(), widen(def).c_str(), buf, 2048, path.c_str());
        return trim(narrow(buf));
    }
    int geti(const char *sec, const char *key, int def) const {
        std::string v = get(sec, key, "");
        return v.empty() ? def : atoi(v.c_str());
    }
    bool getb(const char *sec, const char *key, bool def) const {
        std::string v = get(sec, key, "");
        return v.empty() ? def : truthy(v);
    }
    // Written with the ANSI API so existing non-ASCII bytes in the file are preserved as they are.
    void set(const char *sec, const char *key, const std::string &val) const {
        std::string a = narrow(widen(val), CP_ACP);
        WritePrivateProfileStringA(sec, key, a.c_str(), narrow(path, CP_ACP).c_str());
    }
    void del(const char *sec, const char *key) const {
        WritePrivateProfileStringA(sec, key, NULL, narrow(path, CP_ACP).c_str());
    }
};

// ---------------------------------------------------------------------------------------------
// Constants shared with SnowLauncher
// ---------------------------------------------------------------------------------------------
static const char *RES_NAMES[] = { "4k", "2k", "1080p", "720p", "480p", "240p", "144p" };
static const char *SKIP_NAMES[] = { "None", "SkipReminder", "SkipBrand", "SkipAll" };
struct WinSize { int w, h; };
static const WinSize WIN_SIZES[] = {   // SnowLauncher window list; 0/1 are borderless fullscreen
    {0, 0}, {0, 0}, {3840, 2160}, {3440, 1440}, {3200, 1800}, {2560, 1440}, {2560, 1080}, {2048, 1536}, {1920, 1200},
    {1920, 1080}, {1920, 823}, {1792, 768}, {1680, 1050}, {1680, 945}, {1600, 1200}, {1440, 900}, {1440, 810},
    {1400, 1050}, {1280, 1024}, {1280, 960}, {1280, 800}, {1280, 720}, {800, 640}, {800, 600}, {640, 480}, {320, 240} };
static const int WIN_COUNT = sizeof(WIN_SIZES) / sizeof(WIN_SIZES[0]);
static const char *KB_KEYS[] = { "Enabled", "Up", "Down", "Left", "Right", "ButtonA", "ButtonB", "ButtonC", "ButtonD", "ButtonAB",
                                 "ButtonBC", "ButtonAC", "ButtonABC", "Coin", "Card", "Kill", "Start", "Test" };
static const char *PAD_KEYS[] = { "Enabled", "Name", "ButtonA", "ButtonB", "ButtonC", "ButtonD", "ButtonAB", "ButtonBC", "ButtonAC",
                                  "ButtonABC", "Coin", "Card", "Start", "Test", "Service", "Kill", "KillBool", "TestBool", "ServiceBool" };
static const wchar_t *CLIENT_EXE = L"vsac29_Release.exe", *LM_EXE = L"vsac29_Release_LM.exe";

// ---------------------------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------------------------
static std::vector<DWORD> find_procs(const wchar_t *exe, bool in_game_dir = false) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe = { sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, exe) != 0) continue;
        if (in_game_dir) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            wchar_t p[MAX_PATH]; DWORD n = MAX_PATH; bool mine = false;
            if (h && QueryFullProcessImageNameW(h, 0, p, &n)) mine = _wcsnicmp(p, g_dir.c_str(), g_dir.size()) == 0;
            if (h) CloseHandle(h);
            if (!mine) continue;
        }
        out.push_back(pe.th32ProcessID);
    }
    CloseHandle(snap);
    return out;
}
static void kill_procs(const wchar_t *exe, bool in_game_dir) {
    for (DWORD pid : find_procs(exe, in_game_dir)) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
}
static bool start_process(const std::wstring &exe, const std::wstring &args, const std::wstring &cwd, DWORD flags = 0, PROCESS_INFORMATION *out = NULL) {
    std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, flags, NULL, cwd.c_str(), &si, &pi)) {
        log_line("start failed: %s (%lu)", narrow(cmd).c_str(), GetLastError());
        return false;
    }
    log_line("started: %s", narrow(cmd).c_str());
    if (out) *out = pi; else { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return true;
}
static DWORD run_hidden_wait(const std::wstring &cmdline, DWORD timeout_ms) {
    std::wstring cmd = cmdline;
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dir.c_str(), &si, &pi)) return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return code;
}

// ---------------------------------------------------------------------------------------------
// Status shared with the UI
// ---------------------------------------------------------------------------------------------
static std::mutex g_mx;
static std::string g_busy;
static std::vector<std::string> g_log;          // N2N log lines
static std::atomic<bool> g_n2n_running{false}, g_n2n_connected{false};
static std::string g_n2n_ip;
static HANDLE g_edge_proc;
static void set_busy(const std::string &s) { std::lock_guard<std::mutex> l(g_mx); g_busy = s; }
static void n2n_log(const std::string &s) {
    std::lock_guard<std::mutex> l(g_mx);
    g_log.push_back(s);
    if (g_log.size() > 2000) g_log.erase(g_log.begin(), g_log.begin() + 500);
}

// ---------------------------------------------------------------------------------------------
// State <-> ini
// ---------------------------------------------------------------------------------------------
static std::vector<std::string> list_interfaces() {
    std::vector<std::string> out;
    ULONG sz = 32768; std::vector<char> buf(sz);
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL,
                             (IP_ADAPTER_ADDRESSES *)buf.data(), &sz) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(sz);
        if (GetAdaptersAddresses(AF_INET, 0, NULL, (IP_ADAPTER_ADDRESSES *)buf.data(), &sz) != NO_ERROR) return out;
    }
    for (auto *a = (IP_ADAPTER_ADDRESSES *)buf.data(); a; a = a->Next)
        if (a->IfType != IF_TYPE_SOFTWARE_LOOPBACK) out.push_back(narrow(a->FriendlyName));
    return out;
}

static std::string base64(const std::string &in) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16; if (i + 1 < in.size()) v |= (unsigned char)in[i + 1] << 8;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += i + 1 < in.size() ? t[(v >> 6) & 63] : '='; out += '=';
    }
    return out;
}
static json image_data_url(const std::wstring &p) {
    FILE *f = _wfopen(p.c_str(), L"rb");
    if (!f) return nullptr;
    std::string data; char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    std::wstring ext = p.substr(p.find_last_of(L'.') + 1);
    for (auto &c : ext) c = towlower(c);
    const char *mime = ext == L"png" ? "image/png" : ext == L"webp" ? "image/webp" : "image/jpeg";
    return std::string("data:") + mime + ";base64," + base64(data);
}

#include "update.h"   // self-update from the GitHub releases

static bool overlay_installed() { return exists(path_of(L"xinput9_1_0.dll")); }

// Version of an overlay DLL from its "EXVSOverlayVersion=x.y.z;" marker (0.13.0 and later), else "".
static std::string overlay_file_version(const char *data, size_t n) {
    static const char tag[] = "EXVSOverlayVersion=";
    const char *end = data + n, *p = std::search(data, end, tag, tag + sizeof(tag) - 1);
    if (p == end) return "";
    p += sizeof(tag) - 1;
    const char *q = p; while (q < end && q - p < 24 && *q != ';' && *q) q++;
    return std::string(p, q);
}

// "missing", "current" (same bytes or same version as the overlay inside this launcher), "outdated",
// or "newer" (the overlay updated itself past the bundled one: nothing to offer)
static const char *overlay_status() {
    std::wstring p = path_of(L"xinput9_1_0.dll");
    if (!exists(p)) return "missing";
    HRSRC r = FindResourceW(NULL, L"OVERLAY_DLL", MAKEINTRESOURCEW(10));
    if (!r) return "current";
    DWORD size = SizeofResource(NULL, r);
    const char *data = (const char *)LockResource(LoadResource(NULL, r));
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return "current";
    LARGE_INTEGER fs; GetFileSizeEx(f, &fs);
    std::vector<char> buf(fs.QuadPart > 0 && fs.QuadPart < (64ll << 20) ? (size_t)fs.QuadPart : 0); DWORD rd = 0;
    bool read = !buf.empty() && ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, NULL) && rd == buf.size();
    CloseHandle(f);
    if (!read) return "outdated";
    if (buf.size() == size && memcmp(buf.data(), data, size) == 0) return "current";
    std::string installed = overlay_file_version(buf.data(), buf.size());
    if (installed.empty()) return "outdated";   // 0.12.0 or older
    int c = cmp_version(installed, OVERLAY_BUNDLED_VERSION);
    return c > 0 ? "newer" : c == 0 ? "current" : "outdated";
}

static json get_state() {
    Ini sl(L"SnowLauncher.ini"), cfg(L"config.ini"), card(L"card.ini"), me(L"EXVSLauncher.ini");
    bool have_sl = exists(sl.path);
    json s;
    s["version"] = VERSION;
    s["gameDir"] = narrow(g_dir.substr(0, g_dir.size() - 1));
    s["files"] = {
        {"client", exists(path_of(CLIENT_EXE))}, {"lm", exists(path_of(LM_EXE))}, {"edge", exists(path_of(L"edge.exe"))},
        {"server", exists(path_of(L"Server\\Server.exe")) || exists(path_of(L"Server.exe"))},
        {"modManager", exists(path_of(L"EXVSIB_Mod_Manager.exe"))}, {"overlay", overlay_installed()}, {"overlayStatus", overlay_status()}, {"overlayVersion", OVERLAY_BUNDLED_VERSION},
        {"snowLauncherIni", have_sl} };

    // card: SnowLauncher keeps it in Game_Config, the game reads card.ini
    std::string chip = sl.get("Game_Config", "ChipId", card.get("card", "chipId").c_str());
    std::string code = sl.get("Game_Config", "AccessCode", card.get("card", "accessCode").c_str());
    int region_idx = sl.geti("Game_Config", "Region", cfg.geti("config", "Region", 40) - 1);
    json c = {
        {"chipId", chip}, {"accessCode", code},
        {"serial", sl.get("Game_Config", "Serial", cfg.get("config", "serial").c_str())},
        {"server", sl.get("Game_Config", "Server", cfg.get("config", "Server").c_str())},
        {"region", region_idx + 1},
        {"openingSkip", sl.geti("Game_Config", "OpeningScreenSkip", 3)},
        {"bootToken", sl.get("Game_Config", "BootToken", cfg.get("config", "BootToken").c_str())},
        {"disableSocketHook", sl.getb("Game_Config", "DisableSocketHook", false)},
        {"enableQuickSkinChange", sl.getb("Game_Config", "EnableQuickSkinChange", true)},
        {"framerateLimit", sl.getb("Game_Config", "FramerateLimit", true)},
        {"useRealCardReader", sl.getb("Game_Config", "UseRealCardReader", false)},
        {"cardReaderComPort", sl.get("Game_Config", "CardReaderComPort")},
        {"client", { {"window", sl.geti("AppSettings", "ClientWindow", 0)}, {"resolution", sl.geti("Game_Config", "PCB_Resolution", 2)},
                     {"iface", sl.get("Game_Config", "PCB_InterfaceName", cfg.get("config", "InterfaceName").c_str())},
                     {"ipRun", sl.getb("Game_Config", "PCB_IpAddressRun", false)}, {"ip", sl.get("Game_Config", "PCB_IpAddress")} }},
        {"lm", { {"window", sl.geti("AppSettings", "LMWindow", 9)}, {"resolution", sl.geti("Game_Config", "LM_Resolution", 2)},
                 {"iface", sl.get("Game_Config", "LM_InterfaceName")},
                 {"ipRun", sl.getb("Game_Config", "LM_IpAddressRun", false)}, {"ip", sl.get("Game_Config", "LM_IpAddress")} }},
    };
    s["cfg"] = c;

    json kb = json::object();
    const char *kb_secs[][2] = { {"client", "Game_Client_Keyboard"}, {"lm", "Game_LM_Keyboard"} };
    for (auto &ks : kb_secs) {
        json k = json::object();
        for (const char *key : KB_KEYS) k[key] = sl.get(ks[1], key);
        if (k["Enabled"].get<std::string>().empty()) k["Enabled"] = "True";
        kb[ks[0]] = k;
    }
    s["keyboard"] = kb;
    json pads = json::array();
    const char *layout_keys[] = { "Pad1Layout", "Pad2Layout" };
    int pad_i = 0;
    for (const char *sec : { "Game_Controller", "Game_Controller_1" }) {
        json p = json::object();
        for (const char *key : PAD_KEYS) p[key] = sl.get(sec, key);
        p["Layout"] = me.get("Controller", layout_keys[pad_i++]);   // "" auto, "native", "custom"
        pads.push_back(p);
    }
    s["controller"] = pads;

    s["n2n"] = {
        {"host", sl.get("AppSettings", "N2N_Host")}, {"community", sl.get("AppSettings", "N2N_Community")},
        {"playerName", sl.get("AppSettings", "N2N_PlayerName", chip.c_str())}, {"virtualIp", sl.get("AppSettings", "N2N_VirtualIp")},
        {"dhcp", sl.getb("AppSettings", "N2N_DHCP", true)}, {"relay", sl.getb("AppSettings", "N2N_Relay", false)},
        {"noPortForwarding", sl.getb("AppSettings", "N2N_NoPortForwarding", false)}, {"multicast", sl.getb("AppSettings", "N2N_MulticastMAC", true)},
        {"iface", me.get("N2N", "Interface", "N2N")}, {"conf", sl.get("AppSettings", "N2N_Conf")} };

    bool sc = sl.getb("AppSettings", "StartClient", true), slm = sl.getb("AppSettings", "StartLM", false);
    s["app"] = { {"mode", sc && slm ? "both" : slm ? "lm" : "client"},
                 {"syncMods", me.getb("Launcher", "SyncMods", false)}, {"closeOnPlay", me.getb("Launcher", "CloseOnPlay", false)},
                 {"streamer", me.getb("Launcher", "StreamerMode", false)},
                 {"lang", me.get("Launcher", "Language")},   // "" = follow the Windows language
                 {"autoUpdate", me.getb("Launcher", "CheckUpdates", true)}, {"updatesEnabled", updates_enabled()} };
    s["interfaces"] = list_interfaces();
    {   // main monitor, to recommend a resolution
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY), &mi);
        s["monitor"] = { {"w", mi.rcMonitor.right - mi.rcMonitor.left}, {"h", mi.rcMonitor.bottom - mi.rcMonitor.top} };
    }

    std::string img = sl.get("AppSettings", "ImagePath");
    s["background"] = nullptr;
    if (!img.empty()) {
        std::wstring p = widen(img);
        if (p.size() < 2 || p[1] != L':') p = g_dir + p;
        if (!exists(p)) {   // SnowLauncher keeps the original extension when it copies the picture
            for (const wchar_t *e : { L".jpg", L".jpeg", L".png", L".webp" }) {
                std::wstring q = p.substr(0, p.find_last_of(L'.')) + e;
                if (exists(q)) { p = q; break; }
            }
        }
        if (exists(p) && p.find(L".mp4") == std::wstring::npos) s["background"] = image_data_url(p);
    }
    return s;
}

static std::string js(const json &o, const char *k, const char *def = "") {
    if (!o.contains(k) || o[k].is_null()) return def;
    if (o[k].is_string()) return o[k].get<std::string>();
    if (o[k].is_boolean()) return tf(o[k].get<bool>());
    return o[k].dump();
}
static int ji(const json &o, const char *k, int def) {
    if (!o.contains(k)) return def;
    if (o[k].is_number()) return o[k].get<int>();
    if (o[k].is_string()) return atoi(o[k].get<std::string>().c_str());
    return def;
}
static bool jb(const json &o, const char *k, bool def) {
    if (!o.contains(k)) return def;
    if (o[k].is_boolean()) return o[k].get<bool>();
    if (o[k].is_string()) return truthy(o[k].get<std::string>());
    return def;
}

// don't add empty keys that weren't in the file (keeps SnowLauncher.ini tidy)
static void set_if_needed(const Ini &ini, const char *sec, const char *key, const std::string &v) {
    if (v.empty() && ini.get(sec, key).empty()) return;
    ini.set(sec, key, v);
}

static void save_state(const json &s) {
    Ini sl(L"SnowLauncher.ini"), card(L"card.ini"), me(L"EXVSLauncher.ini");
    const json &c = s.at("cfg");
    sl.set("Game_Config", "ChipId", js(c, "chipId"));
    sl.set("Game_Config", "AccessCode", js(c, "accessCode"));
    sl.set("Game_Config", "Serial", js(c, "serial"));
    sl.set("Game_Config", "Server", js(c, "server"));
    sl.set("Game_Config", "Region", std::to_string(ji(c, "region", 40) - 1));
    sl.set("Game_Config", "OpeningScreenSkip", std::to_string(ji(c, "openingSkip", 3)));
    sl.set("Game_Config", "BootToken", js(c, "bootToken"));
    sl.set("Game_Config", "DisableSocketHook", tf(jb(c, "disableSocketHook", false)));
    sl.set("Game_Config", "EnableQuickSkinChange", tf(jb(c, "enableQuickSkinChange", true)));
    sl.set("Game_Config", "FramerateLimit", tf(jb(c, "framerateLimit", true)));
    sl.set("Game_Config", "UseRealCardReader", tf(jb(c, "useRealCardReader", false)));
    sl.set("Game_Config", "CardReaderComPort", js(c, "cardReaderComPort"));
    const json &cl = c.at("client"), &lm = c.at("lm");
    sl.set("AppSettings", "ClientWindow", std::to_string(ji(cl, "window", 0)));
    sl.set("AppSettings", "LMWindow", std::to_string(ji(lm, "window", 9)));
    sl.set("Game_Config", "PCB_Resolution", std::to_string(ji(cl, "resolution", 2)));
    sl.set("Game_Config", "LM_Resolution", std::to_string(ji(lm, "resolution", 2)));
    sl.set("Game_Config", "PCB_InterfaceName", js(cl, "iface"));
    sl.set("Game_Config", "LM_InterfaceName", js(lm, "iface"));
    sl.set("Game_Config", "PCB_IpAddressRun", tf(jb(cl, "ipRun", false)));
    sl.set("Game_Config", "LM_IpAddressRun", tf(jb(lm, "ipRun", false)));
    sl.set("Game_Config", "PCB_IpAddress", js(cl, "ip"));
    sl.set("Game_Config", "LM_IpAddress", js(lm, "ip"));

    const char *kb_secs[][2] = { {"client", "Game_Client_Keyboard"}, {"lm", "Game_LM_Keyboard"} };
    for (auto &ks : kb_secs) {
        if (!s["keyboard"].contains(ks[0])) continue;
        const json &k = s["keyboard"][ks[0]];
        for (const char *key : KB_KEYS) if (k.contains(key)) set_if_needed(sl, ks[1], key, js(k, key));
    }
    const char *pad_secs[] = { "Game_Controller", "Game_Controller_1" };
    for (int i = 0; i < 2 && i < (int)s["controller"].size(); i++) {
        const json &p = s["controller"][i];
        for (const char *key : PAD_KEYS) if (p.contains(key)) set_if_needed(sl, pad_secs[i], key, js(p, key));
        if (p.contains("Layout")) me.set("Controller", i == 0 ? "Pad1Layout" : "Pad2Layout", js(p, "Layout"));
    }

    const json &n = s.at("n2n");
    sl.set("AppSettings", "N2N_Host", js(n, "host"));
    sl.set("AppSettings", "N2N_Community", js(n, "community"));
    sl.set("AppSettings", "N2N_PlayerName", js(n, "playerName"));
    sl.set("AppSettings", "N2N_VirtualIp", js(n, "virtualIp"));
    sl.set("AppSettings", "N2N_DHCP", tf(jb(n, "dhcp", true)));
    sl.set("AppSettings", "N2N_Relay", tf(jb(n, "relay", false)));
    sl.set("AppSettings", "N2N_NoPortForwarding", tf(jb(n, "noPortForwarding", false)));
    sl.set("AppSettings", "N2N_MulticastMAC", tf(jb(n, "multicast", true)));
    sl.set("AppSettings", "N2N_Conf", js(n, "conf"));
    me.set("N2N", "Interface", js(n, "iface"));

    const json &a = s.at("app");
    std::string mode = js(a, "mode", "client");
    sl.set("AppSettings", "StartClient", tf(mode != "lm"));
    sl.set("AppSettings", "StartLM", tf(mode != "client"));
    me.set("Launcher", "SyncMods", tf(jb(a, "syncMods", false)));
    me.set("Launcher", "CloseOnPlay", tf(jb(a, "closeOnPlay", false)));
    me.set("Launcher", "StreamerMode", tf(jb(a, "streamer", false)));
    if (!js(a, "lang").empty()) me.set("Launcher", "Language", js(a, "lang"));
    me.set("Launcher", "CheckUpdates", tf(jb(a, "autoUpdate", true)));
}

// ---------------------------------------------------------------------------------------------
// Writing the game's own files (what SnowLauncher does on Start)
// ---------------------------------------------------------------------------------------------
static std::string join_keys(std::initializer_list<std::string> parts) {
    std::string out;
    for (const auto &p : parts) {
        std::stringstream ss(p); std::string item;
        while (std::getline(ss, item, ',')) {
            item = trim(item);
            if (item.empty()) continue;
            if (("," + out + ",").find("," + item + ",") != std::string::npos) continue;
            out += (out.empty() ? "" : ",") + item;
        }
    }
    return out;
}
static void set_or_del(const Ini &ini, const char *sec, const char *key, const std::string &v) {
    if (v.empty()) ini.del(sec, key); else ini.set(sec, key, v);
}
static void write_buttons(const Ini &ini, const char *sec, const json &k, bool combos) {
    auto g = [&](const char *key) { return k.contains(key) ? js(k, key) : std::string(); };
    if (combos) {
        set_or_del(ini, sec, "A", join_keys({ g("ButtonA"), g("ButtonAB"), g("ButtonAC"), g("ButtonABC") }));
        set_or_del(ini, sec, "B", join_keys({ g("ButtonB"), g("ButtonAB"), g("ButtonBC"), g("ButtonABC") }));
        set_or_del(ini, sec, "C", join_keys({ g("ButtonC"), g("ButtonAC"), g("ButtonBC"), g("ButtonABC") }));   // SnowLauncher's order
    } else {
        set_or_del(ini, sec, "A", g("ButtonA")); set_or_del(ini, sec, "B", g("ButtonB")); set_or_del(ini, sec, "C", g("ButtonC"));
    }
    set_or_del(ini, sec, "D", g("ButtonD"));
    for (const char *key : { "Start", "Coin", "Card" }) set_or_del(ini, sec, key, join_keys({ g(key) }));
}

// Same test as SnowLauncher: these names use the game's native XInput support.
static bool pad_is_xinput(std::string name) {
    for (auto &ch : name) ch = (char)tolower((unsigned char)ch);
    return name.find("xbox") != std::string::npos || name.find("x-box") != std::string::npos || name.find("xinput") != std::string::npos;
}

static void write_game_files(const json &s) {
    const json &c = s.at("cfg");
    bool lm_side[] = { false, true };
    for (bool lm : lm_side) {
        Ini ini(lm ? L"lmnfig.ini" : L"config.ini");
        if (lm && !exists(ini.path) && !exists(path_of(LM_EXE))) continue;
        const json &side = c.at(lm ? "lm" : "client");
        int win = ji(side, "window", lm ? 9 : 0);
        ini.set("config", lm ? "Mode" : "mode", lm ? "LM" : "Client");
        ini.set("config", "Server", js(c, "server"));
        ini.set("config", "Windowed", "True");
        // Like SnowLauncher: the loader's own borderless mode doesn't give a real fullscreen window, so the game
        // starts windowed and the window is sized afterwards (by the EXVS Overlay, or by watch_window below).
        (void)win;
        ini.set("config", "BorderlessWindow", "False");
        if (!lm) ini.set("config", "DisableSocketHook", tf(jb(c, "disableSocketHook", false)));   // the LM keeps its own (True)
        int skip = ji(c, "openingSkip", 3); if (skip < 0 || skip > 3) skip = 3;
        ini.set("config", "OpeningScreenSkip", SKIP_NAMES[skip]);
        ini.set("config", "BootToken", js(c, "bootToken"));
        ini.set("config", "InterfaceName", js(side, "iface"));
        if (jb(side, "ipRun", false) && !js(side, "ip").empty()) ini.set("config", "IpAddress", js(side, "ip"));
        else ini.del("config", "IpAddress");
        if (!lm) {
            ini.set("config", "serial", js(c, "serial"));
            ini.set("config", "Region", std::to_string(ji(c, "region", 40)));
            ini.set("config", "CardReaderComPort", js(c, "cardReaderComPort"));
            ini.set("config", "UseRealCardReader", tf(jb(c, "useRealCardReader", false)));
            ini.set("config", "EnableQuickSkinChange", tf(jb(c, "enableQuickSkinChange", true)));
        }
        int res = ji(side, "resolution", 2); if (res < 0 || res > 6) res = 2;
        ini.set("display", "Resolution", RES_NAMES[res]);
        ini.set("display", "FramerateLimit", tf(jb(c, "framerateLimit", true)));

        const json &k = s["keyboard"][lm ? "lm" : "client"];
        ini.set("keyboard", "Enabled", tf(truthy(js(k, "Enabled", "True"))));
        for (const char *key : { "Up", "Down", "Left", "Right", "Kill", "Test" }) set_or_del(ini, "keyboard", key, js(k, key));
        write_buttons(ini, "keyboard", k, !lm);

        if (!lm && s["controller"].size() >= 2) {
            // [controller] = Controller 1, [controller-1] = Controller 2 (the loader, bngrw.dll, reads both the same way).
            // Xbox / XInput pads: Mode = xinput-native, the game reads the pad itself with its own layout (like SnowLauncher),
            // unless the player picked "Custom buttons" (Layout = custom): then, like any other pad, it goes through
            // DirectInput with the button numbers mapped in the launcher.
            const char *pad_sec[] = { "controller", "controller-1" };
            for (int i = 0; i < 2; i++) {
                const json &p = s["controller"][i];
                const char *sec = pad_sec[i];
                std::string name = js(p, "Name");
                ini.set(sec, "Enabled", tf(truthy(js(p, "Enabled"))));
                if (!name.empty()) ini.set(sec, "Name", name);
                std::string layout = js(p, "Layout");
                bool native = layout == "native" || (layout != "custom" && pad_is_xinput(name));
                if (native) ini.set(sec, "Mode", "xinput-native");
                else ini.del(sec, "Mode");
                write_buttons(ini, sec, p, true);
                const char *opt[][2] = { {"Test", "TestBool"}, {"Service", "ServiceBool"}, {"Kill", "KillBool"} };
                for (auto &o : opt) {
                    if (truthy(js(p, o[1])) && !js(p, o[0]).empty()) ini.set(sec, o[0], join_keys({ js(p, o[0]) }));
                    else ini.del(sec, o[0]);
                }
            }
        }
    }
    Ini card(L"card.ini");
    card.set("card", "chipId", js(c, "chipId"));
    card.set("card", "accessCode", js(c, "accessCode"));
}

// ---------------------------------------------------------------------------------------------
// Window placement (for players without the EXVS Overlay, which does this itself)
// ---------------------------------------------------------------------------------------------
struct FindWin { DWORD pid; HWND best; int area; };
static BOOL CALLBACK find_win_cb(HWND h, LPARAM lp) {
    FindWin *f = (FindWin *)lp; DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r; GetWindowRect(h, &r); int a = (r.right - r.left) * (r.bottom - r.top);
    if (a > f->area) { f->area = a; f->best = h; }
    return TRUE;
}
static void place_window(HWND h, int idx) {
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    RECT rc;
    if (idx <= 1) {
        st = (st & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP;
        rc = idx == 0 ? mi.rcMonitor : mi.rcWork;
    } else {
        st = (st & ~(WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT c = { 0, 0, WIN_SIZES[idx].w, WIN_SIZES[idx].h };
        AdjustWindowRectEx(&c, (DWORD)st, FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
        int w = c.right - c.left, hh = c.bottom - c.top;
        int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2, y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - hh) / 2;
        rc = { x < mi.rcWork.left ? mi.rcWork.left : x, y < mi.rcWork.top ? mi.rcWork.top : y, 0, 0 };
        rc.right = rc.left + w; rc.bottom = rc.top + hh;
    }
    SetWindowLongPtrW(h, GWL_STYLE, st | WS_VISIBLE);
    SetWindowPos(h, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
}
static bool window_ok(HWND h, int idx) {
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr);
    if (idx <= 1) {
        RECT t = idx == 0 ? mi.rcMonitor : mi.rcWork;
        return EqualRect(&wr, &t) && cr.right == t.right - t.left && cr.bottom == t.bottom - t.top &&
               !(GetWindowLongPtrW(h, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME));
    }
    return cr.right == WIN_SIZES[idx].w && cr.bottom == WIN_SIZES[idx].h;
}
static void watch_window(const wchar_t *exe, int idx) {
    if (idx < 0 || idx >= WIN_COUNT) return;
    std::wstring e = exe;
    std::thread([e, idx] {
        for (int t = 0; t < 90; t++) {   // ~90 s after start, like the overlay's keep_window
            Sleep(1000);
            auto pids = find_procs(e.c_str(), true);
            if (pids.empty()) { if (t > 30) return; continue; }
            FindWin f = { pids[0], NULL, 0 };
            EnumWindows(find_win_cb, (LPARAM)&f);
            if (!f.best || t < 4) continue;
            static thread_local bool forced = false;   // apply once at startup, then only when it's wrong
            if (!forced) { forced = true; place_window(f.best, idx); continue; }
            if (!window_ok(f.best, idx)) place_window(f.best, idx);
        }
    }).detach();
}

// ---------------------------------------------------------------------------------------------
// Play / stop
// ---------------------------------------------------------------------------------------------
static std::atomic<bool> g_launching{false};
static bool game_running() { return !find_procs(CLIENT_EXE, true).empty() || !find_procs(LM_EXE, true).empty(); }

static json do_play(const std::string &mode, json state) {
    if (g_launching.exchange(true)) return { {"ok", false}, {"error", "Already starting"} };
    struct Done { ~Done() { g_launching = false; set_busy(""); } } done;
    bool want_client = mode != "lm", want_lm = mode != "client";
    if (want_client && !exists(path_of(CLIENT_EXE))) return { {"ok", false}, {"error", "vsac29_Release.exe not found next to the launcher"} };
    if (want_lm && !exists(path_of(LM_EXE))) return { {"ok", false}, {"error", "vsac29_Release_LM.exe not found next to the launcher"} };
    if ((want_client && !find_procs(CLIENT_EXE, true).empty()) || (want_lm && !find_procs(LM_EXE, true).empty()))
        return { {"ok", false}, {"error", "The game is already running"} };
    for (wchar_t ch : g_dir) if (ch >= 0x2E80) return { {"ok", false}, {"error", "The game folder path has non-Latin characters; the game can't start from there"} };

    write_game_files(state);
    log_line("play: %s", mode.c_str());

    if (jb(state["app"], "syncMods", false) && exists(path_of(L"EXVSIB_Mod_Manager.exe"))) {
        set_busy("Syncing online mods…");
        std::wstring cmd = L"\"" + path_of(L"EXVSIB_Mod_Manager.exe") + L"\" --sync";
        DWORD rc = run_hidden_wait(cmd, 10 * 60 * 1000);
        log_line("mod sync exit code %lu", rc);
    }

    set_busy("Starting the game…");
    auto launch = [&](bool lm) {
        const wchar_t *helper = lm ? L"run_iboost_LM_mode_v4.exe" : L"run_iboost_CLIENT_mode_v4.exe";
        bool ok = exists(path_of(helper)) ? start_process(path_of(helper), L"", g_dir.substr(0, g_dir.size() - 1))
                                          : start_process(path_of(lm ? LM_EXE : CLIENT_EXE), L"", g_dir.substr(0, g_dir.size() - 1));
        if (ok && !overlay_installed()) watch_window(lm ? LM_EXE : CLIENT_EXE, ji(state["cfg"][lm ? "lm" : "client"], "window", lm ? 9 : 0));
        return ok;
    };
    bool ok = true;
    if (want_client) ok &= launch(false);
    if (want_lm) { if (want_client) Sleep(1500); ok &= launch(true); }
    if (!ok) return { {"ok", false}, {"error", "Could not start the game (see EXVSLauncher.log)"} };
    for (int i = 0; i < 20 && !game_running(); i++) Sleep(500);
    return { {"ok", true} };
}

static void stop_game() {
    kill_procs(CLIENT_EXE, true);
    kill_procs(LM_EXE, true);
    kill_procs(L"run_iboost_CLIENT_mode_v4.exe", true);
    kill_procs(L"run_iboost_LM_mode_v4.exe", true);
    log_line("game closed from the launcher");
}

// ---------------------------------------------------------------------------------------------
// N2N (edge.exe) and the local card server
// ---------------------------------------------------------------------------------------------
static std::wstring quote_arg(const std::string &s) {
    std::wstring w = widen(s);
    if (w.find_first_of(L" \t\"") == std::wstring::npos && !w.empty()) return w;
    std::wstring o = L"\"";
    for (wchar_t c : w) { if (c == L'"') o += L'\\'; o += c; }
    return o + L"\"";
}

static json n2n_start(const json &state) {
    if (g_n2n_running) return { {"ok", false}, {"error", "N2N is already running"} };
    const json &n = state.at("n2n");
    std::string host = trim(js(n, "host")), community = trim(js(n, "community")), name = trim(js(n, "playerName")), iface = trim(js(n, "iface"));
    if (!exists(path_of(L"edge.exe"))) return { {"ok", false}, {"error", "edge.exe is missing from the game folder"} };
    if (host.empty() || host.find(':') == std::string::npos) return { {"ok", false}, {"error", "Enter the supernode as host:port"} };
    if (community.empty()) return { {"ok", false}, {"error", "Enter the community (room) name"} };
    if (name.empty()) return { {"ok", false}, {"error", "Enter a player name"} };
    if (iface.empty()) return { {"ok", false}, {"error", "Choose the TAP adapter"} };
    bool dhcp = jb(n, "dhcp", true);

    if (dhcp) run_hidden_wait(L"netsh interface ip set address name=" + quote_arg(iface) + L" source=dhcp", 15000);
    std::wstring args = L"-l " + quote_arg(host) + L" -c " + quote_arg(community);
    if (jb(n, "relay", false)) args += L" -S1";
    if (jb(n, "noPortForwarding", false)) args += L" --no-port-forwarding";
    if (!dhcp && !trim(js(n, "virtualIp")).empty()) args += L" -a " + quote_arg(trim(js(n, "virtualIp")));
    if (jb(n, "multicast", true)) args += L" -E";
    args += L" -d " + quote_arg(iface) + L" -I " + quote_arg(name);
    std::string extra = trim(js(n, "conf"));
    if (!extra.empty()) args += L" " + widen(extra);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return { {"ok", false}, {"error", "Could not create a pipe"} };
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = L"\"" + path_of(L"edge.exe") + L"\" " + args;
    STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = NULL;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_dir.c_str(), &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        return { {"ok", false}, {"error", "Could not start edge.exe"} };
    }
    CloseHandle(wr); CloseHandle(pi.hThread);
    g_edge_proc = pi.hProcess; g_n2n_running = true; g_n2n_connected = false;
    { std::lock_guard<std::mutex> l(g_mx); g_n2n_ip.clear(); }
    n2n_log("> edge " + narrow(args));
    log_line("n2n: edge %s", narrow(args).c_str());

    std::string iface_copy = iface;
    std::thread([rd, iface_copy] {
        std::string acc; char buf[1024]; DWORD n;
        while (ReadFile(rd, buf, sizeof(buf), &n, NULL) && n) {
            acc.append(buf, n);
            size_t p;
            while ((p = acc.find('\n')) != std::string::npos) {
                std::string line = trim(acc.substr(0, p)); acc.erase(0, p + 1);
                if (line.empty()) continue;
                n2n_log(line);
                size_t k = line.find("created local tap device IP:");
                if (k != std::string::npos) { std::lock_guard<std::mutex> l(g_mx); g_n2n_ip = trim(line.substr(k + 28)); g_n2n_ip = g_n2n_ip.substr(0, g_n2n_ip.find_first_of(" ,")); }
                if (line.find("[OK] edge <<< ================ >>> supernode") != std::string::npos ||
                    line.find("edge <<< ================ >>> supernode") != std::string::npos) {
                    if (!g_n2n_connected.exchange(true)) {
                        // prefer the N2N adapter for game traffic (SnowLauncher sets its metric to 1)
                        std::wstring ps = L"powershell -NoProfile -Command \"Set-NetIPInterface -InterfaceAlias '" + widen(iface_copy) +
                                          L"' -AddressFamily IPv4 -InterfaceMetric 1\"";
                        run_hidden_wait(ps, 15000);
                    }
                }
            }
        }
        CloseHandle(rd);
        g_n2n_running = false; g_n2n_connected = false;
        n2n_log("[Disconnected]");
    }).detach();
    return { {"ok", true} };
}
static void n2n_stop() {
    if (g_edge_proc) { TerminateProcess(g_edge_proc, 0); CloseHandle(g_edge_proc); g_edge_proc = NULL; }
    kill_procs(L"edge.exe", true);
}

static std::wstring server_exe() {
    if (exists(path_of(L"Server\\Server.exe"))) return path_of(L"Server\\Server.exe");
    if (exists(path_of(L"Server.exe"))) return path_of(L"Server.exe");
    return L"";
}
static bool server_running() {
    std::wstring s = server_exe();
    return !s.empty() && !find_procs(L"Server.exe", true).empty();
}

// ---------------------------------------------------------------------------------------------
// Controllers (DirectInput): list devices and capture a button number for the mapping UI
// ---------------------------------------------------------------------------------------------
static IDirectInput8W *g_di;
static HWND g_hwnd;
static std::atomic<bool> g_capture_cancel{false};
struct DevInfo { GUID guid; std::string name; };
static BOOL CALLBACK enum_dev_cb(LPCDIDEVICEINSTANCEW d, LPVOID ctx) {
    ((std::vector<DevInfo> *)ctx)->push_back({ d->guidInstance, narrow(d->tszProductName) });
    return DIENUM_CONTINUE;
}
static std::vector<DevInfo> list_devices() {
    std::vector<DevInfo> v;
    if (!g_di && FAILED(DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&g_di, NULL))) return v;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_dev_cb, &v, DIEDFL_ATTACHEDONLY);
    return v;
}
// Listens to every attached controller at once and returns the first button pressed, with the name of the
// controller it came from (the UI switches the device to it: the loader finds the pad by that name).
// Watching only the selected device failed when its name didn't match (e.g. a GP2040-CE in PS3 mode plugged
// in after the list was read): the first device in the list was used instead and the presses never showed up.
static json capture_button(const std::string &name, int timeout_ms) {
    auto devs = list_devices();
    if (devs.empty()) return { {"error", "No controller found"} };
    struct Open { IDirectInputDevice8W *dev; std::string name; DIJOYSTATE2 base; };
    std::vector<Open> open;
    for (auto &d : devs) {
        IDirectInputDevice8W *dev = NULL;
        HRESULT hr = g_di->CreateDevice(d.guid, &dev, NULL);
        if (FAILED(hr)) { log_line("capture: can't open '%s' (0x%08lx)", d.name.c_str(), (unsigned long)hr); continue; }
        if (FAILED(hr = dev->SetDataFormat(&c_dfDIJoystick2))) { log_line("capture: SetDataFormat '%s' failed (0x%08lx)", d.name.c_str(), (unsigned long)hr); dev->Release(); continue; }
        if (FAILED(dev->SetCooperativeLevel(g_hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
            dev->SetCooperativeLevel(NULL, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        hr = dev->Acquire();
        if (FAILED(hr)) log_line("capture: Acquire '%s' failed (0x%08lx)", d.name.c_str(), (unsigned long)hr);
        Open o = { dev, d.name, {} };
        dev->Poll(); dev->GetDeviceState(sizeof(o.base), &o.base);
        open.push_back(o);
    }
    if (open.empty()) return { {"error", "Could not open the controller"} };
    log_line("capture: listening to %zu controller(s), selected '%s'", open.size(), name.c_str());
    json result = { {"error", "No button pressed"} };
    g_capture_cancel = false;
    for (int t = 0; t < timeout_ms / 15 && !g_capture_cancel && result.contains("error"); t++) {
        Sleep(15);
        for (auto &o : open) {
            DIJOYSTATE2 st;
            if (FAILED(o.dev->Poll())) o.dev->Acquire();
            if (FAILED(o.dev->GetDeviceState(sizeof(st), &st))) continue;
            int hit = -1;
            for (int b = 0; b < 128; b++) {
                bool now = st.rgbButtons[b] & 0x80, before = o.base.rgbButtons[b] & 0x80;
                if (now && !before) { hit = b; break; }
                if (!now && before) o.base.rgbButtons[b] = 0;   // released: allow it next time
            }
            if (hit >= 0) { result = { {"button", hit + 1}, {"device", o.name} }; log_line("capture: button %d on '%s'", hit + 1, o.name.c_str()); break; }
        }
    }
    if (g_capture_cancel) result = { {"cancelled", true} };
    for (auto &o : open) { o.dev->Unacquire(); o.dev->Release(); }
    return result;
}

// ---------------------------------------------------------------------------------------------
// Overlay (embedded EXVS Overlay DLL) and background picture
// ---------------------------------------------------------------------------------------------
static json install_overlay() {
    HRSRC r = FindResourceW(NULL, L"OVERLAY_DLL", MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return { {"ok", false}, {"error", "This launcher build doesn't include the overlay"} };
    HGLOBAL g = LoadResource(NULL, r); DWORD size = SizeofResource(NULL, r);
    const void *data = LockResource(g);
    std::wstring dst = path_of(L"xinput9_1_0.dll");
    if (exists(dst)) {   // in use by a running game: move it aside, the overlay deletes the .old file on next start
        DeleteFileW(path_of(L"xinput9_1_0.old.dll").c_str());
        if (!DeleteFileW(dst.c_str())) MoveFileW(dst.c_str(), path_of(L"xinput9_1_0.old.dll").c_str());
    }
    HANDLE f = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return { {"ok", false}, {"error", "Could not write xinput9_1_0.dll"} };
    DWORD w; WriteFile(f, data, size, &w, NULL); CloseHandle(f);
    log_line("overlay installed (%lu bytes)", size);
    return { {"ok", w == size} };
}
static json remove_overlay() {
    std::wstring dst = path_of(L"xinput9_1_0.dll");
    if (!DeleteFileW(dst.c_str())) {
        DeleteFileW(path_of(L"xinput9_1_0.old.dll").c_str());
        if (!MoveFileW(dst.c_str(), path_of(L"xinput9_1_0.old.dll").c_str())) return { {"ok", false}, {"error", "Could not remove xinput9_1_0.dll"} };
    }
    log_line("overlay removed");
    return { {"ok", true} };
}
static json pick_background() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = { sizeof(of) };
    of.hwndOwner = g_hwnd; of.lpstrFile = file; of.nMaxFile = MAX_PATH;
    of.lpstrFilter = L"Images (*.jpg;*.jpeg;*.png;*.webp)\0*.jpg;*.jpeg;*.png;*.webp\0";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return { {"cancelled", true} };
    std::wstring src = file, ext = src.substr(src.find_last_of(L'.'));
    std::wstring dst = path_of((L"LauncherImage" + ext).c_str());
    if (_wcsicmp(src.c_str(), dst.c_str()) != 0 && !CopyFileW(src.c_str(), dst.c_str(), FALSE)) return { {"error", "Could not copy the picture"} };
    Ini(L"SnowLauncher.ini").set("AppSettings", "ImagePath", narrow(L"LauncherImage" + ext));
    return { {"dataUrl", image_data_url(dst)} };
}

// ---------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------
static void init_dir(const wchar_t *override_dir) {
    wchar_t p[MAX_PATH];
    if (override_dir) wcsncpy(p, override_dir, MAX_PATH - 1), p[MAX_PATH - 1] = 0;
    else { GetModuleFileNameW(NULL, p, MAX_PATH); *wcsrchr(p, L'\\') = 0; }
    g_dir = p;
    if (g_dir.back() != L'\\') g_dir += L'\\';
}

static int selftest(const wchar_t *dir, const wchar_t *out) {
    // --selftest <game dir> <out.json>: exercise state/ini logic on a copy of a game folder, no window
    init_dir(dir);
    json r;
    json st = get_state();
    st["background"] = st["background"].is_null() ? json(nullptr) : json("<data url>");
    r["state"] = st;
    save_state(get_state());
    write_game_files(get_state());
    r["devices"] = json::array();
    for (auto &d : list_devices()) r["devices"].push_back(d.name);
    r["running"] = game_running();
    FILE *f = _wfopen(out, L"wb");
    if (!f) return 2;
    std::string txt = r.dump(2); fwrite(txt.data(), 1, txt.size(), f); fclose(f);
    return 0;
}

static HANDLE g_mtx;   // single instance

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 4 && wcscmp(argv[1], L"--selftest") == 0) return selftest(argv[2], argv[3]);
    if (argc >= 3 && wcscmp(argv[1], L"--update-test") == 0) {   // --update-test <out.json> [install]: check (+ download and swap), no window
        init_dir(NULL);
        json r = { {"check", check_update()} };
        if (argc >= 4 && r["check"].value("available", false)) r["install"] = install_update();
        FILE *f = _wfopen(argv[2], L"wb"); if (!f) return 2;
        std::string t = r.dump(2); fwrite(t.data(), 1, t.size(), f); fclose(f);
        return 0;
    }
    // --uitest <game dir> <out.json>: load the real UI in a hidden window, exercise it, write a report
    bool uitest = argc >= 4 && wcscmp(argv[1], L"--uitest") == 0;
    std::wstring uitest_out = uitest ? argv[3] : L"";
    init_dir(uitest || (argc >= 3 && wcscmp(argv[1], L"--game-dir") == 0) ? argv[2] : NULL);

    g_mtx = uitest ? NULL : CreateMutexW(NULL, TRUE, L"EXVSLauncher.SingleInstance");
    if (!uitest && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(NULL, L"EXVS Launcher");
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    log_line("---- EXVS Launcher %s started in %s", VERSION, narrow(g_dir).c_str());
    if (!uitest) cleanup_update_files();

    try {
        HWND test_parent = NULL;
        if (uitest) {   // a window that is never shown; webview doesn't show windows it doesn't own
            WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"EXVSLauncherTest";
            RegisterClassW(&wc);
            test_parent = CreateWindowW(wc.lpszClassName, L"EXVS Launcher test", WS_OVERLAPPEDWINDOW, 0, 0, 1100, 680, NULL, NULL, wc.hInstance, NULL);
        }
        webview::webview w(false, uitest ? (void *)&test_parent : nullptr);
        w.set_title("EXVS Launcher");
        w.set_size(1100, 680, WEBVIEW_HINT_NONE);
        if (!uitest) w.set_size(960, 600, WEBVIEW_HINT_MIN);
        g_hwnd = (HWND)w.window().value();
        if (uitest) {
            w.bind("__report", [&](const std::string &req) {
                json a = json::parse(req, nullptr, false);
                FILE *f = _wfopen(uitest_out.c_str(), L"wb");
                if (f) { std::string t = (a.is_array() && !a.empty() ? a[0] : a).dump(2); fwrite(t.data(), 1, t.size(), f); fclose(f); }
                w.dispatch([&w] { w.terminate(); });
                return std::string("{}");
            });
            w.init("window.__errors = []; window.addEventListener('error', e => __errors.push(String(e.message)));"
                   "window.addEventListener('unhandledrejection', e => __errors.push('promise: ' + String(e.reason)));"
                   "setTimeout(async () => { const r = { errors: __errors }; try {"
                   "  r.card = document.querySelector('#q-card').textContent; r.server = document.querySelector('#q-server').textContent;"
                   "  r.display = document.querySelector('#q-display').textContent; r.res = document.querySelector('#q-res').textContent;"
                   "  r.cats = {}; for (const k of CAT_KEYS) { openCat(k); r.cats[k] = CATS[k].custom ? document.querySelectorAll('#ctrl-body .cap').length + ' keys' : rows.map(x => x.el.querySelector('.lbl').textContent + '=' + (x.el.querySelector('.val')?.childNodes[0]?.textContent || x.el.querySelector('.onoff .on')?.textContent || x.el.querySelector('input,select')?.value || '')).join(' | '); }"
                   "  r.mm = { open: !document.querySelector('#mm-open').disabled, status: document.querySelector('#mm-status').textContent };"
                   "  r.status = await getStatus(0); r.controllers = await listControllers();"
                   "  r.save = await saveState(Object.assign({}, S, {background: null, interfaces: null}));"
                   "  r.playButton = document.querySelector('#play-btn').textContent.trim(); r.hasHost = HAS_HOST;"
                   "} catch (e) { r.errors.push('test: ' + e); } __report(r); }, 4000);");
            SetTimer(NULL, 0, 30000, [](HWND, UINT, UINT_PTR, DWORD) { ExitProcess(3); });   // safety net
        }
        BOOL dark = TRUE; DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        COLORREF cap = RGB(8, 10, 17); DwmSetWindowAttribute(g_hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
        HICON icon = LoadIconW(GetModuleHandleW(NULL), L"APPICON");
        if (icon) { SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon); SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon); }
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        RECT r; GetWindowRect(g_hwnd, &r);
        SetWindowPos(g_hwnd, NULL, wa.left + ((wa.right - wa.left) - (r.right - r.left)) / 2, wa.top + ((wa.bottom - wa.top) - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        auto arg0 = [](const std::string &req) { json a = json::parse(req, nullptr, false); return a.is_array() && !a.empty() ? a[0] : json(); };
        auto async = [&w](const char *name, std::function<json(json)> fn) {
            w.bind(name, [&w, fn](const std::string &id, const std::string &req, void *) {
                json args = json::parse(req, nullptr, false);
                std::thread([&w, fn, id, args] {
                    json r;
                    try { r = fn(args); } catch (const std::exception &e) { r = { {"ok", false}, {"error", e.what()} }; }
                    w.resolve(id, 0, r.dump());
                }).detach();
            }, nullptr);
        };

        static json last_state;
        w.bind("getState", [&](const std::string &) { last_state = get_state(); return last_state.dump(); });
        w.bind("saveState", [&](const std::string &req) {
            json s = arg0(req);
            try { save_state(s); last_state = s; return json({ {"ok", true} }).dump(); }
            catch (const std::exception &e) { return json({ {"ok", false}, {"error", e.what()} }).dump(); }
        });
        w.bind("getStatus", [&](const std::string &req) {
            json a = arg0(req); size_t from = a.is_number() ? a.get<size_t>() : 0;
            std::lock_guard<std::mutex> l(g_mx);
            json log = json::array();
            for (size_t i = from; i < g_log.size(); i++) log.push_back(g_log[i]);
            json st = { {"client", !find_procs(CLIENT_EXE, true).empty()}, {"lm", !find_procs(LM_EXE, true).empty()},
                        {"server", server_running()}, {"busy", g_busy}, {"update", update_progress()}, {"log", log}, {"logTotal", g_log.size()},
                        {"n2n", { {"running", (bool)g_n2n_running}, {"connected", (bool)g_n2n_connected}, {"ip", g_n2n_ip} }} };
            return st.dump();
        });
        async("play", [&](json a) {
            std::string mode = a.is_array() && !a.empty() && a[0].is_string() ? a[0].get<std::string>() : "client";
            json r = do_play(mode, last_state);
            if (r.value("ok", false) && last_state["app"].value("closeOnPlay", false))
                std::thread([&w] { Sleep(3000); w.dispatch([&w] { w.terminate(); }); }).detach();
            return r;
        });
        async("stopGame", [](json) { stop_game(); return json({ {"ok", true} }); });
        async("n2nStart", [&](json) { return n2n_start(last_state); });
        async("n2nStop", [](json) { n2n_stop(); return json({ {"ok", true} }); });
        async("serverStart", [](json) {
            std::wstring s = server_exe();
            if (s.empty()) return json({ {"ok", false}, {"error", "Server.exe not found"} });
            if (server_running()) return json({ {"ok", true} });
            return json({ {"ok", start_process(s, L"", s.substr(0, s.find_last_of(L'\\')), CREATE_NEW_CONSOLE)} });
        });
        async("serverStop", [](json) { kill_procs(L"Server.exe", true); return json({ {"ok", true} }); });
        async("listControllers", [](json) {
            json out = json::array();
            for (auto &d : list_devices()) {
                std::string low = d.name; for (auto &c : low) c = (char)tolower((unsigned char)c);
                out.push_back({ {"name", d.name}, {"xinput", low.find("xbox") != std::string::npos || low.find("xinput") != std::string::npos} });
            }
            return out;
        });
        async("captureButton", [](json a) {
            std::string name = a.is_array() && !a.empty() && a[0].is_string() ? a[0].get<std::string>() : "";
            int timeout = a.is_array() && a.size() > 1 && a[1].is_number() ? a[1].get<int>() : 6000;
            return capture_button(name, timeout);
        });
        w.bind("cancelCapture", [](const std::string &) { g_capture_cancel = true; return std::string("{}"); });
        async("checkUpdate", [uitest](json) { return uitest ? json({ {"ok", true}, {"available", false}, {"disabled", true} }) : check_update(); });
        async("installUpdate", [&w](json) {
            if (game_running()) return json({ {"ok", false}, {"error", "Close the game before updating the launcher"} });
            json r = install_update();
            if (!r.value("ok", false)) return r;
            if (g_mtx) { ReleaseMutex(g_mtx); CloseHandle(g_mtx); g_mtx = NULL; }
            if (!start_new_launcher()) { r = { {"ok", false}, {"error", "Updated. Start EXVS Launcher again to use the new version."} }; return r; }
            std::thread([&w] { Sleep(700); w.dispatch([&w] { w.terminate(); }); }).detach();
            return r;
        });
        async("installOverlay", [](json) { return install_overlay(); });
        async("removeOverlay", [](json) { return remove_overlay(); });
        w.bind("pickBackground", [](const std::string &) { return pick_background().dump(); });
        w.bind("resetBackground", [](const std::string &) { Ini(L"SnowLauncher.ini").set("AppSettings", "ImagePath", ""); return std::string("{}"); });
        async("openModManager", [](json) {
            std::wstring mm = path_of(L"EXVSIB_Mod_Manager.exe");
            if (!exists(mm)) return json({ {"ok", false}, {"error", "EXVSIB_Mod_Manager.exe isn't in the game folder"} });
            return json({ {"ok", start_process(mm, L"", g_dir.substr(0, g_dir.size() - 1))} });
        });
        async("syncModsNow", [](json) {
            std::wstring mm = path_of(L"EXVSIB_Mod_Manager.exe");
            if (!exists(mm)) return json({ {"ok", false}, {"error", "EXVSIB_Mod_Manager.exe isn't in the game folder"} });
            if (game_running()) return json({ {"ok", false}, {"error", "Close the game before syncing mods"} });
            set_busy("Syncing online mods…");
            DWORD rc = run_hidden_wait(L"\"" + mm + L"\" --sync", 10 * 60 * 1000);
            set_busy("");
            log_line("manual mod sync exit code %lu", rc);
            return rc == 0 ? json({ {"ok", true} }) : json({ {"ok", false}, {"error", "Mod sync failed (code " + std::to_string(rc) + ")"} });
        });
        w.bind("openFolder",[](const std::string &) { ShellExecuteW(NULL, L"open", g_dir.c_str(), NULL, NULL, SW_SHOWNORMAL); return std::string("{}"); });

        w.set_html(UI_HTML);
        w.run();
    } catch (const std::exception &e) {
        log_line("fatal: %s", e.what());
        MessageBoxW(NULL, (L"EXVS Launcher could not start:\n" + widen(e.what()) +
                           L"\n\nIt needs the Microsoft Edge WebView2 Runtime (included in Windows 11).").c_str(), L"EXVS Launcher", MB_ICONERROR);
        return 1;
    }
    // keep N2N up if the game is still running (closing the launcher must not drop an online match)
    if (g_n2n_running && !game_running()) n2n_stop();
    if (g_mtx) ReleaseMutex(g_mtx);
    return 0;
}
