// Automatic updates of EXVS Launcher.exe (included by main.cpp).
//
// Builds are published as releases of UPDATE_REPO on GitHub with the bare .exe attached. When the UI
// starts it calls checkUpdate (if "Check for updates at start" is on): one request to the GitHub API
// (no token needed). If the tag is newer than VERSION the Play page offers it. installUpdate:
//     download                  -> EXVS Launcher.new.exe  (size, SHA-256 from GitHub's asset "digest", "MZ")
//     running EXVS Launcher.exe -> EXVS Launcher.old.exe  (Windows allows renaming a running exe)
//     downloaded                -> EXVS Launcher.exe, then it is started and this one closes.
// The new launcher bundles the matching EXVS Overlay, so its Play page then offers the overlay update.
// The .old.exe is deleted by the next start. EXVS_NO_UPDATE=1 turns the check off.
#pragma once
#include <winhttp.h>
#include <bcrypt.h>
#include <functional>

#ifdef EXVS_TEST_UPDATE_REPO   // test builds only
static const char *UPDATE_REPO = EXVS_TEST_UPDATE_REPO;
#define UPDATE_CURRENT "0.0.1"
#else
static const char *UPDATE_REPO = "furshow/EXVSLauncher";
#define UPDATE_CURRENT VERSION
#endif

static std::atomic<long long> g_upd_done{0}, g_upd_total{0};
static std::atomic<bool> g_upd_running{false};
static std::mutex g_upd_mx;
static json g_upd_last;   // last check result with an update (the install only uses this, never data from the UI)

static std::vector<int> parse_version(const std::string &s) {
    std::vector<int> v;
    size_t i = s.find_first_of("0123456789");
    while (i != std::string::npos && i < s.size() && isdigit((unsigned char)s[i]) && v.size() < 3) {
        v.push_back(atoi(s.c_str() + i));
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
        if (i < s.size() && s[i] == '.') i++; else break;
    }
    while (!v.empty() && v.size() < 3) v.push_back(0);
    return v;
}
static int cmp_version(const std::string &a, const std::string &b) {
    auto x = parse_version(a), y = parse_version(b);
    if (x.empty() || y.empty()) return 0;
    return x < y ? -1 : x > y ? 1 : 0;
}

// HTTPS GET with WinHTTP; follows GitHub's redirect to its download host. sink returns false to stop.
static bool https_get(const std::string &url_utf8, const wchar_t *accept, const std::function<bool(const char *, DWORD)> &sink, std::string &err) {
    std::wstring url = widen(url_utf8);
    wchar_t host[256] = L"", path[2048] = L"", extra[2048] = L"";
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { err = "bad address"; return false; }
    std::wstring full = std::wstring(path) + extra;
    std::wstring agent = L"EXVS-Launcher-updater/" + widen(VERSION);
    HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) { err = "WinHTTP is not available"; return false; }
    WinHttpSetTimeouts(s, 10000, 10000, 10000, 20000);
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0), r = NULL;
    do {
        if (!c) { err = "can't reach " + narrow(host); break; }
        r = WinHttpOpenRequest(c, L"GET", full.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!r) { err = "request failed"; break; }
        std::wstring hdr = std::wstring(L"Accept: ") + accept;
        WinHttpAddRequestHeaders(r, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) {
            err = "no answer from " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")"; break;
        }
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) {
            err = code == 404 ? "no release published yet" : code == 403 ? "GitHub limit reached, try again later" : "HTTP " + std::to_string(code);
            break;
        }
        std::vector<char> buf(1 << 16);
        ok = true;
        for (;;) {
            DWORD n = 0;
            if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { err = "download interrupted"; ok = false; break; }
            if (!n) break;
            if (!sink(buf.data(), n)) { err = "stopped"; ok = false; break; }
        }
    } while (0);
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

static bool updates_enabled() { return !GetEnvironmentVariableW(L"EXVS_NO_UPDATE", NULL, 0); }

// {ok, available, current, version, notes, size, page} or {ok:false, error}
static json check_update() {
    json out = { {"ok", true}, {"available", false}, {"current", VERSION} };
    if (!updates_enabled()) { out["disabled"] = true; return out; }
    std::string body, err;
    bool ok = https_get(std::string("https://api.github.com/repos/") + UPDATE_REPO + "/releases/latest", L"application/vnd.github+json",
                        [&](const char *d, DWORD n) { body.append(d, n); return body.size() < (8u << 20); }, err);
    if (!ok) { log_line("update check failed: %s", err.c_str()); return { {"ok", false}, {"error", "Couldn't check for updates: " + err} }; }
    json rel = json::parse(body, nullptr, false);
    if (!rel.is_object() || !rel.contains("tag_name")) return { {"ok", false}, {"error", "Couldn't check for updates: unexpected answer from GitHub"} };
    std::string tag = rel.value("tag_name", "");
    std::string ver = tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
    out["latest"] = ver;
    if (rel.value("draft", false) || rel.value("prerelease", false) || cmp_version(ver, UPDATE_CURRENT) <= 0) {
        log_line("update check: up to date (latest %s)", tag.c_str());
        return out;
    }
    const json *exe = nullptr;
    if (rel.contains("assets") && rel["assets"].is_array())
        for (const json &a : rel["assets"]) {
            std::string n = a.value("name", "");
            if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".exe") == 0) { exe = &a; break; }
        }
    if (!exe) { log_line("update %s has no .exe", tag.c_str()); return out; }
    std::string url = exe->value("browser_download_url", ""), digest = exe->value("digest", "");
    if (url.rfind("https://", 0) != 0) return out;
    json info = { {"version", ver}, {"tag", tag}, {"url", url}, {"size", exe->value("size", 0LL)},
                  {"sha256", digest.rfind("sha256:", 0) == 0 ? digest.substr(7) : ""},
                  {"notes", rel.value("body", "")}, {"page", rel.value("html_url", "")} };
    { std::lock_guard<std::mutex> l(g_upd_mx); g_upd_last = info; }
    log_line("update %s available (%lld bytes)", tag.c_str(), info["size"].get<long long>());
    out["available"] = true; out["version"] = ver; out["notes"] = info["notes"]; out["size"] = info["size"]; out["page"] = info["page"];
    return out;
}

static std::wstring self_exe() { wchar_t p[MAX_PATH]; GetModuleFileNameW(NULL, p, MAX_PATH); return p; }
static std::wstring sibling(const std::wstring &exe, const wchar_t *suffix) {   // "...\EXVS Launcher.exe" -> "...\EXVS Launcher<suffix>"
    size_t dot = exe.find_last_of(L'.');
    return (dot == std::wstring::npos ? exe : exe.substr(0, dot)) + suffix;
}

static std::string sha256_hex(BCRYPT_HASH_HANDLE h) {
    unsigned char d[32]; BCryptFinishHash(h, d, 32, 0);
    char hex[65]; for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
    return hex;
}

// Downloads, checks and swaps the exe. The caller restarts.
static json install_update() {
    json info;
    { std::lock_guard<std::mutex> l(g_upd_mx); info = g_upd_last; }
    if (info.is_null()) return { {"ok", false}, {"error", "Check for updates first"} };
    bool expected = false;
    if (!g_upd_running.compare_exchange_strong(expected, true)) return { {"ok", false}, {"error", "The update is already downloading"} };
    struct Done { ~Done() { g_upd_running = false; } } done_guard;

    std::wstring exe = self_exe(), nw = sibling(exe, L".new.exe"), old = sibling(exe, L".old.exe");
    long long size = info.value("size", 0LL);
    g_upd_done = 0; g_upd_total = size;
    set_busy("Downloading the update…");
    struct Busy { ~Busy() { set_busy(""); } } busy_guard;

    HANDLE f = CreateFileW(nw.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return { {"ok", false}, {"error", "Couldn't write next to the launcher (error " + std::to_string(GetLastError()) + ")"} };
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    long long got = 0; std::string err;
    bool ok = https_get(info["url"].get<std::string>(), L"application/octet-stream", [&](const char *d, DWORD n) {
        DWORD w = 0;
        if (!WriteFile(f, d, n, &w, NULL) || w != n) return false;
        if (h) BCryptHashData(h, (PUCHAR)d, n, 0);
        got += n; g_upd_done = got;
        return got < (256ll << 20);
    }, err);
    std::string hex = h ? sha256_hex(h) : "";
    char mz[2] = {}; DWORD rd = 0;
    SetFilePointer(f, 0, NULL, FILE_BEGIN); ReadFile(f, mz, 2, &rd, NULL);
    CloseHandle(f);
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);

    std::string expect = info.value("sha256", "");
    std::string problem;
    if (!ok) problem = "Download failed: " + err;
    else if (size && got != size) problem = "The download is incomplete (" + std::to_string(got) + " of " + std::to_string(size) + " bytes)";
    else if (!expect.empty() && _stricmp(hex.c_str(), expect.c_str()) != 0) problem = "The downloaded file is damaged (SHA-256 doesn't match)";
    else if (rd != 2 || mz[0] != 'M' || mz[1] != 'Z') problem = "The downloaded file isn't a Windows program";
    if (!problem.empty()) { DeleteFileW(nw.c_str()); log_line("update failed: %s", problem.c_str()); return { {"ok", false}, {"error", problem} }; }

    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD e = GetLastError(); DeleteFileW(nw.c_str());
        return { {"ok", false}, {"error", "Couldn't replace the launcher (error " + std::to_string(e) + ")"} };
    }
    if (!MoveFileExW(nw.c_str(), exe.c_str(), 0)) {
        DWORD e = GetLastError();
        MoveFileExW(old.c_str(), exe.c_str(), 0);   // put this one back: the launcher keeps working
        DeleteFileW(nw.c_str());
        return { {"ok", false}, {"error", "Couldn't put the new launcher in place (error " + std::to_string(e) + ")"} };
    }
    log_line("updated to %s (sha256 %s)", info.value("tag", "").c_str(), hex.c_str());
    return { {"ok", true}, {"version", info["version"]} };
}

// Starts the (new) exe with the same arguments. The single-instance mutex must be released first.
static bool start_new_launcher() {
    std::wstring exe = self_exe();
    const wchar_t *args = PathGetArgsW(GetCommandLineW());
    std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    HINSTANCE r = ShellExecuteW(NULL, L"open", exe.c_str(), args && *args ? args : NULL, dir.c_str(), SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

// The .old.exe of the previous version may still be running for a moment: retry in the background.
static void cleanup_update_files() {
    std::thread([] {
        std::wstring exe = self_exe(), old = sibling(exe, L".old.exe"), nw = sibling(exe, L".new.exe");
        DeleteFileW(nw.c_str());
        for (int i = 0; i < 30 && exists(old); i++) { if (DeleteFileW(old.c_str())) break; Sleep(1000); }
    }).detach();
}

static json update_progress() { return { {"running", (bool)g_upd_running}, {"done", (long long)g_upd_done}, {"total", (long long)g_upd_total} }; }
