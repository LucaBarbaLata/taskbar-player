// ==WindhawkMod==
// @id              taskbar-player
// @name            Taskbar Player
// @description     A compact now-playing widget on the left side of the taskbar with a popup player and Spotify Connect support
// @version         1.1.0
// @author          Luca
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -luuid -lwindowsapp -lwinhttp -lws2_32 -lcrypt32 -lbcrypt -ld2d1 -ldwrite -lwindowscodecs -lgdi32 -luser32 -lshell32 -lshlwapi -lcomctl32 -lversion -luxtheme -ld3d11 -ldxgi -ldcomp -ldwmapi -lcoremessaging
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Player

A small now-playing widget that lives on the left side of the taskbar.

* Shows the cover art, title and artist of whatever is playing, plus
  previous / play-pause / next buttons.
* Click the widget to open a popup with a large cover, a seek bar, shuffle,
  repeat and (for Spotify) like, volume and a device picker.
* The popup is translucent (blur or acrylic, configurable, matching the
  Translucent Windows mod by default) and animated.
* Works with any app that integrates with Windows media controls (Spotify,
  browsers, Apple Music, VLC, ...).
* Optional **Spotify Connect** support: see and control playback that runs on
  any of your devices (phone, speaker, TV, ...). Spotify always has priority.
* Shares what it shows with the Glyph Lock Screen mod, which can then display
  and control Spotify Connect playback on the lock screen.

The widget hides itself when nothing is playing.

## Spotify Connect setup (one time)

1. Go to <https://developer.spotify.com/dashboard> and sign in.
2. Click **Create app**. Use any name and description.
3. Add this **Redirect URI** exactly: `http://127.0.0.1:8974/callback`
   (if you change the port in the settings, change it here too).
4. Under *Which API/SDKs are you planning to use?* check **Web API**, accept
   the terms and save.
5. Open the app's **Settings** and copy the **Client ID**.
6. Paste the Client ID in this mod's settings.
7. A "Connect Spotify" button appears on the taskbar. Click it, approve the
   permissions in the browser, and you're done.

Notes:

* Spotify requires the owner of a development app to have **Spotify Premium**.
* If signing in works but nothing shows up, open the app's **User Management**
  page in the Spotify dashboard and add the email of your Spotify account.
* The sign-in is stored encrypted (Windows DPAPI) for your Windows account
  only. Clearing the Client ID signs you out.
* Spotify limits how many requests a development app may make (all apps of
  one developer account share the limit). If it's used up, Spotify blocks
  the app for some hours: playback on other devices doesn't show until then,
  while music from apps on this PC still does. The mod asks Spotify only as
  often as needed and backs off while it's blocked.
* If the widget doesn't appear on the taskbar, turn on **Compatibility mode**
  in the settings.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- spotifyClientId: ""
  $name: Spotify Client ID
  $description: >-
    Optional. Enables Spotify Connect. See the Details tab for the one-time
    setup. Leave empty to only use local media sessions.
- redirectPort: 8974
  $name: Spotify redirect port
  $description: >-
    The redirect URI of your Spotify app must be
    http://127.0.0.1:<port>/callback
- maxTextWidth: 220
  $name: Maximum text width
  $description: >-
    Maximum width of the title and artist text on the taskbar, in pixels at
    100% scaling.
- showTaskbarControls: true
  $name: Show playback buttons on the taskbar
- leftMargin: 6
  $name: Left margin
  $description: Distance from the left edge of the taskbar, in pixels at 100% scaling.
- hoverRadius: 6
  $name: Hover corner radius
  $description: Corner radius of the hover highlight on the taskbar.
- hideWhenPausedMinutes: 0
  $name: Hide when paused (minutes)
  $description: >-
    Hide the widget after playback has been paused for this many minutes.
    0 means never.
- popupBackground: blur
  $name: Popup background
  $options:
  - blur: Blur (like the Translucent Windows mod)
  - acrylic: Acrylic (Windows flyouts)
  - solid: Solid
- popupTint: "3A232323"
  $name: Popup blur tint
  $description: >-
    Color laid over the blur, as AARRGGBB (alpha first). Higher alpha is
    darker and easier to read. The default matches the Translucent Windows
    mod's default.
- popupAnimations: true
  $name: Popup animations
- popupEngine: modern
  $name: Popup engine
  $description: >-
    Modern: rounded 13px corners and the whole panel fades. Classic: the
    Windows blur with Windows-shaped corners. Switch if the popup looks wrong.
  $options:
  - modern: Modern
  - classic: Classic
- overlayMode: false
  $name: Compatibility mode
  $description: >-
    Draw the widget as a separate window on top of the taskbar instead of
    embedding it. Try this if the widget doesn't show up.
*/
// ==/WindhawkModSettings==

#ifndef TP_HARNESS
#include <windhawk_utils.h>
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <uiautomation.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <dispatcherqueue.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x00000003
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace tp {

using winrt::com_ptr;

// ============================================================================
// Basic helpers
// ============================================================================

static inline ULONGLONG Tick() { return GetTickCount64(); }

static HINSTANCE ModuleInstance() {
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

static std::wstring ToWide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string ToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n,
                        nullptr, nullptr);
    return s;
}

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static bool IContains(const std::wstring& hay, const wchar_t* needle) {
    return Lower(hay).find(Lower(needle)) != std::wstring::npos;
}

static bool IEquals(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static std::string UrlEncode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static std::string UrlDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            char h[3] = {s[i + 1], s[i + 2], 0};
            out += (char)strtol(h, nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

static bool RandomBytes(void* buf, ULONG len) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, (PUCHAR)buf, len,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

static std::string RandomToken(size_t n) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::vector<unsigned char> b(n);
    RandomBytes(b.data(), (ULONG)n);
    std::string s;
    for (auto c : b) s += alphabet[c % 66];
    return s;
}

static std::string Base64(const unsigned char* data, size_t len) {
    if (!len) return {};
    DWORD n = 0;
    CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         nullptr, &n);
    std::string s(n, '\0');
    CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         s.data(), &n);
    s.resize(n);
    while (!s.empty() && (s.back() == '\0')) s.pop_back();
    return s;
}

static std::vector<unsigned char> Unbase64(const std::string& s) {
    DWORD n = 0;
    if (!CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64, nullptr,
                              &n, nullptr, nullptr)) {
        return {};
    }
    std::vector<unsigned char> v(n);
    CryptStringToBinaryA(s.c_str(), (DWORD)s.size(), CRYPT_STRING_BASE64, v.data(), &n,
                         nullptr, nullptr);
    v.resize(n);
    return v;
}

static std::string Base64Url(const unsigned char* data, size_t len) {
    std::string s = Base64(data, len);
    for (auto& c : s) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!s.empty() && s.back() == '=') s.pop_back();
    return s;
}

static bool Sha256(const std::string& in, unsigned char out[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
            ok = BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)in.data(), (ULONG)in.size(), 0)) &&
                 BCRYPT_SUCCESS(BCryptFinishHash(hash, out, 32, 0));
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}

// DPAPI: encrypt a secret for the current Windows user.
static std::string ProtectSecret(const std::string& plain) {
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"TaskbarPlayer", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::string s = Base64(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

static std::string UnprotectSecret(const std::string& b64) {
    auto bytes = Unbase64(b64);
    if (bytes.empty()) return {};
    DATA_BLOB in{(DWORD)bytes.size(), bytes.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    std::string s((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

static std::wstring GetStoredString(const wchar_t* name) {
    std::vector<wchar_t> buf(8192);
    size_t n = Wh_GetStringValue(name, buf.data(), buf.size());
    return std::wstring(buf.data(), std::min(n, buf.size()));
}

// ---------------------------------------------------------------------------
// Diagnostic log: <mod storage>\taskbar-player.log (kept under ~256 KB).
// Never contains tokens or other secrets.
// ---------------------------------------------------------------------------

static std::mutex g_traceMutex;
static std::wstring g_tracePath;

static void InitTrace() {
    wchar_t dir[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(dir, MAX_PATH) || !dir[0]) return;
    SHCreateDirectoryExW(nullptr, dir, nullptr);
    std::lock_guard<std::mutex> lock(g_traceMutex);
    g_tracePath = std::wstring(dir) + L"\\taskbar-player.log";
}

static void Trace(const wchar_t* fmt, ...) {
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(msg, ARRAYSIZE(msg) - 1, fmt, args);
    va_end(args);
    msg[ARRAYSIZE(msg) - 1] = 0;
    Wh_Log(L"%s", msg);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    swprintf(line, ARRAYSIZE(line), L"%04d-%02d-%02d %02d:%02d:%02d.%03d [%5u] %s\r\n", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId(), msg);
    std::string utf8;
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            utf8.resize(n - 1);
            WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), n, nullptr, nullptr);
        }
    }
    std::lock_guard<std::mutex> lock(g_traceMutex);
    if (g_tracePath.empty()) return;
    HANDLE f = CreateFileW(g_tracePath.c_str(), FILE_GENERIC_READ | FILE_GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > 256 * 1024) {
        SetFilePointer(f, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(f);
    }
    SetFilePointer(f, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
}

static std::wstring FormatTime(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t total = ms / 1000;
    int64_t h = total / 3600, m = (total / 60) % 60, s = total % 60;
    wchar_t buf[32];
    if (h > 0) swprintf(buf, 32, L"%lld:%02lld:%02lld", h, m, s);
    else swprintf(buf, 32, L"%lld:%02lld", m, s);
    return buf;
}

// ============================================================================
// Minimal JSON parser
// ============================================================================

class Json {
   public:
    enum class T : uint8_t { Null, Bool, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    static const Json& Nil() {
        static const Json j;
        return j;
    }
    const Json& operator[](std::string_view key) const {
        if (t == T::Obj)
            for (auto& p : o)
                if (p.first == key) return p.second;
        return Nil();
    }
    const Json& operator[](size_t i) const {
        return (t == T::Arr && i < a.size()) ? a[i] : Nil();
    }
    size_t size() const { return t == T::Arr ? a.size() : t == T::Obj ? o.size() : 0; }
    bool IsNull() const { return t == T::Null; }
    bool IsObj() const { return t == T::Obj; }
    bool Bool(bool def = false) const { return t == T::Bool ? b : def; }
    int64_t Int(int64_t def = 0) const { return t == T::Num ? (int64_t)n : def; }
    std::string Str() const { return t == T::Str ? s : std::string(); }
    std::wstring WStr() const { return ToWide(Str()); }

    static bool Parse(const std::string& text, Json& out) {
        Cursor c{text.data(), text.data() + text.size(), 0};
        if (!Value(c, out)) return false;
        Ws(c);
        return c.p == c.e;
    }

   private:
    struct Cursor {
        const char* p;
        const char* e;
        int depth;
    };
    static void Ws(Cursor& c) {
        while (c.p < c.e && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r'))
            c.p++;
    }
    static bool Lit(Cursor& c, const char* lit) {
        size_t n = strlen(lit);
        if ((size_t)(c.e - c.p) < n || memcmp(c.p, lit, n) != 0) return false;
        c.p += n;
        return true;
    }
    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    static bool Hex4(Cursor& c, uint32_t& v) {
        if (c.e - c.p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; i++) {
            char h = *c.p++;
            v <<= 4;
            if (h >= '0' && h <= '9') v |= h - '0';
            else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
            else return false;
        }
        return true;
    }
    static bool String(Cursor& c, std::string& out) {
        if (c.p >= c.e || *c.p != '"') return false;
        c.p++;
        while (c.p < c.e) {
            char ch = *c.p++;
            if (ch == '"') return true;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (c.p >= c.e) return false;
            char esc = *c.p++;
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!Hex4(c, cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && c.e - c.p >= 6 && c.p[0] == '\\' &&
                        c.p[1] == 'u') {
                        c.p += 2;
                        uint32_t lo;
                        if (!Hex4(c, lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    static bool Value(Cursor& c, Json& out) {
        if (++c.depth > 64) return false;
        Ws(c);
        if (c.p >= c.e) return false;
        bool ok = true;
        char ch = *c.p;
        if (ch == '{') {
            out.t = T::Obj;
            c.p++;
            Ws(c);
            if (c.p < c.e && *c.p == '}') {
                c.p++;
            } else {
                for (;;) {
                    Ws(c);
                    std::string key;
                    if (!String(c, key)) { ok = false; break; }
                    Ws(c);
                    if (c.p >= c.e || *c.p != ':') { ok = false; break; }
                    c.p++;
                    out.o.emplace_back(std::move(key), Json());
                    if (!Value(c, out.o.back().second)) { ok = false; break; }
                    Ws(c);
                    if (c.p < c.e && *c.p == ',') { c.p++; continue; }
                    if (c.p < c.e && *c.p == '}') { c.p++; break; }
                    ok = false;
                    break;
                }
            }
        } else if (ch == '[') {
            out.t = T::Arr;
            c.p++;
            Ws(c);
            if (c.p < c.e && *c.p == ']') {
                c.p++;
            } else {
                for (;;) {
                    out.a.emplace_back();
                    if (!Value(c, out.a.back())) { ok = false; break; }
                    Ws(c);
                    if (c.p < c.e && *c.p == ',') { c.p++; continue; }
                    if (c.p < c.e && *c.p == ']') { c.p++; break; }
                    ok = false;
                    break;
                }
            }
        } else if (ch == '"') {
            out.t = T::Str;
            ok = String(c, out.s);
        } else if (ch == 't') {
            out.t = T::Bool;
            out.b = true;
            ok = Lit(c, "true");
        } else if (ch == 'f') {
            out.t = T::Bool;
            ok = Lit(c, "false");
        } else if (ch == 'n') {
            ok = Lit(c, "null");
        } else {
            char buf[64];
            size_t n = 0;
            while (c.p < c.e && n < sizeof(buf) - 1 &&
                   (isdigit((unsigned char)*c.p) || *c.p == '-' || *c.p == '+' ||
                    *c.p == '.' || *c.p == 'e' || *c.p == 'E')) {
                buf[n++] = *c.p++;
            }
            buf[n] = 0;
            if (!n) return false;
            out.t = T::Num;
            out.n = strtod(buf, nullptr);
        }
        c.depth--;
        return ok;
    }
};

// ============================================================================
// HTTP client (WinHTTP, synchronous; call from worker threads only)
// ============================================================================

struct HttpResult {
    int status = 0;  // 0 = network error / cancelled
    std::string body;
    int retryAfter = 0;          // seconds, 0 if absent
    std::wstring retryAfterText; // the raw header, for the log
    bool throttled = false;  // not sent: still waiting out an earlier Retry-After
    bool Ok() const { return status >= 200 && status < 300; }
};

class Http {
   public:
    static bool Init() {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_cancel = false;
        if (s_session) return true;
        s_session = WinHttpOpen(L"TaskbarPlayer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s_session) {
            s_session = WinHttpOpen(L"TaskbarPlayer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!s_session) return false;
        WinHttpSetTimeouts(s_session, 5000, 5000, 8000, 10000);
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s_session, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                         sizeof(decompression));
        return true;
    }

    // Aborts all in-flight requests (used on shutdown).
    static void CancelAll() {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_cancel = true;
        for (HINTERNET h : s_active) WinHttpCloseHandle(h);
        s_active.clear();
    }

    static void Cleanup() {
        CancelAll();
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_session) {
            WinHttpCloseHandle(s_session);
            s_session = nullptr;
        }
    }

    static HttpResult Request(const wchar_t* method, const std::wstring& url,
                              const std::string& body = {},
                              const std::wstring& headers = {}) {
        HttpResult r;
        size_t scheme = url.find(L"://");
        if (scheme == std::wstring::npos) return r;
        bool secure = url.compare(0, scheme, L"https") == 0;
        size_t hostStart = scheme + 3;
        size_t pathStart = url.find(L'/', hostStart);
        std::wstring host = url.substr(
            hostStart, pathStart == std::wstring::npos ? std::wstring::npos : pathStart - hostStart);
        std::wstring path = pathStart == std::wstring::npos ? L"/" : url.substr(pathStart);
        INTERNET_PORT port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        size_t colon = host.find(L':');
        if (colon != std::wstring::npos) {
            port = (INTERNET_PORT)_wtoi(host.c_str() + colon + 1);
            host.resize(colon);
        }

        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_cancel || !s_session) return r;
            hConnect = WinHttpConnect(s_session, host.c_str(), port, 0);
            if (!hConnect) return r;
            hRequest = WinHttpOpenRequest(hConnect, method, path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          secure ? WINHTTP_FLAG_SECURE : 0);
            if (!hRequest) {
                WinHttpCloseHandle(hConnect);
                return r;
            }
            s_active.push_back(hRequest);
        }

        BOOL ok = WinHttpSendRequest(
            hRequest, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : (DWORD)-1L,
            body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
            (DWORD)body.size(), 0);
        if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);
        if (ok) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);
            // Read as text: the value is logged, and it may be large.
            wchar_t retry[64] = {};
            size = sizeof(retry) - sizeof(wchar_t);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, retry,
                                    &size, WINHTTP_NO_HEADER_INDEX)) {
                r.retryAfterText = retry;
                long v = wcstol(retry, nullptr, 10);
                r.retryAfter = v > 0 ? (int)std::min<long>(v, 7 * 24 * 3600) : 0;
            }
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail) || !avail) break;
                size_t old = r.body.size();
                if (old + avail > 24 * 1024 * 1024) break;
                r.body.resize(old + avail);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, r.body.data() + old, avail, &read)) {
                    r.body.resize(old);
                    break;
                }
                r.body.resize(old + read);
                if (!read) break;
            }
            r.status = (int)status;
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            auto it = std::find(s_active.begin(), s_active.end(), hRequest);
            if (it != s_active.end()) {
                s_active.erase(it);
                WinHttpCloseHandle(hRequest);
            } else {
                r.status = 0;  // closed by CancelAll()
            }
        }
        WinHttpCloseHandle(hConnect);
        return r;
    }

   private:
    static inline HINTERNET s_session = nullptr;
    static inline std::mutex s_mutex;
    static inline std::vector<HINTERNET> s_active;
    static inline bool s_cancel = false;
};

// ============================================================================
// Worker thread with a delayed task queue (MTA, COM/WinRT initialized)
// ============================================================================

class Worker {
   public:
    // When explorer exits without unloading the mod (e.g. the short-lived
    // helper explorer.exe processes), Windows has already killed this thread.
    // A still-joinable std::thread would call std::terminate() and crash the
    // process, so let it go instead.
    ~Worker() {
        if (thread_.joinable()) thread_.detach();
    }

    void Start() {
        std::lock_guard<std::mutex> lock(m_);
        if (thread_.joinable()) return;
        stopping_ = false;
        thread_ = std::thread([this] { Run(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    bool Post(std::function<void()> fn, DWORD delayMs = 0) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (stopping_ || !thread_.joinable()) return false;
            items_.push_back(Item{Tick() + delayMs, seq_++, std::move(fn)});
        }
        cv_.notify_all();
        return true;
    }

    // Runs `fn` on the worker and waits for it (with a timeout).
    void Sync(std::function<void()> fn, DWORD timeoutMs = 10000) {
        if (GetCurrentThreadId() == tid_) {
            fn();
            return;
        }
        // The event is shared so that a late task never touches a closed handle.
        auto ev = std::shared_ptr<void>(CreateEventW(nullptr, TRUE, FALSE, nullptr), CloseHandle);
        bool posted = Post([fn = std::move(fn), ev] {
            fn();
            SetEvent(ev.get());
        });
        if (posted) WaitForSingleObject(ev.get(), timeoutMs);
    }

    bool IsCurrent() const { return GetCurrentThreadId() == tid_; }

   private:
    struct Item {
        ULONGLONG due;
        uint64_t seq;
        std::function<void()> fn;
    };

    void Run() {
        tid_ = GetCurrentThreadId();
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::unique_lock<std::mutex> lock(m_);
        while (!stopping_) {
            if (items_.empty()) {
                cv_.wait(lock);
                continue;
            }
            auto best = std::min_element(items_.begin(), items_.end(),
                                         [](const Item& a, const Item& b) {
                                             return a.due != b.due ? a.due < b.due : a.seq < b.seq;
                                         });
            ULONGLONG now = Tick();
            if (best->due > now) {
                cv_.wait_for(lock, std::chrono::milliseconds(best->due - now));
                continue;
            }
            std::function<void()> fn = std::move(best->fn);
            items_.erase(best);
            lock.unlock();
            try {
                fn();
            } catch (winrt::hresult_error const& e) {
                Wh_Log(L"Task failed: 0x%08X %s", (unsigned)e.code().value, e.message().c_str());
            } catch (std::exception const& e) {
                Wh_Log(L"Task failed: %S", e.what());
            } catch (...) {
                Wh_Log(L"Task failed: unknown exception");
            }
            fn = nullptr;
            lock.lock();
        }
        // Drop pending tasks on this thread (they may hold COM references).
        std::vector<Item> pending = std::move(items_);
        items_.clear();
        lock.unlock();
        pending.clear();
        winrt::uninit_apartment();
    }

    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<Item> items_;
    uint64_t seq_ = 0;
    bool stopping_ = false;
    std::atomic<DWORD> tid_{0};
};

// core: model + GSMTC; spot: Spotify polling; cmd: commands; img: images, icons, UIA
// Globals below are heap-allocated and never freed: if the process exits
// without unloading the mod, their destructors must not run (threads are gone
// and COM objects may belong to DLLs that are being torn down).
static Worker& g_core = *new Worker();
static Worker& g_spot = *new Worker();
static Worker& g_cmd = *new Worker();
static Worker& g_img = *new Worker();
static std::atomic<bool> g_shuttingDown{false};

// ============================================================================
// Settings
// ============================================================================

struct Settings {
    std::wstring clientId;
    int redirectPort = 8974;
    int maxTextWidth = 220;
    bool showControls = true;
    int leftMargin = 6;
    int hoverRadius = 6;
    int hidePausedMinutes = 0;
    enum class Backdrop { Blur, Acrylic, Solid } popupBackdrop = Backdrop::Blur;
    uint32_t popupTint = 0x3A232323;  // AARRGGBB
    bool popupAnimations = true;
    bool popupClassic = false;  // force the DirectComposition + DWM blur popup
    bool overlayMode = false;
};

// Parses "AARRGGBB" or "RRGGBB" (optionally with a leading '#').
static bool ParseArgb(std::wstring s, uint32_t& out) {
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return false;
    uint32_t v = 0;
    for (wchar_t c : s) {
        int d = iswdigit(c) ? c - L'0' : (c >= L'a' && c <= L'f') ? c - L'a' + 10 : (c >= L'A' && c <= L'F') ? c - L'A' + 10 : -1;
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = s.size() == 6 ? (0xFF000000u | v) : v;
    return true;
}

static std::mutex g_settingsMutex;
static std::shared_ptr<const Settings> g_settings = std::make_shared<Settings>();

static std::shared_ptr<const Settings> GetSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

static void LoadSettings() {
    auto s = std::make_shared<Settings>();
    {
        PCWSTR id = Wh_GetStringSetting(L"spotifyClientId");
        s->clientId = id ? id : L"";
        Wh_FreeStringSetting(id);
        s->clientId.erase(std::remove_if(s->clientId.begin(), s->clientId.end(),
                                         [](wchar_t c) { return iswspace(c); }),
                          s->clientId.end());
    }
    s->redirectPort = std::clamp(Wh_GetIntSetting(L"redirectPort"), 1024, 65535);
    s->maxTextWidth = std::clamp(Wh_GetIntSetting(L"maxTextWidth"), 40, 1000);
    s->showControls = Wh_GetIntSetting(L"showTaskbarControls") != 0;
    s->leftMargin = std::clamp(Wh_GetIntSetting(L"leftMargin"), 0, 2000);
    s->hoverRadius = std::clamp(Wh_GetIntSetting(L"hoverRadius"), 0, 40);
    s->hidePausedMinutes = std::clamp(Wh_GetIntSetting(L"hideWhenPausedMinutes"), 0, 100000);
    {
        PCWSTR bg = Wh_GetStringSetting(L"popupBackground");
        std::wstring b = bg ? bg : L"";
        Wh_FreeStringSetting(bg);
        s->popupBackdrop = b == L"solid"     ? Settings::Backdrop::Solid
                           : b == L"acrylic" ? Settings::Backdrop::Acrylic
                                             : Settings::Backdrop::Blur;
        PCWSTR tint = Wh_GetStringSetting(L"popupTint");
        if (!tint || !ParseArgb(tint, s->popupTint)) s->popupTint = 0x3A232323;
        Wh_FreeStringSetting(tint);
    }
    s->popupAnimations = Wh_GetIntSetting(L"popupAnimations") != 0;
    {
        PCWSTR engine = Wh_GetStringSetting(L"popupEngine");
        s->popupClassic = engine && wcscmp(engine, L"classic") == 0;
        Wh_FreeStringSetting(engine);
    }
    s->overlayMode = Wh_GetIntSetting(L"overlayMode") != 0;
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = s;
}

}  // namespace tp

namespace tp {

namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

// ============================================================================
// Shared model types
// ============================================================================

enum class RepeatMode : int { Off = 0, Context = 1, Track = 2 };

struct Art {
    std::wstring key;
    com_ptr<IWICBitmap> full;   // square, up to 640px
    com_ptr<IWICBitmap> small;  // pre-scaled for the taskbar widget
    com_ptr<IWICBitmap> large;  // pre-scaled for the popup cover
    UINT smallPx = 0, largePx = 0;
    D2D1_COLOR_F accent{0.86f, 0.86f, 0.86f, 1.f};
};

struct AppInfo {
    std::wstring aumid, name;
    com_ptr<IWICBitmap> icon;  // may be null
    bool spotify = false;
};

struct DeviceInfo {
    std::wstring id, name, type;
    bool active = false, restricted = false, supportsVolume = false;
    int volume = -1;
};

enum class Src { None, Spotify, Session };

struct Snapshot {
    uint64_t seq = 0;
    bool visible = false;
    Src src = Src::None;
    bool connectPrompt = false;
    bool authPending = false;
    bool spotifyConfigured = false, spotifyAuthorized = false;

    std::wstring appName, aumid;
    std::shared_ptr<AppInfo> app;
    bool isSpotify = false;

    std::wstring title, artist, album, trackKey, trackUri;
    std::shared_ptr<Art> art;
    std::wstring artUrl;  // Spotify cover URL (for the remote feed)

    bool playing = false;
    int64_t posMs = 0, durMs = 0;
    ULONGLONG posTick = 0;
    double rate = 1.0;

    bool canPlayPause = false, canNext = false, canPrev = false, canSeek = false;
    bool canShuffle = false, canRepeat = false;
    bool shuffle = false;
    RepeatMode repeat = RepeatMode::Off;
    bool canLike = false;
    int liked = -1;
    bool canVolume = false;
    int volume = -1;
    bool canDevices = false;
    std::wstring deviceId, deviceName, deviceType;
    std::vector<DeviceInfo> devices;
    bool devicesLoading = false;

    std::wstring toast;
    uint32_t toastSeq = 0;

    int64_t PosNow() const {
        int64_t p = posMs + (playing ? (int64_t)((double)(Tick() - posTick) * rate) : 0);
        if (durMs > 0) p = std::min(p, durMs);
        return std::max<int64_t>(0, p);
    }
};

// Messages posted to the UI (taskbar thread).
constexpr UINT WM_TP_SNAPSHOT = WM_APP + 0x351;
constexpr UINT WM_TP_RELAYOUT = WM_APP + 0x352;
constexpr UINT WM_TP_SETTINGS = WM_APP + 0x353;

static std::mutex g_snapMutex;
static std::shared_ptr<const Snapshot>& g_snap = *new std::shared_ptr<const Snapshot>();
static std::atomic<HWND> g_uiHwnd{nullptr};      // widget window, receives notifications
static std::atomic<HWND> g_taskbarHwnd{nullptr};
static std::atomic<int> g_uiScaleMilli{1000};     // DPI scale * 1000
static std::atomic<int> g_startButtonLeft{-1};    // taskbar client X of the Start button
static std::atomic<bool> g_widgetVisible{false};
static std::atomic<bool> g_popupOpen{false};

#ifdef TP_HARNESS
static std::function<void(const Snapshot&)> g_testSnapHook;  // observes every published snapshot
static std::wstring g_testOnlyAumid;                          // ignore all other media sessions
#endif

// ============================================================================
// Remote feed: what is shown, for other mods (the Glyph Lock Screen mod), and
// transport commands from them
// ============================================================================
//
// The record lives in a named file mapping and is rewritten on every
// snapshot, at least every few seconds. `seq` is a seqlock: odd while a write
// is in progress. A reader that wants to control playback stores a
// FeedCommand in `command` and sets the named event. The layout is shared
// with the readers: change the names (".1") together with it.

#ifdef TP_HARNESS
constexpr wchar_t kFeedMappingName[] = L"Local\\TaskbarPlayer.Feed.Harness";
constexpr wchar_t kFeedEventName[] = L"Local\\TaskbarPlayer.Command.Harness";
#else
constexpr wchar_t kFeedMappingName[] = L"Local\\TaskbarPlayer.Feed.1";
constexpr wchar_t kFeedEventName[] = L"Local\\TaskbarPlayer.Command.1";
#endif
constexpr uint32_t kFeedMagic = 0x31465054;  // "TPF1"

struct FeedRecord {
    uint32_t magic;
    volatile LONG seq;
    volatile LONG command;  // FeedCommand, written by readers
    uint32_t source;        // 0 nothing shown, 1 Spotify Web API, 2 media session
    uint64_t stampMs;       // unix ms of the last write
    uint32_t playing;
    uint32_t controls;      // 1 previous, 2 next, 4 play/pause
    int64_t posMs, durMs;
    uint64_t posStampMs;    // unix ms at which posMs was sampled
    wchar_t title[256], artist[256], app[64], device[128], artUrl[512];
};
static_assert(sizeof(FeedRecord) == 2488, "the feed layout is shared with other mods");

enum FeedCommand : LONG { kFeedNone = 0, kFeedToggle = 1, kFeedNext = 2, kFeedPrev = 3 };

static void RunFeedCommand(LONG command);  // defined after the controller

static uint64_t UnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) / 10000ULL;
}

class RemoteFeed {
   public:
    // Core thread.
    void Start() {
        if (rec_) return;
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FeedRecord),
                                      kFeedMappingName);
        if (mapping_)
            rec_ = (FeedRecord*)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FeedRecord));
        event_ = CreateEventW(nullptr, FALSE, FALSE, kFeedEventName);
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!rec_ || !event_ || !stop_) {
            Wh_Log(L"Remote feed unavailable (error %u)", GetLastError());
            Close();
            return;
        }
        rec_->magic = kFeedMagic;
        InterlockedExchange(&rec_->command, kFeedNone);
        thread_ = std::thread([this] { Run(); });
    }

    // Core thread.
    void Stop() {
        if (stop_) SetEvent(stop_);
        if (thread_.joinable()) thread_.join();
        Write(nullptr);  // nothing is shown any more
        Close();
    }

    // Core thread: called for every published snapshot.
    void Write(const Snapshot* s) {
        FeedRecord* r = rec_;
        if (!r) return;
        bool shown = s && s->visible && !s->connectPrompt && s->src != Src::None;
        uint64_t now = UnixMs();
        InterlockedIncrement(&r->seq);
        r->stampMs = now;
        r->source = !shown ? 0 : (s->src == Src::Spotify ? 1 : 2);
        r->playing = shown && s->playing;
        r->controls = !shown ? 0 : (s->canPrev ? 1 : 0) | (s->canNext ? 2 : 0) | (s->canPlayPause ? 4 : 0);
        r->posMs = shown ? s->PosNow() : 0;
        r->durMs = shown ? s->durMs : 0;
        r->posStampMs = now;
        static const std::wstring empty;
        Put(r->title, shown ? s->title : empty);
        Put(r->artist, shown ? s->artist : empty);
        Put(r->app, shown ? s->appName : empty);
        Put(r->device, shown ? s->deviceName : empty);
        Put(r->artUrl, shown ? s->artUrl : empty);
        InterlockedIncrement(&r->seq);
    }

   private:
    template <size_t N>
    static void Put(wchar_t (&dst)[N], const std::wstring& src) {
        size_t n = std::min(src.size(), N - 1);
        memcpy(dst, src.data(), n * sizeof(wchar_t));
        dst[n] = 0;
    }

    // Waits for commands from readers.
    void Run() {
        HANDLE handles[2] = {stop_, event_};
        while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            LONG command = InterlockedExchange(&rec_->command, kFeedNone);
            if (command != kFeedNone) RunFeedCommand(command);
        }
    }

    void Close() {
        if (rec_) UnmapViewOfFile(rec_);
        rec_ = nullptr;
        if (mapping_) CloseHandle(mapping_);
        mapping_ = nullptr;
        if (event_) CloseHandle(event_);
        event_ = nullptr;
        if (stop_) CloseHandle(stop_);
        stop_ = nullptr;
    }

    HANDLE mapping_ = nullptr, event_ = nullptr, stop_ = nullptr;
    FeedRecord* rec_ = nullptr;
    std::thread thread_;
};

static RemoteFeed& g_feed = *new RemoteFeed();  // never destroyed, see g_core

static void PostSnapshot(std::shared_ptr<const Snapshot> s) {
#ifdef TP_HARNESS
    if (g_testSnapHook && s) g_testSnapHook(*s);
#endif
    g_feed.Write(s.get());
    {
        std::lock_guard<std::mutex> lock(g_snapMutex);
        g_snap = std::move(s);
    }
    if (HWND h = g_uiHwnd) PostMessage(h, WM_TP_SNAPSHOT, 0, 0);
}

static std::shared_ptr<const Snapshot> LatestSnapshot() {
    std::lock_guard<std::mutex> lock(g_snapMutex);
    return g_snap;
}

// ============================================================================
// Imaging (WIC)
// ============================================================================

static std::mutex g_wicMutex;
static com_ptr<IWICImagingFactory>& g_wic = *new com_ptr<IWICImagingFactory>();

static IWICImagingFactory* Wic() {
    std::lock_guard<std::mutex> lock(g_wicMutex);
    if (!g_wic) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IWICImagingFactory), g_wic.put_void());
    }
    return g_wic.get();
}

static com_ptr<IWICBitmap> ToBitmap(IWICBitmapSource* src) {
    com_ptr<IWICBitmap> b;
    if (src && Wic()) Wic()->CreateBitmapFromSource(src, WICBitmapCacheOnLoad, b.put());
    return b;
}

static com_ptr<IWICBitmap> ToPbgra(IWICBitmapSource* src) {
    if (!src) return nullptr;
    com_ptr<IWICBitmapSource> conv;
    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, src, conv.put())))
        return nullptr;
    return ToBitmap(conv.get());
}

static com_ptr<IWICBitmap> DecodeImage(const void* data, size_t size) {
    if (!data || !size || !Wic()) return nullptr;
    com_ptr<IStream> stream;
    stream.attach(SHCreateMemStream((const BYTE*)data, (UINT)size));
    if (!stream) return nullptr;
    com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(Wic()->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                              decoder.put())))
        return nullptr;
    com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) return nullptr;
    return ToPbgra(frame.get());
}

// Center-crops to a square and scales to `px` with high quality.
static com_ptr<IWICBitmap> SquareScaled(IWICBitmapSource* src, UINT px) {
    if (!src || !px || !Wic()) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    if (!w || !h) return nullptr;
    UINT side = std::min(w, h);
    com_ptr<IWICBitmapClipper> clipper;
    if (FAILED(Wic()->CreateBitmapClipper(clipper.put()))) return nullptr;
    WICRect rc{(INT)((w - side) / 2), (INT)((h - side) / 2), (INT)side, (INT)side};
    if (FAILED(clipper->Initialize(src, &rc))) return nullptr;
    if (side == px) return ToBitmap(clipper.get());
    com_ptr<IWICBitmapScaler> scaler;
    if (FAILED(Wic()->CreateBitmapScaler(scaler.put()))) return nullptr;
    auto mode = (WICBitmapInterpolationMode)4;  // WICBitmapInterpolationModeHighQualityCubic
    if (FAILED(scaler->Initialize(clipper.get(), px, px, mode)) &&
        FAILED(scaler->Initialize(clipper.get(), px, px, WICBitmapInterpolationModeFant)))
        return nullptr;
    return ToBitmap(scaler.get());
}

static void RgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    l = (mx + mn) / 2;
    if (mx == mn) {
        h = s = 0;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
}

static float HueToRgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t;
    if (t < 1.f / 2) return q;
    if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
    return p;
}

static D2D1_COLOR_F HslToColor(float h, float s, float l) {
    if (s == 0) return D2D1_COLOR_F{l, l, l, 1};
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    return D2D1_COLOR_F{HueToRgb(p, q, h + 1.f / 3), HueToRgb(p, q, h), HueToRgb(p, q, h - 1.f / 3), 1};
}

// Picks a vivid, readable accent color from the image.
static D2D1_COLOR_F ComputeAccent(IWICBitmapSource* src) {
    const D2D1_COLOR_F neutral{0.86f, 0.86f, 0.86f, 1.f};
    auto small = SquareScaled(src, 40);
    if (!small) return neutral;
    std::vector<BYTE> px(40 * 40 * 4);
    if (FAILED(small->CopyPixels(nullptr, 40 * 4, (UINT)px.size(), px.data()))) return neutral;
    float wsum[36] = {}, rs[36] = {}, gs[36] = {}, bs[36] = {};
    int counted = 0;
    float total = 0;
    for (size_t i = 0; i < 40 * 40; i++) {
        float a = px[i * 4 + 3] / 255.f;
        if (a < 0.8f) continue;
        float b = px[i * 4] / 255.f / a, g = px[i * 4 + 1] / 255.f / a, r = px[i * 4 + 2] / 255.f / a;
        counted++;
        float mx = std::max({r, g, b}), mn = std::min({r, g, b});
        float v = mx, s = mx > 0 ? (mx - mn) / mx : 0;
        if (v < 0.2f || s < 0.25f) continue;
        float h;
        float d = mx - mn;
        if (mx == r) h = std::fmod((g - b) / d + 6.f, 6.f);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        int bin = std::clamp((int)(h * 6.f), 0, 35);
        float w = s * s * v;
        wsum[bin] += w;
        rs[bin] += r * w;
        gs[bin] += g * w;
        bs[bin] += b * w;
        total += w;
    }
    if (!counted || total < counted * 0.015f) return neutral;
    int best = 0;
    float bestScore = -1;
    for (int i = 0; i < 36; i++) {
        float score = wsum[i] + 0.5f * (wsum[(i + 35) % 36] + wsum[(i + 1) % 36]);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    float W = 0, R = 0, G = 0, B = 0;
    for (int k = -1; k <= 1; k++) {
        int i = (best + k + 36) % 36;
        W += wsum[i];
        R += rs[i];
        G += gs[i];
        B += bs[i];
    }
    if (W <= 0) return neutral;
    float h, s, l;
    RgbToHsl(R / W, G / W, B / W, h, s, l);
    s = std::clamp(s, 0.45f, 0.9f);
    l = std::clamp(l, 0.58f, 0.72f);
    return HslToColor(h, s, l);
}

static std::shared_ptr<Art> MakeArt(const std::wstring& key, const void* data, size_t size,
                                    UINT smallPx, UINT largePx) {
    auto src = DecodeImage(data, size);
    if (!src) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    auto art = std::make_shared<Art>();
    art->key = key;
    art->full = SquareScaled(src.get(), std::min(std::min(w, h), 640u));
    if (!art->full) return nullptr;
    art->small = SquareScaled(art->full.get(), smallPx);
    art->smallPx = art->small ? smallPx : 0;
    art->large = SquareScaled(art->full.get(), largePx);
    art->largePx = art->large ? largePx : 0;
    art->accent = ComputeAccent(art->full.get());
    return art;
}

static void ArtSizes(UINT& smallPx, UINT& largePx) {
    float scale = g_uiScaleMilli / 1000.f;
    smallPx = (UINT)std::lround(32 * scale);
    largePx = (UINT)std::lround(300 * scale);
}

// ============================================================================
// App name and icon resolution
// ============================================================================

static com_ptr<IWICBitmap> IconFromShellItem(IShellItem* item) {
    com_ptr<IShellItemImageFactory> factory;
    if (!item || FAILED(item->QueryInterface(__uuidof(IShellItemImageFactory), factory.put_void())))
        return nullptr;
    HBITMAP hbm = nullptr;
    if (FAILED(factory->GetImage(SIZE{64, 64}, SIIGBF_ICONONLY, &hbm)) || !hbm) return nullptr;
    com_ptr<IWICBitmap> bmp;
    if (Wic())
        Wic()->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapUsePremultipliedAlpha, bmp.put());
    DeleteObject(hbm);
    return bmp ? ToPbgra(bmp.get()) : nullptr;
}

static std::wstring FindProcessPath(const std::wstring& exeName) {
    std::wstring result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, exeName.c_str()) != 0) continue;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!proc) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD len = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(proc, 0, path, &len)) result.assign(path, len);
        CloseHandle(proc);
        if (!result.empty()) break;
    }
    CloseHandle(snap);
    return result;
}

static std::wstring FileDescription(const std::wstring& path) {
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct LangCp {
        WORD lang, cp;
    }* trans = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&trans, &len) ||
        len < sizeof(LangCp))
        return {};
    wchar_t key[64];
    swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\FileDescription", trans->lang, trans->cp);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), key, (void**)&value, &len) || !value || !len) return {};
    return value;
}

static std::shared_ptr<AppInfo> ResolveAppInfo(const std::wstring& aumid) {
    auto info = std::make_shared<AppInfo>();
    info->aumid = aumid;
    if (IContains(aumid, L"spotify")) {
        info->name = L"Spotify";
        info->spotify = true;
        return info;
    }
    com_ptr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, aumid.c_str(),
                                            __uuidof(IShellItem), item.put_void())) &&
        item) {
        PWSTR name = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
            // Plain executable IDs ("app.exe") aren't real Start menu entries.
            if (!(IEquals(name, aumid) && IContains(aumid, L".exe"))) info->name = name;
            CoTaskMemFree(name);
        }
        if (!info->name.empty()) info->icon = IconFromShellItem(item.get());
    }
    if ((info->name.empty() || !info->icon) && IContains(aumid, L".exe")) {
        std::wstring exe = aumid.substr(aumid.find_last_of(L"\\/") + 1);
        std::wstring path = FindProcessPath(exe);
        if (!path.empty()) {
            if (info->name.empty()) info->name = FileDescription(path);
            if (!info->icon) {
                com_ptr<IShellItem> fileItem;
                if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, __uuidof(IShellItem),
                                                          fileItem.put_void())))
                    info->icon = IconFromShellItem(fileItem.get());
            }
        }
    }
    if (info->name.empty()) {
        std::wstring n = aumid;
        size_t bang = n.find(L'!');
        if (bang != std::wstring::npos) n = n.substr(bang + 1);
        size_t slash = n.find_last_of(L"\\/");
        if (slash != std::wstring::npos) n = n.substr(slash + 1);
        if (n.size() > 4 && IEquals(n.substr(n.size() - 4), L".exe")) n.resize(n.size() - 4);
        size_t dot = n.find_last_of(L'.');
        if (dot != std::wstring::npos && dot + 1 < n.size()) n = n.substr(dot + 1);
        if (!n.empty()) n[0] = (wchar_t)towupper(n[0]);
        info->name = n;
    }
    return info;
}

// ============================================================================
// Taskbar measurement (UI Automation): where the centered icons begin
// ============================================================================

static const CLSID kCLSID_CUIAutomation = {
    0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
static const IID kIID_IUIAutomation = {
    0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};

static com_ptr<IUIAutomation>& g_uia = *new com_ptr<IUIAutomation>();  // img thread only
static com_ptr<IUIAutomationElement>& g_uiaStart = *new com_ptr<IUIAutomationElement>();  // img thread only

static com_ptr<IUIAutomationElement> FindByProperty(IUIAutomationElement* root, PROPERTYID prop,
                                                    const wchar_t* value) {
    com_ptr<IUIAutomationElement> found;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value);
    com_ptr<IUIAutomationCondition> cond;
    if (SUCCEEDED(g_uia->CreatePropertyCondition(prop, v, cond.put())) && cond)
        root->FindFirst(TreeScope_Descendants, cond.get(), found.put());
    VariantClear(&v);
    return found;
}

static void MeasureTaskbar() {
    HWND tb = g_taskbarHwnd;
    if (!tb || g_shuttingDown) return;
    if (!g_uia) {
        CoCreateInstance(kCLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, kIID_IUIAutomation,
                         g_uia.put_void());
        if (!g_uia) return;
    }
    if (!g_uiaStart) {
        com_ptr<IUIAutomationElement> root;
        if (FAILED(g_uia->ElementFromHandle(tb, root.put())) || !root) return;
        g_uiaStart = FindByProperty(root.get(), 30011 /*AutomationId*/, L"StartButton");
        if (!g_uiaStart) g_uiaStart = FindByProperty(root.get(), 30005 /*Name*/, L"Start");
        if (!g_uiaStart) return;
    }
    RECT r{};
    if (FAILED(g_uiaStart->get_CurrentBoundingRectangle(&r)) || r.right <= r.left) {
        g_uiaStart = nullptr;
        return;
    }
    POINT p{r.left, r.top};
    ScreenToClient(tb, &p);
    if (p.x != g_startButtonLeft) {
        g_startButtonLeft = p.x;
        if (HWND ui = g_uiHwnd) PostMessage(ui, WM_TP_RELAYOUT, 0, 0);
    }
}

static void ReleaseImgThreadObjects() {
    g_uiaStart = nullptr;
    g_uia = nullptr;
}

// ============================================================================
// Spotify Web API
// ============================================================================

struct SpotifyState {
    bool ok = false;  // the last poll succeeded
    bool hasDevice = false, playing = false;
    DeviceInfo device;
    std::string type;  // track, episode, ad, unknown
    std::wstring id, uri, title, artist, album, imageUrl;
    int64_t progressMs = 0, durationMs = 0;
    ULONGLONG tick = 0;
    bool shuffle = false;
    RepeatMode repeat = RepeatMode::Off;
    bool disPause = false, disResume = false, disSeek = false, disNext = false, disPrev = false;
    bool disShuffle = false, disRepeat = false;
    int liked = -1;
};

class SpotifyAuth {
   public:
    // Sets the client ID and loads the stored sign-in for it, if any.
    void Load(const std::wstring& clientId) {
        std::lock_guard<std::mutex> lock(m_);
        clientId_ = clientId;
        access_.clear();
        refresh_.clear();
        expiry_ = 0;
        if (clientId.empty()) return;
        if (GetStoredString(L"spotifyClient") != clientId) return;
        refresh_ = UnprotectSecret(ToUtf8(GetStoredString(L"spotifyToken")));
    }

    // Forgets the stored sign-in.
    void Clear() {
        std::lock_guard<std::mutex> lock(m_);
        access_.clear();
        refresh_.clear();
        expiry_ = 0;
        Wh_DeleteValue(L"spotifyToken");
        Wh_DeleteValue(L"spotifyClient");
    }

    bool HasToken() {
        std::lock_guard<std::mutex> lock(m_);
        return !refresh_.empty() && !clientId_.empty();
    }

    std::wstring ClientId() {
        std::lock_guard<std::mutex> lock(m_);
        return clientId_;
    }

    void Store(const std::string& access, int64_t expiresIn, const std::string& refresh) {
        std::lock_guard<std::mutex> lock(m_);
        access_ = access;
        expiry_ = Tick() + (ULONGLONG)std::max<int64_t>(60, expiresIn - 120) * 1000;
        if (!refresh.empty() && refresh != refresh_) {
            refresh_ = refresh;
            Wh_SetStringValue(L"spotifyToken", ToWide(ProtectSecret(refresh)).c_str());
            Wh_SetStringValue(L"spotifyClient", clientId_.c_str());
        }
    }

    // Returns a valid access token, refreshing it if needed. Empty on failure.
    std::string AccessToken(bool force, bool* authLost) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!force && !access_.empty() && Tick() < expiry_) return access_;
            if (refresh_.empty()) return {};
        }
        std::lock_guard<std::mutex> refreshLock(refreshMutex_);
        std::string refresh, client, previous;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (!access_.empty() && Tick() < expiry_ && (!force || Tick() - lastRefresh_ < 5000))
                return access_;
            refresh = refresh_;
            client = ToUtf8(clientId_);
        }
        if (refresh.empty()) return {};
        std::string body = "grant_type=refresh_token&refresh_token=" + UrlEncode(refresh) +
                           "&client_id=" + UrlEncode(client);
        auto r = Http::Request(L"POST", L"https://accounts.spotify.com/api/token", body,
                               L"Content-Type: application/x-www-form-urlencoded\r\n");
        Json j;
        if (r.status == 200 && Json::Parse(r.body, j) && !j["access_token"].Str().empty()) {
            Store(j["access_token"].Str(), j["expires_in"].Int(3600), j["refresh_token"].Str());
            std::lock_guard<std::mutex> lock(m_);
            lastRefresh_ = Tick();
            return access_;
        }
        if (r.status == 400 || r.status == 401) {
            Json::Parse(r.body, j);
            std::string err = j["error"].Str();
            if (err == "invalid_grant" || err == "invalid_client" || r.status == 401) {
                Wh_Log(L"Spotify refresh token rejected: %S", err.c_str());
                Clear();
                if (authLost) *authLost = true;
            }
        }
        return {};
    }

   private:
    std::mutex m_, refreshMutex_;
    std::wstring clientId_;
    std::string access_, refresh_;
    ULONGLONG expiry_ = 0, lastRefresh_ = 0;
};

static SpotifyAuth g_auth;
static std::atomic<uint32_t> g_spotRateLimits{0};  // real 429 answers, from any request
static std::atomic<uint32_t> g_pollGen{0};

static void NotifyAuthLost();
static void NotifySpotifyState(SpotifyState st);
static void NotifyDevices(std::vector<DeviceInfo> devices, bool ok);
static void NotifyToast(std::wstring msg);

static std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

// Spotify answers 429 for two different limits:
// * a rate limit over a rolling 30 s window, with a short Retry-After;
// * the quota of development-mode apps (shared by all apps of a developer
//   account, body reason "QUOTA_EXCEEDED"). Once it is used up Spotify
//   refuses requests for many hours.
// Endpoints are limited in groups (the library "contains" check is much
// stricter than the player), so waits are tracked per family.
enum class ApiFamily : int { Player = 0, Library = 1, Other = 2 };
static std::atomic<ULONGLONG> g_retryUntil[3] = {0, 0, 0};
static std::atomic<int> g_limitStreak[3] = {0, 0, 0};  // consecutive 429 answers
static std::atomic<uint32_t> g_spotRequests{0};        // requests actually sent
#ifdef TP_HARNESS
static std::wstring g_testApiBase;  // replaces https://api.spotify.com/v1
#endif

static ApiFamily FamilyOf(const std::wstring& pathQuery) {
    if (pathQuery.rfind(L"/me/player", 0) == 0) return ApiFamily::Player;
    if (pathQuery.rfind(L"/me/library", 0) == 0) return ApiFamily::Library;
    return ApiFamily::Other;
}

// The "reason" of a Spotify error answer, if any.
static std::string ApiErrorReason(const std::string& body) {
    Json j;
    if (!Json::Parse(body, j)) return {};
    return j["error"]["reason"].Str();
}

// How long to send no more requests of a family after a 429. `streak` counts
// the 429 answers in a row (1 for the first). Short rate limits are waited
// out as Spotify asks. A used-up quota, or a limit that keeps coming back,
// backs off from 2 up to 30 minutes, so the retries themselves don't keep
// the quota used up. Spotify is asked again at least every 30 minutes.
constexpr int kPlainLimitsBeforeBackoff = 5;
static DWORD RateLimitWaitMs(int streak, int retryAfterSec, bool quota) {
    static const DWORD kSteps[] = {120000, 300000, 600000, 1200000, 1800000};
    DWORD asked = (DWORD)std::clamp(retryAfterSec > 0 ? retryAfterSec : 5, 1, 1800) * 1000;
    int step = quota ? streak - 1 : streak - kPlainLimitsBeforeBackoff - 1;
    if (step < 0) return asked;
    return std::max(asked, kSteps[std::min(step, 4)]);
}

static ULONGLONG RetryWaitLeftMs(ApiFamily family) {
    ULONGLONG until = g_retryUntil[(int)family], now = Tick();
    return until > now ? until - now : 0;
}

// For user commands: waits until Spotify allows requests of this family
// again, instead of dropping the command. Longer blocks fail right away.
static void WaitForRetryWindow(const std::wstring& pathQuery, DWORD maxWaitMs) {
    ULONGLONG left = RetryWaitLeftMs(FamilyOf(pathQuery));
    if (!left || left > maxWaitMs) return;
    Trace(L"Waiting %llu ms for Spotify's rate limit before %s", left, pathQuery.c_str());
    ULONGLONG until = Tick() + left;
    while (!g_shuttingDown && Tick() < until) Sleep(50);
}

static HttpResult SpotifyApi(const wchar_t* method, const std::wstring& pathQuery,
                             const std::string& body = {}) {
    HttpResult r;
    ApiFamily family = FamilyOf(pathQuery);
    if (Tick() < g_retryUntil[(int)family]) {
        r.status = 429;
        r.throttled = true;
        return r;
    }
    std::wstring base = L"https://api.spotify.com/v1";
#ifdef TP_HARNESS
    if (!g_testApiBase.empty()) base = g_testApiBase;
#endif
    for (int attempt = 0; attempt < 2; attempt++) {
        bool lost = false;
        std::string token = g_auth.AccessToken(attempt > 0, &lost);
        if (token.empty()) {
            r.status = lost ? 401 : 0;
            if (lost) NotifyAuthLost();
            return r;
        }
        std::wstring headers = L"Authorization: Bearer " + ToWide(token) + L"\r\n";
        if (!body.empty()) headers += L"Content-Type: application/json\r\n";
        else if (wcscmp(method, L"PUT") == 0 || wcscmp(method, L"POST") == 0)
            headers += L"Content-Length: 0\r\n";
        g_spotRequests++;
        r = Http::Request(method, base + pathQuery, body, headers);
        if (r.status != 401) break;
    }
    if (r.status == 429) {
        std::string reason = ApiErrorReason(r.body);
        bool quota = reason == "QUOTA_EXCEEDED";
        int streak = ++g_limitStreak[(int)family];
        DWORD wait = RateLimitWaitMs(streak, r.retryAfter, quota);
        g_retryUntil[(int)family] = Tick() + wait;
        if (family == ApiFamily::Player) g_spotRateLimits++;  // only these pace the polling
        Trace(L"Spotify %s on %s %s (reason '%S', Retry-After '%s', %d in a row): next request in %u s",
              quota ? L"quota used up" : L"rate limit", method, pathQuery.c_str(), reason.c_str(),
              r.retryAfterText.c_str(), streak, (unsigned)(wait / 1000));
    } else if (r.status != 0) {
        int streak = g_limitStreak[(int)family].exchange(0);
        if (streak > kPlainLimitsBeforeBackoff)
            Trace(L"Spotify accepts requests again (after %d refusals): %s %d", streak, pathQuery.c_str(), r.status);
    }
    return r;
}

// User commands are never dropped because of a short rate limit: they wait
// it out (a few seconds at most) and retry once.
static HttpResult SpotifyCommand(const wchar_t* method, const std::wstring& pathQuery,
                                 const std::string& body = {}) {
    WaitForRetryWindow(pathQuery, 6000);
    HttpResult r = SpotifyApi(method, pathQuery, body);
    if (r.status == 429 && !g_shuttingDown) {
        WaitForRetryWindow(pathQuery, 6000);
        r = SpotifyApi(method, pathQuery, body);
    }
    return r;
}

static DeviceInfo ParseDevice(const Json& d) {
    DeviceInfo dev;
    dev.id = d["id"].WStr();
    dev.name = d["name"].WStr();
    dev.type = d["type"].WStr();
    dev.active = d["is_active"].Bool();
    dev.restricted = d["is_restricted"].Bool();
    dev.supportsVolume = d["supports_volume"].Bool(true);
    dev.volume = (int)d["volume_percent"].Int(-1);
    if (dev.volume < 0) dev.supportsVolume = false;
    return dev;
}

// Prefers the largest image up to 700px (Spotify serves 640, 300 and 64).
static std::wstring PickImage(const Json& images) {
    std::wstring best, first;
    int64_t bestW = -1;
    for (size_t i = 0; i < images.size(); i++) {
        std::wstring url = images[i]["url"].WStr();
        if (url.empty()) continue;
        if (first.empty()) first = url;
        int64_t w = images[i]["width"].Int(0);
        if (w <= 700 && w > bestW) {
            best = url;
            bestW = w;
        }
    }
    return best.empty() ? first : best;
}

static SpotifyState ParsePlayer(const Json& j) {
    SpotifyState st;
    st.ok = true;
    const Json& dev = j["device"];
    if (dev.IsObj()) {
        st.hasDevice = true;
        st.device = ParseDevice(dev);
    }
    st.playing = j["is_playing"].Bool();
    st.progressMs = j["progress_ms"].Int();
    st.shuffle = j["shuffle_state"].Bool();
    std::string rep = j["repeat_state"].Str();
    st.repeat = rep == "track" ? RepeatMode::Track : rep == "context" ? RepeatMode::Context
                                                                       : RepeatMode::Off;
    st.type = j["currently_playing_type"].Str();
    const Json& item = j["item"];
    if (item.IsObj()) {
        st.id = item["id"].WStr();
        st.uri = item["uri"].WStr();
        st.title = item["name"].WStr();
        st.durationMs = item["duration_ms"].Int();
        if (item["type"].Str() == "episode") {
            st.type = "episode";
            st.artist = item["show"]["name"].WStr();
            st.imageUrl = PickImage(item["images"]);
            if (st.imageUrl.empty()) st.imageUrl = PickImage(item["show"]["images"]);
        } else {
            if (st.type.empty() || st.type == "unknown") st.type = "track";
            const Json& artists = item["artists"];
            for (size_t i = 0; i < artists.size(); i++) {
                std::wstring name = artists[i]["name"].WStr();
                if (name.empty()) continue;
                if (!st.artist.empty()) st.artist += L", ";
                st.artist += name;
            }
            st.album = item["album"]["name"].WStr();
            st.imageUrl = PickImage(item["album"]["images"]);
        }
    } else if (st.type == "ad") {
        st.title = L"Advertisement";
        st.artist = L"Spotify";
    }
    const Json& dis = j["actions"]["disallows"];
    st.disPause = dis["pausing"].Bool();
    st.disResume = dis["resuming"].Bool();
    st.disSeek = dis["seeking"].Bool();
    st.disNext = dis["skipping_next"].Bool();
    st.disPrev = dis["skipping_prev"].Bool();
    st.disShuffle = dis["toggling_shuffle"].Bool();
    st.disRepeat = dis["toggling_repeat_context"].Bool() && dis["toggling_repeat_track"].Bool();
    return st;
}

// Polling the player. The quota of development-mode apps is small and, once
// used up, Spotify blocks the app for many hours (polling every second while
// music played used it up in one evening). So the player is asked:
// * every 5 s while something plays (2 s while the popup is open), and right
//   when the current track should end, so track changes still show on time;
// * every 15 s while the local Spotify app plays (it reports changes itself);
// * less often while paused or when no device is active;
// * right away after a command, or a change seen in the local Spotify app.
constexpr DWORD kPollPlaying = 5000, kPollPlayingLocal = 15000, kPollPlayingPopup = 2000;
constexpr DWORD kPollPaused = 15000, kPollPausedLong = 30000, kPollPausedPopup = 3000;
constexpr DWORD kPollIdle = 30000, kPollIdleLong = 60000, kPollIdlePopup = 5000, kPollBlip = 3000;
constexpr DWORD kPollAtTrackEndMs = 800;   // after the expected end of the track
constexpr DWORD kEventPollGapMs = 1500;    // local app events poll at most this often
constexpr ULONGLONG kLongPauseMs = 600000;

// Slows polling down when Spotify rate-limits: every real 429 on the player
// adds 50% to the intervals (up to 3x); 5 minutes without one take 25% off.
struct PollPacer {
    static constexpr int kMinPct = 100, kMaxPct = 300, kSlower = 50, kFaster = 25;
    static constexpr ULONGLONG kCalmMs = 300000;
    int percent = kMinPct;
    ULONGLONG lastChange = 0;

    // Called after each poll with whether Spotify rate-limited any player
    // request since the previous poll. Returns true when the pace changed.
    bool OnPoll(bool rateLimited, ULONGLONG now) {
        int old = percent;
        if (rateLimited) {
            percent = std::min(kMaxPct, percent + kSlower);
            lastChange = now;
        } else if (percent > kMinPct && now - lastChange >= kCalmMs) {
            percent = std::max(kMinPct, percent - kFaster);
            lastChange = now;
        }
        return percent != old;
    }
    DWORD Scale(DWORD ms) const { return (DWORD)((uint64_t)ms * (uint64_t)percent / 100); }
};

// What the last poll found, for choosing when to poll next.
struct PollSituation {
    bool ok = true;
    int failures = 0;  // failed polls in a row
    bool hasDevice = false, playing = false;
    bool popupOpen = false;
    bool localSpotifyPlaying = false;  // the local Spotify app plays (and reports changes)
    int64_t remainingMs = -1;          // left in the current track, -1 unknown
    int endPolls = 0;                  // polls made at the end of this track already
    ULONGLONG pausedMs = 0;            // how long playback has been paused
    ULONGLONG sinceDeviceMs = ~0ULL;   // since a device was last reported
};

static DWORD PollDelay(const PollSituation& s, const PollPacer& pacer) {
    if (!s.ok) {
        // A hiccup: retry soon a few times, then back off (a 429 also waits
        // out its block, see DoPoll).
        return s.failures <= 3 ? 3000 : (DWORD)std::min(60000, 3000 << std::min(s.failures - 3, 5));
    }
    DWORD next;
    if (s.hasDevice && s.playing) {
        next = pacer.Scale(s.popupOpen ? kPollPlayingPopup : s.localSpotifyPlaying ? kPollPlayingLocal : kPollPlaying);
        if (s.remainingMs > 0) {
            next = (DWORD)std::min<int64_t>(next, s.remainingMs + kPollAtTrackEndMs);
        } else if (s.remainingMs == 0 && s.endPolls < 3) {
            next = std::min<DWORD>(next, 1500);  // the next track hasn't been reported yet
        }
    } else if (s.hasDevice) {
        next = pacer.Scale(s.popupOpen ? kPollPausedPopup : s.pausedMs > kLongPauseMs ? kPollPausedLong : kPollPaused);
    } else if (s.sinceDeviceMs < 10000) {
        next = kPollBlip;  // a device was there a moment ago: probably a blip
    } else {
        next = pacer.Scale(s.popupOpen ? kPollIdlePopup : s.sinceDeviceMs > kLongPauseMs ? kPollIdleLong : kPollIdle);
    }
    return std::max<DWORD>(next, 1000);
}

// State owned by the polling thread (g_spot).
struct PollMemo {
    std::wstring likedUri;
    int liked = -1;
    ULONGLONG likedCheckTick = 0;
    int failures = 0;
    ULONGLONG lastDeviceTick = 0, pausedSince = 0;
    std::wstring lastAnswer;  // for logging changes only
    PollPacer pacer;
    uint32_t seenRateLimits = 0;  // value of g_spotRateLimits at the last poll
    std::wstring seenUri;         // track currently reported as playing...
    ULONGLONG seenSince = 0;      // ...and since when
    int endPolls = 0;             // polls at the end of that track
    ULONGLONG likeCheckNotBefore = 0;
    ULONGLONG hourStart = 0;      // for the hourly request count in the log
    uint32_t hourRequests = 0;
};
constexpr ULONGLONG kLikeCheckSettleMs = 1500;    // track must stay this long before the heart check
constexpr ULONGLONG kLikeCheckBackoffMs = 20000;  // pause heart checks after a 429
constexpr ULONGLONG kLikeRecheckMs = 60000;       // re-check the heart this often while the popup is open
static PollMemo g_pollMemo;
static std::atomic<bool> g_localSpotifyPlaying{false};  // set by the controller
static std::atomic<ULONGLONG> g_lastPollTick{0};

static void DoPoll();

static void SchedulePoll(DWORD delayMs) {
    uint32_t gen = ++g_pollGen;
    g_spot.Post(
        [gen] {
            if (gen == g_pollGen) DoPoll();
        },
        delayMs);
}

// A change seen in the local Spotify app: poll soon, but not more often than
// every kEventPollGapMs however many events the app sends.
static void SchedulePollSoon() {
    ULONGLONG since = Tick() - g_lastPollTick;
    SchedulePoll(since >= kEventPollGapMs ? 120 : (DWORD)(kEventPollGapMs - since));
}

static void DoPoll() {
    if (g_shuttingDown || !g_auth.HasToken()) return;
    auto& memo = g_pollMemo;
    ULONGLONG t0 = Tick();
    g_lastPollTick = t0;
    HttpResult r = SpotifyApi(L"GET", L"/me/player?additional_types=episode");
    ULONGLONG t1 = Tick();
    if (g_shuttingDown) return;
    if (!memo.hourStart) {
        memo.hourStart = t1;
        memo.hourRequests = g_spotRequests;
    } else if (t1 - memo.hourStart >= 3600000) {
        uint32_t total = g_spotRequests;
        Trace(L"Spotify requests in the last hour: %u", total - memo.hourRequests);
        memo.hourStart = t1;
        memo.hourRequests = total;
    }

    SpotifyState st;
    if (r.status == 200) {
        Json j;
        if (Json::Parse(r.body, j)) st = ParsePlayer(j);
    } else if (r.status == 204) {
        st.ok = true;
    }
    st.tick = t0 + (t1 - t0) / 2;
    memo.failures = st.ok ? 0 : memo.failures + 1;
    {
        // Player requests (polls, commands) that Spotify rate-limited since
        // the last poll slow polling down.
        uint32_t limits = g_spotRateLimits;
        bool limited = limits != memo.seenRateLimits;
        memo.seenRateLimits = limits;
        if (memo.pacer.OnPoll(limited, Tick()))
            Trace(L"Spotify polling pace is now %d%% of the normal interval", memo.pacer.percent);
    }
    {
        // Log changes in what Spotify answers (not every poll).
        wchar_t key[160];
        swprintf(key, ARRAYSIZE(key), L"status=%d device=%d item=%d playing=%d type=%S", r.status, st.hasDevice,
                 !st.title.empty(), st.playing, st.type.c_str());
        if (memo.lastAnswer != key) {
            memo.lastAnswer = key;
            Trace(L"Spotify answer: %s%s", key, st.hasDevice ? (L" on '" + st.device.name + L"'").c_str() : L"");
        }
    }

    if (st.ok && st.uri != memo.seenUri) {
        memo.seenUri = st.uri;
        memo.seenSince = Tick();
        memo.endPolls = 0;
    }
    if (st.ok && !st.uri.empty() && (st.type == "track" || st.type == "episode") &&
        st.uri.rfind(L"spotify:local:", 0) != 0) {
        // Spotify limits this check tightly, so it runs only once a track has
        // stayed for a moment (not while skipping) and backs off after a 429.
        bool stable = Tick() - memo.seenSince >= kLikeCheckSettleMs;
        bool newTrack = st.uri != memo.likedUri;
        bool recheck = g_popupOpen && Tick() - memo.likedCheckTick > kLikeRecheckMs;
        if ((newTrack || recheck) && stable && Tick() >= memo.likeCheckNotBefore) {
            auto lr = SpotifyApi(L"GET", L"/me/library/contains?uris=" + ToWide(UrlEncode(ToUtf8(st.uri))));
            Json lj;
            if (lr.status == 200 && Json::Parse(lr.body, lj) && lj.size()) {
                memo.liked = lj[0].Bool() ? 1 : 0;
                memo.likedUri = st.uri;
                memo.likedCheckTick = Tick();
            } else if (lr.status == 429) {
                memo.likeCheckNotBefore = Tick() + kLikeCheckBackoffMs;  // try this track again later
            } else {
                memo.liked = -1;  // unknown; don't keep asking for this track
                memo.likedUri = st.uri;
                memo.likedCheckTick = Tick();
            }
        }
        st.liked = st.uri == memo.likedUri ? memo.liked : -1;
    }

    if (r.status != 401) NotifySpotifyState(st);

    PollSituation sit;
    sit.ok = st.ok;
    sit.failures = memo.failures;
    sit.popupOpen = g_popupOpen;
    sit.localSpotifyPlaying = g_localSpotifyPlaying;
    if (st.ok && st.hasDevice) {
        memo.lastDeviceTick = Tick();
        sit.hasDevice = true;
        sit.playing = st.playing;
        if (st.playing) {
            memo.pausedSince = 0;
            if (st.durationMs > 0) {
                int64_t elapsed = st.progressMs + (int64_t)(Tick() - st.tick);
                sit.remainingMs = std::max<int64_t>(0, st.durationMs - elapsed);
                if (sit.remainingMs == 0) sit.endPolls = memo.endPolls++;
            }
        } else {
            if (!memo.pausedSince) memo.pausedSince = Tick();
            sit.pausedMs = Tick() - memo.pausedSince;
        }
    }
    if (memo.lastDeviceTick) sit.sinceDeviceMs = Tick() - memo.lastDeviceTick;
    DWORD next = PollDelay(sit, memo.pacer);
    ULONGLONG wait = RetryWaitLeftMs(ApiFamily::Player);
    if (wait) next = std::max<DWORD>(next, (DWORD)wait + 50);
    if (r.status == 401 && !g_auth.HasToken()) return;
    SchedulePoll(next);
}

static void FetchDevices() {
    auto r = SpotifyApi(L"GET", L"/me/player/devices");
    std::vector<DeviceInfo> devices;
    bool ok = false;
    Json j;
    if (r.status == 200 && Json::Parse(r.body, j)) {
        ok = true;
        const Json& list = j["devices"];
        for (size_t i = 0; i < list.size(); i++) devices.push_back(ParseDevice(list[i]));
    }
    NotifyDevices(std::move(devices), ok);
}

static void ReportApiError(const HttpResult& r, ApiFamily family) {
    std::wstring msg;
    if (r.status == 0) {
        msg = L"Couldn't reach Spotify";
    } else if (r.status == 429) {
        ULONGLONG left = RetryWaitLeftMs(family);
        if (left > 60000)
            msg = L"Spotify's request limit was reached, try again in " +
                  std::to_wstring((left + 59999) / 60000) + L" min";
        else
            msg = L"Spotify is busy, try again in a moment";
    } else {
        Json j;
        Json::Parse(r.body, j);
        std::string reason = j["error"]["reason"].Str();
        if (reason == "PREMIUM_REQUIRED") msg = L"Spotify Premium is required for this";
        else if (reason == "NO_ACTIVE_DEVICE" || r.status == 404) msg = L"No active Spotify device";
        else if (r.status == 403) msg = L"Spotify doesn't allow this right now";
        else if (r.status == 401) msg = L"Spotify sign-in expired";
        else msg = L"Spotify request failed (" + std::to_wstring(r.status) + L")";
    }
    NotifyToast(msg);
}

// ---------------------------------------------------------------------------
// Sign-in (authorization code flow with PKCE and a loopback redirect)
// ---------------------------------------------------------------------------

static std::thread& g_authThread = *new std::thread();  // never destroyed, see g_core
static std::atomic<bool> g_authCancel{false};
static std::atomic<bool> g_authRunning{false};
static std::atomic<bool> g_authReopen{false};  // open the sign-in page again
#ifdef TP_HARNESS
static std::function<void(const std::string&)> g_testOpenUrl;
#endif

static void NotifyAuthFinished(bool success, std::wstring error);

static std::map<std::string, std::string> ParseQuery(const std::string& q) {
    std::map<std::string, std::string> m;
    size_t i = 0;
    while (i <= q.size()) {
        size_t amp = q.find('&', i);
        if (amp == std::string::npos) amp = q.size();
        std::string part = q.substr(i, amp - i);
        size_t eq = part.find('=');
        if (!part.empty()) {
            if (eq == std::string::npos) m[UrlDecode(part)] = "";
            else m[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
        }
        i = amp + 1;
    }
    return m;
}

static void SendHtml(SOCKET c, int status, const std::string& title, const std::string& text) {
    std::string html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Taskbar Player</title>"
        "<style>body{margin:0;height:100vh;display:flex;align-items:center;justify-content:center;"
        "background:#121212;color:#fff;font-family:'Segoe UI Variable Text','Segoe UI',sans-serif}"
        ".c{text-align:center;max-width:420px;padding:24px}h1{font-size:22px;font-weight:600;"
        "margin:0 0 8px}p{color:#b3b3b3;font-size:15px;margin:0}</style></head><body><div class=\"c\">"
        "<h1>" + title + "</h1><p>" + text + "</p></div></body></html>";
    std::string resp = "HTTP/1.1 " + std::to_string(status) +
                       (status == 200 ? " OK" : " Not Found") +
                       "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                       std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
    send(c, resp.data(), (int)resp.size(), 0);
}

// Opens a URL in the default browser. Returns false if nothing could open it.
static bool OpenUrl(const std::string& url) {
    std::wstring w = ToWide(url);
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = w.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        Trace(L"Opened the sign-in page in the browser");
        return true;
    }
    DWORD err = GetLastError();
    Trace(L"ShellExecuteEx failed (%u), trying rundll32", err);
    std::wstring cmd = L"rundll32.exe url.dll,FileProtocolHandler " + w;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Trace(L"Opened the sign-in page via rundll32");
        return true;
    }
    Trace(L"Couldn't open the browser (%u)", GetLastError());
    return false;
}

static void RunAuthFlow(std::wstring clientId, int port) {
    Trace(L"Sign-in started (client id length %d, port %d)", (int)clientId.size(), port);
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::wstring error;
    bool success = false;
    WSADATA wsa;
    bool wsaOk = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    SOCKET ls = INVALID_SOCKET;
    do {
        if (!wsaOk) {
            error = L"Network initialization failed";
            break;
        }
        ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET) {
            Trace(L"socket() failed: %d", WSAGetLastError());
            error = L"Couldn't start the sign-in listener";
            break;
        }
        BOOL exclusive = TRUE;
        setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 4) != 0) {
            Trace(L"bind/listen on 127.0.0.1:%d failed: %d", port, WSAGetLastError());
            error = L"Port " + std::to_wstring(port) + L" is already in use";
            break;
        }
        Trace(L"Listening on 127.0.0.1:%d", port);

        std::string verifier = RandomToken(64);
        unsigned char hash[32];
        if (!Sha256(verifier, hash)) {
            error = L"Sign-in failed (crypto)";
            break;
        }
        std::string challenge = Base64Url(hash, 32);
        std::string state = RandomToken(24);
        std::string redirect = "http://127.0.0.1:" + std::to_string(port) + "/callback";
        std::string scopes =
            "user-read-playback-state user-modify-playback-state user-read-currently-playing "
            "user-library-read user-library-modify";
        std::string url = "https://accounts.spotify.com/authorize?response_type=code&client_id=" +
                          UrlEncode(ToUtf8(clientId)) + "&scope=" + UrlEncode(scopes) +
                          "&redirect_uri=" + UrlEncode(redirect) +
                          "&code_challenge_method=S256&code_challenge=" + challenge +
                          "&state=" + state;
        auto open = [&] {
#ifdef TP_HARNESS
            if (g_testOpenUrl) {
                g_testOpenUrl(url);
                return;
            }
#endif
            if (!OpenUrl(url)) error = L"Couldn't open the browser";
        };
        open();

        ULONGLONG deadline = Tick() + 5 * 60 * 1000;
        std::string code;
        while (!g_authCancel && Tick() < deadline && code.empty() && error.empty()) {
            if (g_authReopen.exchange(false)) open();
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(ls, &fds);
            timeval tv{1, 0};
            int sel = select(0, &fds, nullptr, nullptr, &tv);
            if (sel <= 0) continue;
            SOCKET c = accept(ls, nullptr, nullptr);
            if (c == INVALID_SOCKET) continue;
            DWORD timeout = 5000;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
            std::string req;
            char buf[4096];
            while (req.find("\r\n\r\n") == std::string::npos && req.size() < 32768) {
                int n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, n);
            }
            std::string target;
            size_t sp1 = req.find(' ');
            size_t sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
            if (sp2 != std::string::npos) target = req.substr(sp1 + 1, sp2 - sp1 - 1);
            size_t qm = target.find('?');
            std::string path = target.substr(0, qm);
            Trace(L"Sign-in listener got a request for %S", path.c_str());
            if (path != "/callback") {
                SendHtml(c, 404, "Not found", "");
                closesocket(c);
                continue;
            }
            auto params = ParseQuery(qm == std::string::npos ? "" : target.substr(qm + 1));
            if (params["state"] != state) {
                SendHtml(c, 200, "Sign-in failed", "The request didn't match. Please try again.");
                error = L"Spotify sign-in failed";
            } else if (!params["error"].empty()) {
                SendHtml(c, 200, "Sign-in cancelled", "You can close this tab.");
                error = L"Spotify sign-in was cancelled";
            } else if (!params["code"].empty()) {
                code = params["code"];
                SendHtml(c, 200, "You're connected",
                         "Taskbar Player can now control Spotify. You can close this tab.");
            } else {
                SendHtml(c, 200, "Sign-in failed", "Please try again.");
                error = L"Spotify sign-in failed";
            }
            shutdown(c, SD_SEND);
            closesocket(c);
        }
        if (code.empty()) {
            if (error.empty() && !g_authCancel) error = L"Spotify sign-in timed out";
            break;
        }
        std::string body = "grant_type=authorization_code&code=" + UrlEncode(code) +
                           "&redirect_uri=" + UrlEncode(redirect) + "&client_id=" +
                           UrlEncode(ToUtf8(clientId)) + "&code_verifier=" + verifier;
        auto r = Http::Request(L"POST", L"https://accounts.spotify.com/api/token", body,
                               L"Content-Type: application/x-www-form-urlencoded\r\n");
        Json j;
        if (r.status == 200 && Json::Parse(r.body, j) && !j["refresh_token"].Str().empty() &&
            g_auth.ClientId() == clientId) {
            g_auth.Store(j["access_token"].Str(), j["expires_in"].Int(3600), j["refresh_token"].Str());
            success = true;
        } else if (!g_authCancel) {
            Json::Parse(r.body, j);
            Trace(L"Token exchange failed: %d %S", r.status, j["error"].Str().c_str());
            error = L"Spotify sign-in failed (" + std::to_wstring(r.status) + L")";
        }
    } while (false);
    Trace(L"Sign-in finished: success=%d cancelled=%d error='%s'", success, (int)g_authCancel, error.c_str());
    if (ls != INVALID_SOCKET) closesocket(ls);
    if (wsaOk) WSACleanup();
    if (SUCCEEDED(coHr)) CoUninitialize();
    g_authRunning = false;
    if (!g_authCancel || success) NotifyAuthFinished(success, error);
}

// ============================================================================
// Controller: merges media sessions and Spotify into one snapshot (core thread)
// ============================================================================

enum class Act {
    PlayPause,  // v = 1 play, 0 pause
    Next,
    Prev,
    Seek,      // v = position ms
    Shuffle,   // v = desired state
    Repeat,    // v = desired RepeatMode
    Volume,    // v = percent
    Like,      // v = desired state
    Transfer,  // s = device id, v = keep playing
    Connect,
    SignOut,
    DevicesOpened,
    PopupOpened,
    PopupClosed,
};

struct SessionRec {
    uint64_t id = 0;
    wmc::GlobalSystemMediaTransportControlsSession s{nullptr};
    winrt::event_token tMedia{}, tPlayback{}, tTimeline{};
    std::wstring aumid;
    bool isSpotify = false;
    // Metadata as last reported by the app...
    std::wstring rawTitle, rawArtist, rawAlbum;
    std::shared_ptr<Art> rawArt;
    uint32_t metaGen = 0;
    // ...and the stable version that is shown (browsers change metadata a lot,
    // e.g. YouTube hover previews, so changes are applied after a short delay).
    std::wstring title, artist, album;
    int status = 0;
    bool playing = false;
    ULONGLONG playingSince = 0;
    int64_t posMs = 0, durMs = 0, startMs = 0;
    ULONGLONG posTick = 0;
    double rate = 1.0;
    bool canPlayPause = false, canNext = false, canPrev = false, canSeek = false;
    bool canShuffle = false, canRepeat = false;
    bool shuffle = false;
    RepeatMode repeat = RepeatMode::Off;
    ULONGLONG lastActive = 0;
    uint32_t artGen = 0;
    std::wstring artKey;
    std::shared_ptr<Art> art;

    int64_t PosNow() const {
        int64_t p = posMs + (playing ? (int64_t)((double)(Tick() - posTick) * rate) : 0);
        if (durMs > 0) p = std::min(p, durMs);
        return std::max<int64_t>(0, p);
    }
    bool RawMatchesShown() const {
        return rawTitle == title && rawArtist == artist && rawAlbum == album;
    }
};

// Stabilization delays (ms).
constexpr DWORD kMetaSettleMs = 1000;      // browser metadata must be stable this long
constexpr DWORD kEmptyMetaSettleMs = 3000; // apps often clear metadata between items
constexpr DWORD kNewPlaybackMs = 1500;     // a newly started session must play this long to take over
constexpr DWORD kLostGraceMs = 1500;       // keep showing the last state when a source vanishes briefly
constexpr DWORD kCarryOverMs = 5000;       // a re-created session inherits what was shown
constexpr DWORD kSpotifyBlipMs = 6000;     // ignore brief "nothing playing" answers from Spotify
constexpr DWORD kSessionGoneGraceMs = 2000; // hold the shown session while the app re-creates it

// What a removed session was showing, so a replacement can pick it up.
struct ShownCarry {
    std::wstring title, artist, album;
    std::shared_ptr<Art> art;
    bool playing = false;
    ULONGLONG playingSince = 0, lastActive = 0, removedTick = 0;
};

class Controller {
   public:
    void Init() {
        g_auth.Load(GetSettings()->clientId);
        g_feed.Start();
        try {
            auto op = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
            if (op.wait_for(std::chrono::milliseconds(10000)) == winrt::Windows::Foundation::AsyncStatus::Completed)
                mgr_ = op.GetResults();
            if (!mgr_) throw winrt::hresult_error(E_FAIL);
            tSessions_ = mgr_.SessionsChanged([](auto&&, auto&&) {
                g_core.Post([] { g_ctl().OnSessionsChanged(); });
            });
            tCurrent_ = mgr_.CurrentSessionChanged([](auto&&, auto&&) {
                g_core.Post([] { g_ctl().Publish(); });
            });
        } catch (...) {
            Wh_Log(L"Media session manager unavailable");
            mgr_ = nullptr;
        }
        OnSessionsChanged();
        if (g_auth.HasToken()) SchedulePoll(0);
        g_core.Post([] { g_ctl().PeriodicTick(); }, 3000);
        Publish();
    }

    void Shutdown() {
        g_authCancel = true;
        g_feed.Stop();
        for (auto& r : sessions_) Unsubscribe(*r);
        sessions_.clear();
        if (mgr_) {
            try {
                mgr_.SessionsChanged(tSessions_);
                mgr_.CurrentSessionChanged(tCurrent_);
            } catch (...) {
            }
            mgr_ = nullptr;
        }
        artCache_.clear();
        apps_.clear();
        devices_.clear();
        removed_.clear();
        lastShown_ = nullptr;
        spot_ = SpotifyState();
        std::lock_guard<std::mutex> lock(g_snapMutex);
        g_snap = nullptr;
    }

    void OnSessionsChanged() {
        if (!mgr_) return;
        std::vector<std::unique_ptr<SessionRec>> next;
        std::vector<SessionRec*> added;
        try {
            auto list = mgr_.GetSessions();
            for (auto const& s : list) {
                auto it = std::find_if(sessions_.begin(), sessions_.end(),
                                       [&](const std::unique_ptr<SessionRec>& r) { return r->s == s; });
                if (it != sessions_.end()) {
                    next.push_back(std::move(*it));
                    sessions_.erase(it);
                    continue;
                }
                auto rec = std::make_unique<SessionRec>();
                rec->id = nextId_++;
                rec->s = s;
                rec->aumid = s.SourceAppUserModelId().c_str();
                rec->isSpotify = IContains(rec->aumid, L"spotify");
                rec->lastActive = Tick();
                uint64_t id = rec->id;
                rec->tMedia = s.MediaPropertiesChanged([id](auto&&, auto&&) {
                    g_core.Post([id] { g_ctl().OnSessionEvent(id, 0); });
                });
                rec->tPlayback = s.PlaybackInfoChanged([id](auto&&, auto&&) {
                    g_core.Post([id] { g_ctl().OnSessionEvent(id, 1); });
                });
                rec->tTimeline = s.TimelinePropertiesChanged([id](auto&&, auto&&) {
                    g_core.Post([id] { g_ctl().OnSessionEvent(id, 2); });
                });
                added.push_back(rec.get());
                next.push_back(std::move(rec));
            }
        } catch (...) {
            Wh_Log(L"GetSessions failed");
        }
        // Browsers sometimes replace their session object for the same media:
        // inherit what was shown so the widget doesn't flash.
        for (SessionRec* rec : added) {
            auto old = std::find_if(sessions_.begin(), sessions_.end(), [&](const std::unique_ptr<SessionRec>& r) {
                return r && r->aumid == rec->aumid && !(r->title.empty() && r->artist.empty());
            });
            if (old != sessions_.end()) {
                SessionRec& o = **old;
                rec->title = o.title;
                rec->artist = o.artist;
                rec->album = o.album;
                rec->art = o.art;
                rec->rawTitle = o.rawTitle;
                rec->rawArtist = o.rawArtist;
                rec->rawAlbum = o.rawAlbum;
                rec->rawArt = o.rawArt;
                rec->playingSince = o.playingSince;
                rec->playing = o.playing;
                rec->lastActive = o.lastActive;
                Unsubscribe(o);
                sessions_.erase(old);
            } else {
                auto carry = removed_.find(rec->aumid);
                if (carry != removed_.end() && Tick() - carry->second.removedTick < kCarryOverMs) {
                    const ShownCarry& c = carry->second;
                    rec->title = rec->rawTitle = c.title;
                    rec->artist = rec->rawArtist = c.artist;
                    rec->album = rec->rawAlbum = c.album;
                    rec->art = rec->rawArt = c.art;
                    rec->playing = c.playing;
                    rec->playingSince = c.playingSince;
                    rec->lastActive = c.lastActive;
                }
            }
            try {
                RefreshPlayback(*rec);
                RefreshTimeline(*rec);
                RefreshMedia(*rec);
            } catch (...) {
            }
            App(rec->aumid);
        }
        for (auto it = removed_.begin(); it != removed_.end();) {
            if (Tick() - it->second.removedTick >= kCarryOverMs) it = removed_.erase(it);
            else ++it;
        }
        for (auto& old : sessions_) {
            if (!(old->title.empty() && old->artist.empty())) {
                ShownCarry& c = removed_[old->aumid];
                c.title = old->title;
                c.artist = old->artist;
                c.album = old->album;
                c.art = old->art;
                c.playing = old->playing;
                c.playingSince = old->playingSince;
                c.lastActive = old->lastActive;
                c.removedTick = Tick();
            }
            Unsubscribe(*old);
        }
        sessions_ = std::move(next);
        Wh_Log(L"Media sessions: %d", (int)sessions_.size());
        for (auto& r : sessions_)
            Wh_Log(L"  %s status=%d title='%s'", r->aumid.c_str(), r->status, r->title.c_str());
        Publish();
    }

    void OnSessionEvent(uint64_t id, int kind) {
        SessionRec* r = Find(id);
        if (!r) return;
        try {
            if (kind == 0) RefreshMedia(*r);
            else if (kind == 1) RefreshPlayback(*r);
            else RefreshTimeline(*r);
        } catch (...) {
        }
        // A local Spotify change is visible here first: ask the Web API right away.
        if (r->isSpotify && kind != 2 && g_auth.HasToken()) SchedulePollSoon();
        Publish();
    }

    void OnSpotify(SpotifyState st) {
        if (!g_auth.HasToken()) return;
        if (!st.ok) {
            // Network or server hiccup: keep the last good state. It is
            // dropped only when nothing succeeds for a while (see Publish).
            Publish();
            return;
        }
        spotOkTick_ = Tick();
        bool hasContent = st.hasDevice && !st.title.empty();
        if (!hasContent && spotContent_ && Tick() - spotContentTick_ < kSpotifyBlipMs) {
            // Spotify sometimes reports "no device / nothing playing" for a
            // moment (Alexa devices especially). Ignore short blips.
            return;
        }
        spot_ = std::move(st);
        spotContent_ = hasContent;
        if (hasContent) spotContentTick_ = Tick();
        if (!spot_.imageUrl.empty()) RequestUrlArt(spot_.imageUrl);
        Publish();
    }

    void OnArt(const std::wstring& key, std::shared_ptr<Art> art) {
        artPending_.erase(key);
        if (!key.empty() && key[0] == L't') {
            for (auto& r : sessions_) {
                if (r->artKey != key || !art) continue;
                r->rawArt = art;
                if (r->RawMatchesShown()) r->art = art;
            }
        } else {
            artCache_[key] = art;
            artOrder_.erase(std::remove(artOrder_.begin(), artOrder_.end(), key), artOrder_.end());
            artOrder_.push_back(key);
            while (artOrder_.size() > 8) {
                artCache_.erase(artOrder_.front());
                artOrder_.erase(artOrder_.begin());
            }
        }
        Publish();
    }

    void OnAppInfo(const std::wstring& aumid, std::shared_ptr<AppInfo> info) {
        appsPending_.erase(aumid);
        apps_[aumid] = info;
        Publish();
    }

    void OnDevices(std::vector<DeviceInfo> devices, bool ok) {
        devicesLoading_ = false;
        if (ok) devices_ = std::move(devices);
        Publish();
    }

    void OnAuthLost() {
        spot_ = SpotifyState();
        spotContent_ = false;
        devices_.clear();
        ShowToast(L"Spotify sign-in expired, please connect again");
    }

    void OnAuthFinished(bool success, const std::wstring& error) {
        Trace(L"Auth finished on core: success=%d hasToken=%d", success, g_auth.HasToken());
        authPending_ = false;
        if (success) {
            authError_.clear();
            SchedulePoll(0);
        } else if (!error.empty()) {
            authError_ = error;
            authErrorTick_ = Tick();
            ShowToast(error);
        }
        Publish();
    }

    void OnSettingsChanged() {
        auto set = GetSettings();
        if (set->clientId != g_auth.ClientId()) {
            g_authCancel = true;
            authPending_ = false;
            g_auth.Clear();
            g_auth.Load(set->clientId);
            spot_ = SpotifyState();
            spotContent_ = false;
            devices_.clear();
            // A new Client ID was entered: go straight to the Spotify sign-in.
            if (!set->clientId.empty()) {
                Trace(L"Client ID changed, starting sign-in");
                g_core.Post(
                    [] {
                        if (!g_auth.HasToken() && !GetSettings()->clientId.empty() && !g_authRunning)
                            g_ctl().StartAuth();
                    },
                    1500);
            }
        }
        Publish();
    }

    void ShowToast(const std::wstring& msg) {
        toast_ = msg;
        toastSeq_++;
        Publish();
    }

    void HandleAct(Act a, int64_t v, const std::wstring& s) {
        Trace(L"Action %d (value %lld) source=%d", (int)a, (long long)v, (int)curSrc_);
        switch (a) {
            case Act::PopupOpened:
                popupOpen_ = true;
                if (g_auth.HasToken()) SchedulePoll(0);
                return;
            case Act::PopupClosed:
                popupOpen_ = false;
                Publish();
                return;
            case Act::DevicesOpened:
                if (g_auth.HasToken()) {
                    devicesLoading_ = true;
                    Publish();
                    g_spot.Post([] { FetchDevices(); });
                }
                return;
            case Act::Connect:
                StartAuth();
                return;
            case Act::SignOut:
                g_authCancel = true;
                g_auth.Clear();
                spot_ = SpotifyState();
                spotContent_ = false;
                devices_.clear();
                Publish();
                return;
            default:
                break;
        }
        if (curSrc_ == Src::Spotify) SpotifyCmd(a, v, s);
        else if (curSrc_ == Src::Session) SessionCmd(a, v);
    }

    void PeriodicTick() {
        if (g_shuttingDown) return;
        if (g_widgetVisible) g_img.Post([] { MeasureTaskbar(); });
        Publish();
        g_core.Post([] { g_ctl().PeriodicTick(); }, 3000);
    }

    void Publish() {
        if (g_shuttingDown) return;
        auto set = GetSettings();
        auto snap = std::make_shared<Snapshot>();
        snap->seq = ++seq_;
        bool spotOn = g_auth.HasToken();
        snap->spotifyConfigured = !set->clientId.empty();
        snap->spotifyAuthorized = spotOn;
        snap->authPending = authPending_;
        snap->toast = toast_;
        snap->toastSeq = toastSeq_;
        bool spotFresh = spotOn && spot_.ok && Tick() - spotOkTick_ < 20000;

        wmc::GlobalSystemMediaTransportControlsSession current{nullptr};
        try {
            if (mgr_) current = mgr_.GetCurrentSession();
        } catch (...) {
        }
        auto better = [&](SessionRec* a, SessionRec* b) {
            if (!b) return true;
            if (a->isSpotify != b->isSpotify) return a->isSpotify;
            bool ac = current && a->s == current, bc = current && b->s == current;
            if (ac != bc) return ac;
            return a->lastActive > b->lastActive;
        };
        // A session that just started playing (e.g. a hover preview) only takes
        // over once it has kept playing for a moment.
        auto established = [&](SessionRec* r) {
            return r->isSpotify || (curSrc_ == Src::Session && r->aumid == curAumid_) ||
                   (r->playingSince && Tick() - r->playingSince >= kNewPlaybackMs);
        };
        SessionRec* playingRec = nullptr;
        SessionRec* pausedRec = nullptr;
        SessionRec* spotifySession = nullptr;
        for (auto& up : sessions_) {
            SessionRec* r = up.get();
            if (r->isSpotify && !spotifySession) spotifySession = r;
            if (r->isSpotify && spotFresh) continue;
#ifdef TP_HARNESS
            if (!g_testOnlyAumid.empty() && !IContains(r->aumid, g_testOnlyAumid.c_str())) continue;
#endif
            if (r->title.empty() && r->artist.empty()) continue;
            if (r->playing && !established(r)) continue;
            if (r->playing) {
                if (better(r, playingRec)) playingRec = r;
            } else if (r->status == 5 /*Paused*/ || r->status == 2 /*Changing*/) {
                if (better(r, pausedRec)) pausedRec = r;
            }
        }

        // The local app reports its changes, so the Web API is polled less.
        g_localSpotifyPlaying = spotifySession && spotifySession->playing;
        bool spotHas = spotFresh && spot_.hasDevice && !spot_.title.empty();
        // The session shown right now, if it still exists.
        SessionRec* curRec = nullptr;
        if (curSrc_ == Src::Session && !curAumid_.empty()) {
            for (auto& up : sessions_) {
                if (up->aumid == curAumid_ && !(up->title.empty() && up->artist.empty())) {
                    curRec = up.get();
                    break;
                }
            }
        }
        bool curPaused = curRec && !curRec->playing && (curRec->status == 5 || curRec->status == 2);
        Src src = Src::None;
        SessionRec* rec = nullptr;
        if (spotHas && spot_.playing) {
            src = Src::Spotify;
        } else if (playingRec) {
            src = Src::Session;
            rec = playingRec;
        } else if (curPaused) {
            // Paused what was shown (e.g. a video): keep its controls.
            src = Src::Session;
            rec = curRec;
        } else if (spotHas) {
            src = Src::Spotify;
        } else if (pausedRec) {
            src = Src::Session;
            rec = pausedRec;
        }

        // The shown session vanished for a moment (browsers re-create their
        // session object, e.g. while scrolling YouTube): keep showing it
        // briefly instead of jumping to another source, unless Spotify plays.
        if (curSrc_ == Src::Session && !curAumid_.empty() && !curRec && lastShown_ &&
            !(src == Src::Spotify && spot_.playing) && !(rec && rec->aumid == curAumid_)) {
            auto it = removed_.find(curAumid_);
            if (it != removed_.end() && Tick() - it->second.removedTick < kSessionGoneGraceMs) {
                auto held = std::make_shared<Snapshot>(*lastShown_);
                held->seq = snap->seq;
                held->toast = snap->toast;
                held->toastSeq = snap->toastSeq;
                held->authPending = snap->authPending;
                held->spotifyAuthorized = snap->spotifyAuthorized;
                ULONGLONG left = kSessionGoneGraceMs - (Tick() - it->second.removedTick);
                g_core.Post([] { g_ctl().Publish(); }, (DWORD)left + 50);
                PostSnapshot(held);
                return;
            }
        }

        // "Hide when paused" support.
        std::wstring key = src == Src::Spotify ? L"spotify" : rec ? L"s|" + rec->aumid : L"";
        bool playing = src == Src::Spotify ? spot_.playing : rec ? rec->playing : false;
        if (key != curKey_ || playing != curPlaying_) {
            curKey_ = key;
            curPlaying_ = playing;
            pausedSince_ = playing ? 0 : Tick();
        }
        if (src != Src::None && !playing && set->hidePausedMinutes > 0 && pausedSince_ &&
            !popupOpen_ && Tick() - pausedSince_ > (ULONGLONG)set->hidePausedMinutes * 60000) {
            src = Src::None;
            rec = nullptr;
        }

        if (src == Src::Spotify) {
            FillFromSpotify(*snap, spotifySession);
        } else if (src == Src::Session) {
            FillFromSession(*snap, *rec);
        }
        if (src != Src::None) {
            lastShown_ = snap;
            lostSince_ = 0;
        } else if (lastShown_) {
            // The source vanished: keep the last state briefly in case it returns.
            if (!lostSince_) lostSince_ = Tick();
            ULONGLONG lost = Tick() - lostSince_;
            if (lost < kLostGraceMs) {
                auto held = std::make_shared<Snapshot>(*lastShown_);
                held->seq = snap->seq;
                held->toast = snap->toast;
                held->toastSeq = snap->toastSeq;
                held->authPending = snap->authPending;
                held->spotifyAuthorized = snap->spotifyAuthorized;
                g_core.Post([] { g_ctl().Publish(); }, (DWORD)(kLostGraceMs - lost) + 50);
                PostSnapshot(held);
                return;
            }
            lastShown_ = nullptr;
            lostSince_ = 0;
        }
        if (src == Src::None && snap->spotifyConfigured && !spotOn) {
            snap->visible = true;
            snap->connectPrompt = true;
            snap->isSpotify = true;
            snap->appName = L"Spotify";
            if (authPending_) {
                snap->title = L"Waiting for Spotify";
                snap->artist = L"Click to reopen the sign-in page";
                snap->trackKey = L"#connect-pending";
            } else {
                snap->title = L"Connect Spotify";
                bool recentError = !authError_.empty() && Tick() - authErrorTick_ < 15000;
                snap->artist = recentError ? authError_ : L"Click to sign in";
                snap->trackKey = L"#connect";
            }
        }
        curSrc_ = src;
        curSession_ = rec ? rec->id : 0;
        curAumid_ = rec ? rec->aumid : L"";
        std::wstring traceKey = std::to_wstring((int)src) + L"|" + std::to_wstring(snap->connectPrompt) + L"|" +
                                curAumid_ + L"|" + std::to_wstring(spotOn) + L"|" + std::to_wstring(authPending_);
        if (traceKey != lastTraceKey_) {
            lastTraceKey_ = traceKey;
            Trace(L"Showing: src=%d connectPrompt=%d app='%s' visible=%d spotifyAuthorized=%d authPending=%d",
                  (int)src, snap->connectPrompt, curAumid_.c_str(), snap->visible, spotOn, authPending_);
        }
        PostSnapshot(snap);
    }

    // Applies metadata that has stayed the same since it was reported.
    void CommitMeta(uint64_t id, uint32_t gen) {
        SessionRec* r = Find(id);
        if (!r || r->metaGen != gen) return;
        r->title = r->rawTitle;
        r->artist = r->rawArtist;
        r->album = r->rawAlbum;
        r->art = r->rawArt;
        Publish();
    }

   private:
    static Controller& g_ctl();

    SessionRec* Find(uint64_t id) {
        for (auto& r : sessions_)
            if (r->id == id) return r.get();
        return nullptr;
    }

    void Unsubscribe(SessionRec& r) {
        try {
            if (r.tMedia) r.s.MediaPropertiesChanged(r.tMedia);
            if (r.tPlayback) r.s.PlaybackInfoChanged(r.tPlayback);
            if (r.tTimeline) r.s.TimelinePropertiesChanged(r.tTimeline);
        } catch (...) {
        }
        r.s = nullptr;
    }

    void RefreshMedia(SessionRec& r) {
        // Don't let an unresponsive app block the core thread.
        auto op = r.s.TryGetMediaPropertiesAsync();
        if (op.wait_for(std::chrono::milliseconds(3000)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
            op.Cancel();
            return;
        }
        auto props = op.GetResults();
        if (!props) return;
        std::wstring title = props.Title().c_str();
        std::wstring artist = props.Artist().c_str();
        std::wstring album = props.AlbumTitle().c_str();
        if (artist.empty()) artist = props.AlbumArtist().c_str();
        if (artist.empty()) artist = props.Subtitle().c_str();
        bool changed = title != r.rawTitle || artist != r.rawArtist || album != r.rawAlbum;
        if (changed) {
            r.rawTitle = title;
            r.rawArtist = artist;
            r.rawAlbum = album;
            r.rawArt = nullptr;
            r.artKey.clear();
            r.metaGen++;
        }
        auto thumb = props.Thumbnail();
        if (thumb) RequestThumbArt(r, thumb);
        if (!changed) return;
        if (r.RawMatchesShown()) {
            // Back to what's already shown (e.g. a preview ended): nothing to wait for.
            r.metaGen++;
            return;
        }
        bool nothingShown = r.title.empty() && r.artist.empty();
        bool empty = title.empty() && artist.empty();
        DWORD delay = empty ? kEmptyMetaSettleMs : (r.isSpotify || nothingShown) ? 0 : kMetaSettleMs;
        if (delay == 0) {
            r.title = title;
            r.artist = artist;
            r.album = album;
            r.art = r.rawArt;
            return;
        }
        uint64_t id = r.id;
        uint32_t gen = r.metaGen;
        g_core.Post([id, gen] { g_ctl().CommitMeta(id, gen); }, delay);
    }

    void RefreshPlayback(SessionRec& r) {
        auto info = r.s.GetPlaybackInfo();
        if (!info) return;
        int64_t pos = r.PosNow();
        auto status = info.PlaybackStatus();
        r.status = (int)status;
        bool playing = status == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        if (playing != r.playing) {
            r.posMs = pos;
            r.posTick = Tick();
        }
        if (playing && !r.playingSince) {
            r.playingSince = Tick();
            // Re-evaluate once it counts as established playback.
            g_core.Post([] { g_ctl().Publish(); }, kNewPlaybackMs + 50);
        } else if (!playing) {
            r.playingSince = 0;
        }
        r.playing = playing;
        auto c = info.Controls();
        r.canPlayPause = c.IsPlayPauseToggleEnabled() || c.IsPlayEnabled() || c.IsPauseEnabled();
        r.canNext = c.IsNextEnabled();
        r.canPrev = c.IsPreviousEnabled();
        r.canSeek = c.IsPlaybackPositionEnabled();
        r.canShuffle = c.IsShuffleEnabled();
        r.canRepeat = c.IsRepeatEnabled();
        if (auto sh = info.IsShuffleActive()) r.shuffle = sh.Value();
        if (auto rep = info.AutoRepeatMode()) {
            auto m = rep.Value();
            r.repeat = m == winrt::Windows::Media::MediaPlaybackAutoRepeatMode::Track ? RepeatMode::Track
                       : m == winrt::Windows::Media::MediaPlaybackAutoRepeatMode::List ? RepeatMode::Context
                                                                                        : RepeatMode::Off;
        }
        if (auto rate = info.PlaybackRate()) r.rate = rate.Value() > 0 ? rate.Value() : 1.0;
        if (r.playing) r.lastActive = Tick();
    }

    void RefreshTimeline(SessionRec& r) {
        auto tl = r.s.GetTimelineProperties();
        if (!tl) return;
        int64_t start = tl.StartTime().count() / 10000;
        int64_t end = tl.EndTime().count() / 10000;
        int64_t pos = tl.Position().count() / 10000;
        r.startMs = start;
        r.durMs = std::max<int64_t>(0, end - start);
        int64_t ageMs = 0;
        auto last = tl.LastUpdatedTime();
        if (last.time_since_epoch().count() > 0) {
            ageMs = (winrt::clock::now() - last).count() / 10000;
            if (ageMs < 0 || ageMs > 24LL * 3600 * 1000) ageMs = 0;
        }
        r.posMs = std::max<int64_t>(0, pos - start);
        r.posTick = Tick() - (ULONGLONG)ageMs;
    }

    void RequestUrlArt(const std::wstring& url) {
        if (artCache_.count(url) || artPending_.count(url)) return;
        artPending_.insert(url);
        UINT smallPx, largePx;
        ArtSizes(smallPx, largePx);
        g_img.Post([url, smallPx, largePx] {
            auto r = Http::Request(L"GET", url);
            std::shared_ptr<Art> art;
            if (r.Ok()) art = MakeArt(url, r.body.data(), r.body.size(), smallPx, largePx);
            g_core.Post([url, art] { g_ctl().OnArt(url, art); });
        });
    }

    void RequestThumbArt(SessionRec& r, wss::IRandomAccessStreamReference ref) {
        r.artGen++;
        std::wstring key = L"t" + std::to_wstring(r.id) + L"#" + std::to_wstring(r.artGen);
        r.artKey = key;
        artPending_.insert(key);
        UINT smallPx, largePx;
        ArtSizes(smallPx, largePx);
        g_img.Post([key, ref, smallPx, largePx] {
            std::shared_ptr<Art> art;
            try {
                auto stream = ref.OpenReadAsync().get();
                uint64_t size = stream.Size();
                if (size > 0 && size < 20 * 1024 * 1024) {
                    wss::Buffer buffer((uint32_t)size);
                    auto result =
                        stream.ReadAsync(buffer, (uint32_t)size, wss::InputStreamOptions::None).get();
                    art = MakeArt(key, result.data(), result.Length(), smallPx, largePx);
                }
            } catch (...) {
            }
            g_core.Post([key, art] { g_ctl().OnArt(key, art); });
        });
    }

    std::shared_ptr<AppInfo> App(const std::wstring& aumid) {
        auto it = apps_.find(aumid);
        if (it != apps_.end()) return it->second;
        if (!appsPending_.count(aumid)) {
            appsPending_.insert(aumid);
            g_img.Post([aumid] {
                std::shared_ptr<AppInfo> info;
                try {
                    info = ResolveAppInfo(aumid);
                } catch (...) {
                }
                g_core.Post([aumid, info] { g_ctl().OnAppInfo(aumid, info); });
            });
        }
        return nullptr;
    }

    void FillFromSpotify(Snapshot& s, SessionRec* localSession) {
        const SpotifyState& p = spot_;
        s.visible = true;
        s.src = Src::Spotify;
        s.isSpotify = true;
        s.appName = L"Spotify";
        s.title = p.title;
        s.artist = p.artist;
        s.album = p.album;
        s.trackUri = p.uri;
        s.trackKey = L"sp|" + p.uri + L"|" + p.title + L"|" + p.artist;
        s.artUrl = p.imageUrl;
        if (!p.imageUrl.empty()) {
            auto it = artCache_.find(p.imageUrl);
            if (it != artCache_.end()) s.art = it->second;
        } else if (localSession && localSession->title == p.title) {
            s.art = localSession->art;
        }
        s.playing = p.playing;
        s.posMs = p.progressMs;
        s.posTick = p.tick;
        s.durMs = p.durationMs;
        bool restricted = p.device.restricted;
        bool ad = p.type == "ad";
        s.canPlayPause = !restricted && !(p.playing ? p.disPause : p.disResume);
        s.canNext = !restricted && !p.disNext;
        s.canPrev = !restricted && !p.disPrev;
        s.canSeek = !restricted && !p.disSeek && !ad && p.durationMs > 0;
        s.canShuffle = !restricted && !p.disShuffle && !ad;
        s.canRepeat = !restricted && !p.disRepeat && !ad;
        s.shuffle = p.shuffle;
        s.repeat = p.repeat;
        s.canLike = !ad && !p.uri.empty() && p.uri.rfind(L"spotify:local:", 0) != 0;
        s.liked = p.liked;
        s.canVolume = !restricted && p.device.supportsVolume;
        s.volume = p.device.volume;
        s.canDevices = true;
        s.deviceId = p.device.id;
        s.deviceName = p.device.name;
        s.deviceType = p.device.type;
        s.devices = devices_;
        s.devicesLoading = devicesLoading_;
    }

    void FillFromSession(Snapshot& s, SessionRec& r) {
        s.visible = true;
        s.src = Src::Session;
        s.aumid = r.aumid;
        s.isSpotify = r.isSpotify;
        s.app = App(r.aumid);
        s.appName = s.app ? s.app->name : (r.isSpotify ? L"Spotify" : L"");
        s.title = r.title;
        s.artist = r.artist;
        s.album = r.album;
        s.trackKey = L"s|" + r.aumid + L"|" + r.title + L"|" + r.artist;
        s.art = r.art;
        s.playing = r.playing;
        s.posMs = r.posMs;
        s.posTick = r.posTick;
        s.rate = r.rate;
        s.durMs = r.durMs;
        s.canPlayPause = r.canPlayPause;
        s.canNext = r.canNext;
        s.canPrev = r.canPrev;
        s.canSeek = r.canSeek && r.durMs > 0;
        s.canShuffle = r.canShuffle;
        s.canRepeat = r.canRepeat;
        s.shuffle = r.shuffle;
        s.repeat = r.repeat;
    }

    void StartAuth() {
        auto set = GetSettings();
        Trace(L"StartAuth: clientIdSet=%d running=%d", !set->clientId.empty(), (int)g_authRunning);
        if (set->clientId.empty()) return;
        if (g_authRunning) {
            g_authReopen = true;  // already waiting: just show the page again
            return;
        }
        if (g_authThread.joinable()) g_authThread.join();
        g_authReopen = false;
        g_authRunning = true;
        g_authCancel = false;
        authPending_ = true;
        authError_.clear();
        g_auth.Load(set->clientId);
        g_authThread = std::thread(RunAuthFlow, set->clientId, set->redirectPort);
        Publish();
    }

    void SpotifyCmd(Act a, int64_t v, const std::wstring& s) {
        std::wstring uri = spot_.uri;
        g_cmd.Post([a, v, s, uri] {
            HttpResult r;
            switch (a) {
                case Act::PlayPause:
                    r = SpotifyCommand(L"PUT", v ? L"/me/player/play" : L"/me/player/pause");
                    break;
                case Act::Next:
                    r = SpotifyCommand(L"POST", L"/me/player/next");
                    break;
                case Act::Prev:
                    r = SpotifyCommand(L"POST", L"/me/player/previous");
                    break;
                case Act::Seek:
                    r = SpotifyCommand(L"PUT", L"/me/player/seek?position_ms=" + std::to_wstring(std::max<int64_t>(0, v)));
                    break;
                case Act::Shuffle:
                    r = SpotifyCommand(L"PUT", std::wstring(L"/me/player/shuffle?state=") + (v ? L"true" : L"false"));
                    break;
                case Act::Repeat:
                    r = SpotifyCommand(L"PUT", std::wstring(L"/me/player/repeat?state=") +
                                               (v == 2 ? L"track" : v == 1 ? L"context" : L"off"));
                    break;
                case Act::Volume:
                    r = SpotifyCommand(L"PUT", L"/me/player/volume?volume_percent=" +
                                               std::to_wstring(std::clamp<int64_t>(v, 0, 100)));
                    break;
                case Act::Like: {
                    if (uri.empty()) return;
                    std::wstring q = L"/me/library?uris=" + ToWide(UrlEncode(ToUtf8(uri)));
                    r = SpotifyCommand(v ? L"PUT" : L"DELETE", q);
                    if (r.Ok()) {
                        g_spot.Post([uri, v] {
                            g_pollMemo.likedUri = uri;
                            g_pollMemo.liked = v ? 1 : 0;
                            g_pollMemo.likedCheckTick = Tick();
                        });
                    }
                    break;
                }
                case Act::Transfer: {
                    std::string body = "{\"device_ids\":[\"" + JsonEscape(ToUtf8(s)) +
                                       "\"],\"play\":" + (v ? "true" : "false") + "}";
                    r = SpotifyCommand(L"PUT", L"/me/player", body);
                    break;
                }
                default:
                    return;
            }
            if (!r.Ok()) ReportApiError(r, a == Act::Like ? ApiFamily::Library : ApiFamily::Player);
            SchedulePoll(a == Act::Transfer ? 700 : 200);
            if (a == Act::Transfer) g_spot.Post([] { FetchDevices(); }, 1500);
        });
    }

    void SessionCmd(Act a, int64_t v) {
        SessionRec* r = Find(curSession_);
        if (!r) return;
        auto s = r->s;
        int64_t start = r->startMs;
        g_cmd.Post([s, a, v, start] {
            try {
                switch (a) {
                    case Act::PlayPause: {
                        bool ok = v ? s.TryPlayAsync().get() : s.TryPauseAsync().get();
                        if (!ok) s.TryTogglePlayPauseAsync().get();
                        break;
                    }
                    case Act::Next:
                        s.TrySkipNextAsync().get();
                        break;
                    case Act::Prev:
                        s.TrySkipPreviousAsync().get();
                        break;
                    case Act::Seek:
                        s.TryChangePlaybackPositionAsync((start + v) * 10000).get();
                        break;
                    case Act::Shuffle:
                        s.TryChangeShuffleActiveAsync(v != 0).get();
                        break;
                    case Act::Repeat:
                        s.TryChangeAutoRepeatModeAsync(
                             v == 2   ? winrt::Windows::Media::MediaPlaybackAutoRepeatMode::Track
                             : v == 1 ? winrt::Windows::Media::MediaPlaybackAutoRepeatMode::List
                                      : winrt::Windows::Media::MediaPlaybackAutoRepeatMode::None)
                            .get();
                        break;
                    default:
                        break;
                }
            } catch (...) {
            }
        });
    }

    wmc::GlobalSystemMediaTransportControlsSessionManager mgr_{nullptr};
    winrt::event_token tSessions_{}, tCurrent_{};
    std::vector<std::unique_ptr<SessionRec>> sessions_;
    uint64_t nextId_ = 1;
    SpotifyState spot_;
    ULONGLONG spotOkTick_ = 0;
    bool spotContent_ = false;        // the last accepted state had something playing
    ULONGLONG spotContentTick_ = 0;   // when that state was last confirmed
    std::map<std::wstring, std::shared_ptr<Art>> artCache_;
    std::vector<std::wstring> artOrder_;
    std::set<std::wstring> artPending_;
    std::map<std::wstring, std::shared_ptr<AppInfo>> apps_;
    std::set<std::wstring> appsPending_;
    std::vector<DeviceInfo> devices_;
    bool devicesLoading_ = false;
    bool popupOpen_ = false;
    bool authPending_ = false;
    std::wstring authError_;
    ULONGLONG authErrorTick_ = 0;
    std::wstring toast_;
    uint32_t toastSeq_ = 0;
    Src curSrc_ = Src::None;
    uint64_t curSession_ = 0;
    std::wstring curKey_;
    bool curPlaying_ = false;
    ULONGLONG pausedSince_ = 0;
    std::wstring curAumid_, lastTraceKey_;
    std::map<std::wstring, ShownCarry> removed_;
    std::shared_ptr<Snapshot> lastShown_;
    ULONGLONG lostSince_ = 0;
    uint64_t seq_ = 0;
};

static Controller& g_controller = *new Controller();  // never destroyed, see g_core
Controller& Controller::g_ctl() { return g_controller; }

static void NotifyAuthLost() {
    g_core.Post([] { g_controller.OnAuthLost(); });
}
static void NotifySpotifyState(SpotifyState st) {
    g_core.Post([st = std::move(st)]() mutable { g_controller.OnSpotify(std::move(st)); });
}
static void NotifyDevices(std::vector<DeviceInfo> devices, bool ok) {
    g_core.Post([devices = std::move(devices), ok]() mutable {
        g_controller.OnDevices(std::move(devices), ok);
    });
}
static void NotifyToast(std::wstring msg) {
    g_core.Post([msg = std::move(msg)] { g_controller.ShowToast(msg); });
}
static void NotifyAuthFinished(bool success, std::wstring error) {
    g_core.Post([success, error = std::move(error)] { g_controller.OnAuthFinished(success, error); });
}

// Called from the UI thread.
static void SendAct(Act a, int64_t v = 0, std::wstring s = {}) {
    g_core.Post([a, v, s = std::move(s)] { g_controller.HandleAct(a, v, s); });
}

// Remote feed thread: a command from another mod, handled like a click.
static void RunFeedCommand(LONG command) {
    if (g_shuttingDown) return;
    Trace(L"Remote command %d", (int)command);
    switch (command) {
        case kFeedToggle: {
            auto s = LatestSnapshot();
            if (s && s->visible && !s->connectPrompt) SendAct(Act::PlayPause, s->playing ? 0 : 1);
            break;
        }
        case kFeedNext:
            SendAct(Act::Next);
            break;
        case kFeedPrev:
            SendAct(Act::Prev);
            break;
    }
}

}  // namespace tp

namespace tp {

// ============================================================================
// Rendering helpers (Direct2D + DirectWrite, UI thread only)
// ============================================================================

static D2D1_COLOR_F Rgba(uint32_t rgb, float a = 1.f) {
    return D2D1_COLOR_F{((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f,
                        (rgb & 255) / 255.f, a};
}
static D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}
static D2D1_RECT_F RectF(float l, float t, float r, float b) { return D2D1_RECT_F{l, t, r, b}; }
static D2D1_RECT_F RectXYWH(float x, float y, float w, float h) { return D2D1_RECT_F{x, y, x + w, y + h}; }
static D2D1_RECT_F Inflate(D2D1_RECT_F r, float dx, float dy) {
    return D2D1_RECT_F{r.left - dx, r.top - dy, r.right + dx, r.bottom + dy};
}
static bool Contains(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
static float Width(const D2D1_RECT_F& r) { return r.right - r.left; }
static float Height(const D2D1_RECT_F& r) { return r.bottom - r.top; }
static D2D1_POINT_2F Center(const D2D1_RECT_F& r) {
    return D2D1_POINT_2F{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
}
static float Clamp01(float t) { return std::clamp(t, 0.f, 1.f); }
static float EaseOutCubic(float t) {
    t = Clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}
static float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2;
}

class Gfx {
   public:
    bool Init() {
        D2D1_FACTORY_OPTIONS opts{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                     d2d_.put_void())))
            return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       (IUnknown**)dw_.put())))
            return false;
        textFamily_ = HasFamily(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
        displayFamily_ =
            HasFamily(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
        iconFamily_ = HasFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        D2D1_STROKE_STYLE_PROPERTIES sp{};
        sp.startCap = sp.endCap = sp.dashCap = D2D1_CAP_STYLE_ROUND;
        sp.lineJoin = D2D1_LINE_JOIN_ROUND;
        d2d_->CreateStrokeStyle(&sp, nullptr, 0, round_.put());
        return true;
    }

    ID2D1Factory1* D2D() const { return d2d_.get(); }
    IDWriteFactory* DW() const { return dw_.get(); }
    ID2D1StrokeStyle* RoundStroke() const { return round_.get(); }

    IDWriteTextFormat* Font(bool display, float px, DWRITE_FONT_WEIGHT weight) {
        return Format(display ? displayFamily_ : textFamily_, px, weight);
    }
    IDWriteTextFormat* Icons(float px) { return Format(iconFamily_, px, DWRITE_FONT_WEIGHT_NORMAL); }

    IDWriteInlineObject* Ellipsis(IDWriteTextFormat* f) {
        auto it = ellipsis_.find(f);
        if (it != ellipsis_.end()) return it->second.get();
        com_ptr<IDWriteInlineObject> sign;
        dw_->CreateEllipsisTrimmingSign(f, sign.put());
        ellipsis_[f] = sign;
        return sign.get();
    }

   private:
    bool HasFamily(const wchar_t* name) {
        com_ptr<IDWriteFontCollection> fonts;
        if (FAILED(dw_->GetSystemFontCollection(fonts.put(), FALSE))) return false;
        UINT32 index = 0;
        BOOL exists = FALSE;
        fonts->FindFamilyName(name, &index, &exists);
        return exists;
    }

    IDWriteTextFormat* Format(const std::wstring& family, float px, DWRITE_FONT_WEIGHT weight) {
        wchar_t key[160];
        swprintf(key, 160, L"%s|%.2f|%d", family.c_str(), px, (int)weight);
        auto it = fonts_.find(key);
        if (it != fonts_.end()) return it->second.get();
        com_ptr<IDWriteTextFormat> f;
        if (FAILED(dw_->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, px, L"en-us", f.put())))
            return nullptr;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fonts_[key] = f;
        return f.get();
    }

    com_ptr<ID2D1Factory1> d2d_;
    com_ptr<IDWriteFactory> dw_;
    com_ptr<ID2D1StrokeStyle> round_;
    std::wstring textFamily_, displayFamily_, iconFamily_;
    std::map<std::wstring, com_ptr<IDWriteTextFormat>> fonts_;
    std::map<IDWriteTextFormat*, com_ptr<IDWriteInlineObject>> ellipsis_;
};

static Gfx* g_gfx = nullptr;

static com_ptr<IDWriteTextLayout> MakeLayout(const std::wstring& text, IDWriteTextFormat* fmt,
                                             float maxW, float maxH,
                                             DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                                             bool ellipsis = true) {
    com_ptr<IDWriteTextLayout> layout;
    if (!fmt || FAILED(g_gfx->DW()->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt,
                                                     std::max(maxW, 1.f), std::max(maxH, 1.f),
                                                     layout.put())))
        return nullptr;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(align);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (ellipsis) {
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trim, g_gfx->Ellipsis(fmt));
    }
    return layout;
}

static float TextWidth(const std::wstring& text, IDWriteTextFormat* fmt) {
    if (text.empty()) return 0;
    auto layout = MakeLayout(text, fmt, 10000, 100, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    if (!layout) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return std::ceil(m.widthIncludingTrailingWhitespace);
}

static void DrawTextLine(ID2D1RenderTarget* rt, const std::wstring& text, IDWriteTextFormat* fmt,
                         D2D1_RECT_F rc, ID2D1Brush* brush,
                         DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    if (text.empty() || Width(rc) <= 1) return;
    auto layout = MakeLayout(text, fmt, Width(rc), Height(rc), align, true);
    if (layout)
        rt->DrawTextLayout(D2D1_POINT_2F{rc.left, rc.top}, layout.get(), brush,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void DrawGlyph(ID2D1RenderTarget* rt, wchar_t glyph, D2D1_POINT_2F c, float px,
                      ID2D1Brush* brush) {
    IDWriteTextFormat* f = g_gfx->Icons(px);
    if (!f) return;
    std::wstring s(1, glyph);
    float box = px * 2;
    auto layout = MakeLayout(s, f, box, box, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    if (layout) rt->DrawTextLayout(D2D1_POINT_2F{c.x - box / 2, c.y - box / 2}, layout.get(), brush);
}

// ---------------------------------------------------------------------------
// Canvases: where the widget and popup draw. Each owns its Direct2D resources.
// ---------------------------------------------------------------------------

class Canvas {
   public:
    virtual ~Canvas() = default;
    virtual ID2D1RenderTarget* Target() const = 0;

    ID2D1SolidColorBrush* Brush(D2D1_COLOR_F c) {
        if (!brush_) Target()->CreateSolidColorBrush(c, brush_.put());
        else brush_->SetColor(c);
        return brush_.get();
    }

    ID2D1Bitmap* Bitmap(IWICBitmapSource* src) {
        if (!src || !Target()) return nullptr;
        ULONGLONG now = Tick();
        for (auto& e : cache_) {
            if (e.src.get() == src) {
                e.used = now;
                return e.bmp.get();
            }
        }
        com_ptr<ID2D1Bitmap> bmp;
        if (FAILED(Target()->CreateBitmapFromWicBitmap(src, nullptr, bmp.put()))) return nullptr;
        if (cache_.size() >= 12) {
            auto oldest = std::min_element(cache_.begin(), cache_.end(),
                                           [](const Cached& a, const Cached& b) { return a.used < b.used; });
            cache_.erase(oldest);
        }
        com_ptr<IWICBitmapSource> keep;
        keep.copy_from(src);
        cache_.push_back(Cached{keep, bmp, now});
        return bmp.get();
    }

    int w = 0, h = 0;
    uint32_t gen = 0;  // incremented whenever the render target is (re)created

   protected:
    void DropResources() {
        cache_.clear();
        brush_ = nullptr;
    }

   private:
    struct Cached {
        com_ptr<IWICBitmapSource> src;
        com_ptr<ID2D1Bitmap> bmp;
        ULONGLONG used;
    };
    com_ptr<ID2D1SolidColorBrush> brush_;
    std::vector<Cached> cache_;
};

// Off-screen DIB for per-pixel-alpha layered windows (UpdateLayeredWindow).
class Surface : public Canvas {
   public:
    ~Surface() override { Release(); }

    void Release() {
        DropResources();
        rt_ = nullptr;
        if (dc_) {
            if (old_) SelectObject(dc_, old_);
            DeleteDC(dc_);
            dc_ = nullptr;
        }
        if (bmp_) {
            DeleteObject(bmp_);
            bmp_ = nullptr;
        }
        old_ = nullptr;
        bits_ = nullptr;
        w = h = 0;
    }

    bool Resize(int nw, int nh) {
        nw = std::max(nw, 1);
        nh = std::max(nh, 1);
        if (dc_ && nw == w && nh == h) return true;
        if (!dc_) {
            dc_ = CreateCompatibleDC(nullptr);
            if (!dc_) return false;
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = nw;
        bi.bmiHeader.biHeight = -nh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) return false;
        HGDIOBJ prev = SelectObject(dc_, bmp);
        if (!old_) old_ = prev;
        if (bmp_) DeleteObject(bmp_);
        bmp_ = bmp;
        bits_ = bits;
        w = nw;
        h = nh;
        return true;
    }

    ID2D1RenderTarget* Begin() {
        if (!dc_) return nullptr;
        if (!rt_) {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f,
                96.f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
            if (FAILED(g_gfx->D2D()->CreateDCRenderTarget(&props, rt_.put()))) return nullptr;
            gen++;
        }
        RECT rc{0, 0, w, h};
        if (FAILED(rt_->BindDC(dc_, &rc))) return nullptr;
        rt_->BeginDraw();
        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        rt_->Clear(D2D1_COLOR_F{0, 0, 0, 0});
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return rt_.get();
    }

    bool End() {
        if (!rt_) return false;
        HRESULT hr = rt_->EndDraw();
        GdiFlush();
        if (hr == D2DERR_RECREATE_TARGET) {
            DropResources();
            rt_ = nullptr;
            return false;
        }
        return SUCCEEDED(hr);
    }

    void Present(HWND hwnd, const POINT* screenPos, BYTE alpha) {
        if (!dc_) return;
        BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        SIZE size{w, h};
        POINT src{0, 0};
        UpdateLayeredWindow(hwnd, nullptr, const_cast<POINT*>(screenPos), &size, dc_, &src, 0, &bf,
                            ULW_ALPHA);
    }

    ID2D1RenderTarget* Target() const override { return rt_.get(); }
    const void* Bits() const { return bits_; }

   private:
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    com_ptr<ID2D1DCRenderTarget> rt_;
};

// GPU canvas: a DirectComposition swap chain with per-pixel alpha, so the
// window can sit on top of a DWM blur/acrylic backdrop.
class CompositionCanvas : public Canvas {
   public:
    ~CompositionCanvas() override { Release(); }

    bool Init(HWND hwnd) {
        Release();
        hwnd_ = hwnd;
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr);
        if (FAILED(hr)) return Fail(L"D3D11CreateDevice", hr);
        com_ptr<IDXGIDevice> dxgi;
        if (FAILED(hr = d3d_->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void()))) return Fail(L"IDXGIDevice", hr);
        if (FAILED(hr = g_gfx->D2D()->CreateDevice(dxgi.get(), d2dDevice_.put()))) return Fail(L"D2D CreateDevice", hr);
        if (FAILED(hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.put())))
            return Fail(L"CreateDeviceContext", hr);
        if (FAILED(hr = DCompositionCreateDevice(dxgi.get(), __uuidof(IDCompositionDevice), dcomp_.put_void())))
            return Fail(L"DCompositionCreateDevice", hr);
        if (FAILED(hr = dcomp_->CreateTargetForHwnd(hwnd, TRUE, target_.put()))) return Fail(L"CreateTargetForHwnd", hr);
        if (FAILED(hr = dcomp_->CreateVisual(visual_.put()))) return Fail(L"CreateVisual", hr);
        target_->SetRoot(visual_.get());
        gen++;
        return true;
    }

    bool Ready() const { return dc_ != nullptr; }

    void Release() {
        DropResources();
        if (dc_) dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        swap_ = nullptr;
        visual_ = nullptr;
        target_ = nullptr;
        dcomp_ = nullptr;
        dc_ = nullptr;
        d2dDevice_ = nullptr;
        d3d_ = nullptr;
        w = h = 0;
    }

    bool Resize(int nw, int nh) {
        if (!dc_) return false;
        nw = std::max(nw, 1);
        nh = std::max(nh, 1);
        if (swap_ && nw == w && nh == h) return true;
        dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        HRESULT hr;
        if (!swap_) {
            com_ptr<IDXGIDevice> dxgi;
            d3d_->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void());
            com_ptr<IDXGIAdapter> adapter;
            dxgi->GetAdapter(adapter.put());
            com_ptr<IDXGIFactory2> factory;
            adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void());
            if (!factory) return Fail(L"IDXGIFactory2", E_NOINTERFACE);
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = nw;
            desc.Height = nh;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            if (FAILED(hr = factory->CreateSwapChainForComposition(d3d_.get(), &desc, nullptr, swap_.put())))
                return Fail(L"CreateSwapChainForComposition", hr);
            visual_->SetContent(swap_.get());
        } else if (FAILED(hr = swap_->ResizeBuffers(2, nw, nh, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) {
            return Fail(L"ResizeBuffers", hr);
        }
        w = nw;
        h = nh;
        return true;
    }

    ID2D1RenderTarget* Begin() {
        if (!dc_ || !swap_) return nullptr;
        if (!targetBitmap_) {
            com_ptr<IDXGISurface> surface;
            if (FAILED(swap_->GetBuffer(0, __uuidof(IDXGISurface), surface.put_void()))) return nullptr;
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
            if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, targetBitmap_.put()))) return nullptr;
            dc_->SetTarget(targetBitmap_.get());
        }
        dc_->BeginDraw();
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        dc_->Clear(D2D1_COLOR_F{0, 0, 0, 0});
        dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return dc_.get();
    }

    // Ends drawing and shows the frame. Returns false if the device was lost.
    bool End() {
        if (!dc_) return false;
        HRESULT hr = dc_->EndDraw();
        // With flip-model swap chains the target buffer changes after Present.
        dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        if (SUCCEEDED(hr)) hr = swap_->Present(0, 0);
        if (SUCCEEDED(hr)) hr = dcomp_->Commit();
        if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Trace(L"Composition device lost (0x%08X), recreating", (unsigned)hr);
            HWND hwnd = hwnd_;
            Init(hwnd);
            return false;
        }
        return SUCCEEDED(hr);
    }

    ID2D1RenderTarget* Target() const override { return dc_.get(); }
    ID2D1DeviceContext* Context() const { return dc_.get(); }

   private:
    bool Fail(const wchar_t* what, HRESULT hr) {
        Trace(L"Composition setup failed at %s: 0x%08X", what, (unsigned)hr);
        Release();
        return false;
    }

    HWND hwnd_ = nullptr;
    com_ptr<ID3D11Device> d3d_;
    com_ptr<ID2D1Device> d2dDevice_;
    com_ptr<ID2D1DeviceContext> dc_;
    com_ptr<IDCompositionDevice> dcomp_;
    com_ptr<IDCompositionTarget> target_;
    com_ptr<IDCompositionVisual> visual_;
    com_ptr<IDXGISwapChain1> swap_;
    com_ptr<ID2D1Bitmap1> targetBitmap_;
};


// ---------------------------------------------------------------------------
// Windows.UI.Composition canvas: the window's live backdrop (blurred by the
// system), a tint and our Direct2D content, inside a rounded clip that we
// control. The whole tree can be faded, moved and scaled by the compositor.
// ---------------------------------------------------------------------------

namespace wuc = winrt::Windows::UI::Composition;

static const IID kIID_ICompositorDesktopInterop = {
    0x29E691FA, 0x4567, 0x4DCA, {0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07}};
static const IID kIID_ICompositorInterop = {
    0x25297D5C, 0x3AD4, 0x4C9C, {0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30}};

struct ICompositorDesktopInteropRaw : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND hwnd, BOOL topmost, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnsureOnThread(DWORD threadId) = 0;
};
struct ICompositorInteropRaw : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateCompositionSurfaceForHandle(HANDLE swapChain, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCompositionSurfaceForSwapChain(IUnknown* swapChain, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGraphicsDevice(IUnknown* device, void** result) = 0;
};

class WinCompCanvas : public Canvas {
   public:
    ~WinCompCanvas() override { Release(); }

    bool Init(HWND hwnd) {
        Release();
        hwnd_ = hwnd;
        HRESULT hr = S_OK;
        try {
            // A compositor needs a dispatcher queue on this thread (the
            // taskbar thread already has one for its XAML).
            if (!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
                DispatcherQueueOptions o{sizeof(o), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
                if (FAILED(hr = CreateDispatcherQueueController(o, (PDISPATCHERQUEUECONTROLLER*)dq_.put_void())))
                    return Fail(L"CreateDispatcherQueueController", hr);
            }
            if (!CreateDevices()) return false;
            comp_ = wuc::Compositor();
            com_ptr<ICompositorDesktopInteropRaw> desk;
            if (FAILED(hr = winrt::get_unknown(comp_)->QueryInterface(kIID_ICompositorDesktopInterop, desk.put_void())))
                return Fail(L"ICompositorDesktopInterop", hr);
            if (FAILED(hr = desk->CreateDesktopWindowTarget(hwnd, TRUE, winrt::put_abi(target_))))
                return Fail(L"CreateDesktopWindowTarget", hr);
            root_ = comp_.CreateContainerVisual();
            target_.Root(root_);
            card_ = comp_.CreateContainerVisual();
            clipGeom_ = comp_.CreateRoundedRectangleGeometry();
            card_.Clip(comp_.CreateGeometricClip(clipGeom_));
            backdrop_ = comp_.CreateSpriteVisual();
            backdrop_.Brush(comp_.CreateHostBackdropBrush());
            backdrop_.RelativeSizeAdjustment({1.f, 1.f});
            tintBrush_ = comp_.CreateColorBrush();
            tint_ = comp_.CreateSpriteVisual();
            tint_.Brush(tintBrush_);
            tint_.RelativeSizeAdjustment({1.f, 1.f});
            card_.Children().InsertAtTop(backdrop_);
            card_.Children().InsertAtTop(tint_);
            content_ = comp_.CreateSpriteVisual();
            root_.Children().InsertAtTop(card_);
            root_.Children().InsertAtTop(content_);
        } catch (winrt::hresult_error const& e) {
            return Fail(L"Windows.UI.Composition", e.code());
        } catch (...) {
            return Fail(L"Windows.UI.Composition", E_FAIL);
        }
        BOOL use = TRUE;
        DwmSetWindowAttribute(hwnd, 17 /*DWMWA_USE_HOSTBACKDROPBRUSH*/, &use, sizeof(use));
        DWORD corner = 1;  // DWMWCP_DONOTROUND: our own rounded clip does it
        DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
        BOOL noTransitions = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));
        gen++;
        return true;
    }

    bool Ready() const { return dc_ != nullptr && comp_ != nullptr; }

    void Release() {
        DropResources();
        if (dc_) dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        try {
            if (target_) target_.Root(nullptr);
        } catch (...) {
        }
        content_ = nullptr;
        tint_ = nullptr;
        tintBrush_ = nullptr;
        backdrop_ = nullptr;
        clipGeom_ = nullptr;
        card_ = nullptr;
        root_ = nullptr;
        target_ = nullptr;
        comp_ = nullptr;
        swap_ = nullptr;
        dc_ = nullptr;
        d2dDevice_ = nullptr;
        d3d_ = nullptr;
        dq_ = nullptr;
        w = h = 0;
    }

    bool Resize(int nw, int nh) {
        if (!dc_) return false;
        nw = std::max(nw, 1);
        nh = std::max(nh, 1);
        if (swap_ && nw == w && nh == h) return true;
        dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        HRESULT hr;
        try {
            if (!swap_) {
                com_ptr<IDXGIDevice> dxgi;
                d3d_->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void());
                com_ptr<IDXGIAdapter> adapter;
                dxgi->GetAdapter(adapter.put());
                com_ptr<IDXGIFactory2> factory;
                adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void());
                if (!factory) return Fail(L"IDXGIFactory2", E_NOINTERFACE);
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = nw;
                desc.Height = nh;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.Scaling = DXGI_SCALING_STRETCH;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
                if (FAILED(hr = factory->CreateSwapChainForComposition(d3d_.get(), &desc, nullptr, swap_.put())))
                    return Fail(L"CreateSwapChainForComposition", hr);
                com_ptr<ICompositorInteropRaw> interop;
                if (FAILED(hr = winrt::get_unknown(comp_)->QueryInterface(kIID_ICompositorInterop, interop.put_void())))
                    return Fail(L"ICompositorInterop", hr);
                wuc::ICompositionSurface surface{nullptr};
                if (FAILED(hr = interop->CreateCompositionSurfaceForSwapChain(swap_.get(), winrt::put_abi(surface))))
                    return Fail(L"CreateCompositionSurfaceForSwapChain", hr);
                auto brush = comp_.CreateSurfaceBrush(surface);
                brush.Stretch(wuc::CompositionStretch::None);
                brush.HorizontalAlignmentRatio(0.f);
                brush.VerticalAlignmentRatio(0.f);
                content_.Brush(brush);
            } else if (FAILED(hr = swap_->ResizeBuffers(2, nw, nh, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) {
                return Fail(L"ResizeBuffers", hr);
            }
            content_.Size({(float)nw, (float)nh});
            root_.Size({(float)nw, (float)nh});
        } catch (winrt::hresult_error const& e) {
            return Fail(L"composition resize", e.code());
        }
        w = nw;
        h = nh;
        return true;
    }

    // Places the rounded backdrop + tint under the card area.
    void SetCard(D2D1_RECT_F card, float radius, D2D1_COLOR_F tint, bool backdrop) {
        if (!card_) return;
        try {
            card_.Offset({card.left, card.top, 0.f});
            card_.Size({card.right - card.left, card.bottom - card.top});
            clipGeom_.Size({card.right - card.left, card.bottom - card.top});
            clipGeom_.CornerRadius({radius, radius});
            auto c = winrt::Windows::UI::ColorHelper::FromArgb(
                (uint8_t)std::lround(Clamp01(tint.a) * 255), (uint8_t)std::lround(Clamp01(tint.r) * 255),
                (uint8_t)std::lround(Clamp01(tint.g) * 255), (uint8_t)std::lround(Clamp01(tint.b) * 255));
            tintBrush_.Color(c);
            backdrop_.IsVisible(backdrop);
        } catch (...) {
        }
    }

    // Fades, moves and scales everything (backdrop included).
    void SetRoot(float opacity, float dy, float scale, D2D1_POINT_2F center) {
        if (!root_) return;
        try {
            root_.Opacity(Clamp01(opacity));
            root_.Offset({0.f, dy, 0.f});
            root_.CenterPoint({center.x, center.y, 0.f});
            root_.Scale({scale, scale, 1.f});
        } catch (...) {
        }
    }

    ID2D1RenderTarget* Begin() {
        if (!dc_ || !swap_) return nullptr;
        if (!targetBitmap_) {
            com_ptr<IDXGISurface> surface;
            if (FAILED(swap_->GetBuffer(0, __uuidof(IDXGISurface), surface.put_void()))) return nullptr;
            D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f, 96.f);
            if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, targetBitmap_.put()))) return nullptr;
            dc_->SetTarget(targetBitmap_.get());
        }
        dc_->BeginDraw();
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        dc_->Clear(D2D1_COLOR_F{0, 0, 0, 0});
        dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        return dc_.get();
    }

    bool End() {
        if (!dc_) return false;
        HRESULT hr = dc_->EndDraw();
        dc_->SetTarget(nullptr);
        targetBitmap_ = nullptr;
        if (SUCCEEDED(hr)) hr = swap_->Present(0, 0);
        if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Trace(L"Composition device lost (0x%08X), recreating", (unsigned)hr);
            HWND hwnd = hwnd_;
            Init(hwnd);
            return false;
        }
        return SUCCEEDED(hr);
    }

    ID2D1RenderTarget* Target() const override { return dc_.get(); }

   private:
    bool CreateDevices() {
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                      D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, d3d_.put(), nullptr, nullptr);
        if (FAILED(hr)) return Fail(L"D3D11CreateDevice", hr);
        com_ptr<IDXGIDevice> dxgi;
        if (FAILED(hr = d3d_->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void()))) return Fail(L"IDXGIDevice", hr);
        if (FAILED(hr = g_gfx->D2D()->CreateDevice(dxgi.get(), d2dDevice_.put()))) return Fail(L"D2D CreateDevice", hr);
        if (FAILED(hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.put())))
            return Fail(L"CreateDeviceContext", hr);
        return true;
    }

    bool Fail(const wchar_t* what, winrt::hresult hr) {
        Trace(L"Composition (WinRT) setup failed at %s: 0x%08X", what, (unsigned)hr.value);
        Release();
        return false;
    }

    HWND hwnd_ = nullptr;
    com_ptr<IUnknown> dq_;
    com_ptr<ID3D11Device> d3d_;
    com_ptr<ID2D1Device> d2dDevice_;
    com_ptr<ID2D1DeviceContext> dc_;
    com_ptr<IDXGISwapChain1> swap_;
    com_ptr<ID2D1Bitmap1> targetBitmap_;
    wuc::Compositor comp_{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{nullptr};
    wuc::ContainerVisual root_{nullptr}, card_{nullptr};
    wuc::SpriteVisual backdrop_{nullptr}, tint_{nullptr}, content_{nullptr};
    wuc::CompositionRoundedRectangleGeometry clipGeom_{nullptr};
    wuc::CompositionColorBrush tintBrush_{nullptr};
};
// Draws a bitmap into a rounded rectangle (1:1 when sizes match).
static void DrawRoundedBitmap(ID2D1RenderTarget* rt, ID2D1Bitmap* bmp, D2D1_RECT_F rc, float radius,
                              float opacity) {
    if (!bmp) return;
    D2D1_SIZE_F sz = bmp->GetSize();
    if (sz.width <= 0 || sz.height <= 0) return;
    com_ptr<ID2D1BitmapBrush> brush;
    D2D1_BITMAP_BRUSH_PROPERTIES bp = D2D1::BitmapBrushProperties(
        D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    D2D1_BRUSH_PROPERTIES brp = D2D1::BrushProperties(
        opacity, D2D1::Matrix3x2F::Scale(Width(rc) / sz.width, Height(rc) / sz.height) *
                     D2D1::Matrix3x2F::Translation(rc.left, rc.top));
    if (FAILED(rt->CreateBitmapBrush(bmp, &bp, &brp, brush.put()))) return;
    rt->FillRoundedRectangle(D2D1::RoundedRect(rc, radius, radius), brush.get());
}

// ---------------------------------------------------------------------------
// Vector icons
// ---------------------------------------------------------------------------

enum class Glyph : wchar_t {
    Shuffle = 0xE8B1,
    RepeatAll = 0xE8EE,
    RepeatOne = 0xE8ED,
    Heart = 0xEB51,
    HeartFill = 0xEB52,
    Volume0 = 0xE992,
    Volume1 = 0xE993,
    Volume2 = 0xE994,
    Volume3 = 0xE995,
    Mute = 0xE74F,
    Speaker = 0xE7F5,
    Laptop = 0xE7F8,
    Phone = 0xE8EA,
    Tablet = 0xE70A,
    Tv = 0xE7F4,
    Game = 0xE7FC,
    Car = 0xE804,
    Music = 0xE8D6,
    Devices = 0xE772,
    Close = 0xE711,
    Check = 0xE73E,
};

static void DrawIcon(ID2D1RenderTarget* rt, Glyph g, D2D1_POINT_2F c, float px, ID2D1Brush* b) {
    DrawGlyph(rt, (wchar_t)g, c, px, b);
}

// A triangle with rounded corners, built as a single filled path (arcs at the corners).
static void FillRoundedTriangle(ID2D1RenderTarget* rt, D2D1_POINT_2F a, D2D1_POINT_2F b,
                                D2D1_POINT_2F c, float round, ID2D1Brush* brush) {
    D2D1_POINT_2F v[3] = {a, b, c};
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(cross) < 0.01f) return;
    D2D1_SWEEP_DIRECTION sweep =
        cross > 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
    D2D1_POINT_2F start[3], end[3];
    for (int i = 0; i < 3; i++) {
        D2D1_POINT_2F p = v[(i + 2) % 3], q = v[i], n = v[(i + 1) % 3];
        float l1 = std::hypot(p.x - q.x, p.y - q.y), l2 = std::hypot(n.x - q.x, n.y - q.y);
        if (l1 <= 0 || l2 <= 0) return;
        D2D1_POINT_2F u1{(p.x - q.x) / l1, (p.y - q.y) / l1}, u2{(n.x - q.x) / l2, (n.y - q.y) / l2};
        float angle = std::acos(std::clamp(u1.x * u2.x + u1.y * u2.y, -1.f, 1.f));
        float d = std::min(round / std::tan(angle / 2), std::min(l1, l2) * 0.45f);
        start[i] = D2D1_POINT_2F{q.x + u1.x * d, q.y + u1.y * d};
        end[i] = D2D1_POINT_2F{q.x + u2.x * d, q.y + u2.y * d};
    }
    com_ptr<ID2D1PathGeometry> geo;
    if (FAILED(g_gfx->D2D()->CreatePathGeometry(geo.put()))) return;
    com_ptr<ID2D1GeometrySink> sink;
    geo->Open(sink.put());
    sink->BeginFigure(end[0], D2D1_FIGURE_BEGIN_FILLED);
    for (int k = 1; k <= 3; k++) {
        int i = k % 3;
        sink->AddLine(start[i]);
        sink->AddArc(D2D1::ArcSegment(end[i], D2D1::SizeF(round, round), 0, sweep, D2D1_ARC_SIZE_SMALL));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(geo.get(), brush);
}

enum class Transport { Play, Pause, Prev, Next };

static void DrawTransport(ID2D1RenderTarget* rt, Transport t, D2D1_POINT_2F c, float s,
                          ID2D1Brush* brush) {
    float r = 0.075f * s;
    switch (t) {
        case Transport::Play:
            FillRoundedTriangle(rt, {c.x - 0.30f * s, c.y - 0.40f * s}, {c.x + 0.42f * s, c.y},
                                {c.x - 0.30f * s, c.y + 0.40f * s}, r, brush);
            break;
        case Transport::Pause: {
            float w = 0.25f * s, h = 0.78f * s, gap = 0.16f * s;
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(RectXYWH(c.x - gap / 2 - w, c.y - h / 2, w, h), r, r), brush);
            rt->FillRoundedRectangle(D2D1::RoundedRect(RectXYWH(c.x + gap / 2, c.y - h / 2, w, h), r, r),
                                     brush);
            break;
        }
        case Transport::Prev:
        case Transport::Next: {
            float dir = t == Transport::Next ? 1.f : -1.f;
            float h = 0.72f * s;
            float barW = 0.14f * s;
            float barX = c.x + dir * 0.40f * s - (dir > 0 ? barW : 0);
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(RectXYWH(barX, c.y - h / 2, barW, h), barW / 2, barW / 2), brush);
            FillRoundedTriangle(rt, {c.x - dir * 0.40f * s, c.y - h / 2}, {c.x + dir * 0.20f * s, c.y},
                                {c.x - dir * 0.40f * s, c.y + h / 2}, r, brush);
            break;
        }
    }
}

// Builds a Direct2D geometry from SVG path data (M, L, H, V, C, S, Z; absolute
// and relative), mapping the SVG coordinates with `scale` and `offset`.
static com_ptr<ID2D1PathGeometry> SvgPathGeometry(const char* d, float scale, D2D1_POINT_2F offset) {
    com_ptr<ID2D1PathGeometry> geo;
    if (FAILED(g_gfx->D2D()->CreatePathGeometry(geo.put()))) return nullptr;
    com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.put()))) return nullptr;
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);  // inner shapes become holes

    auto T = [&](D2D1_POINT_2F p) { return D2D1_POINT_2F{offset.x + p.x * scale, offset.y + p.y * scale}; };
    const char* p = d;
    auto skip = [&] {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t') p++;
    };
    auto number = [&](float& out) {
        skip();
        const char* s = p;
        if (*p == '-' || *p == '+') p++;
        bool dot = false;
        while (isdigit((unsigned char)*p) || (*p == '.' && !dot)) {
            if (*p == '.') dot = true;
            p++;
        }
        if (*p == 'e' || *p == 'E') {
            p++;
            if (*p == '-' || *p == '+') p++;
            while (isdigit((unsigned char)*p)) p++;
        }
        if (p == s) return false;
        out = strtof(std::string(s, p).c_str(), nullptr);
        return true;
    };
    auto hasNumber = [&] {
        skip();
        return *p == '-' || *p == '+' || *p == '.' || isdigit((unsigned char)*p);
    };

    D2D1_POINT_2F cur{0, 0}, start{0, 0}, ctrl{0, 0};
    bool open = false, lastCurve = false;
    char cmd = 0;
    while (true) {
        skip();
        if (!*p) break;
        if (isalpha((unsigned char)*p)) cmd = *p++;
        else if (!cmd) break;
        bool rel = islower((unsigned char)cmd);
        char up = (char)toupper((unsigned char)cmd);
        if (up == 'Z') {
            if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            open = false;
            cur = start;
            lastCurve = false;
            cmd = 0;
            continue;
        }
        do {
            float v[6];
            if (up == 'M' || up == 'L') {
                if (!number(v[0]) || !number(v[1])) return nullptr;
                D2D1_POINT_2F pt{rel ? cur.x + v[0] : v[0], rel ? cur.y + v[1] : v[1]};
                if (up == 'M') {
                    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure(T(pt), D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                    start = pt;
                    cmd = rel ? 'l' : 'L';  // following pairs are line-tos
                    up = 'L';
                } else {
                    sink->AddLine(T(pt));
                }
                cur = pt;
                lastCurve = false;
            } else if (up == 'H' || up == 'V') {
                if (!number(v[0])) return nullptr;
                D2D1_POINT_2F pt = cur;
                if (up == 'H') pt.x = rel ? cur.x + v[0] : v[0];
                else pt.y = rel ? cur.y + v[0] : v[0];
                sink->AddLine(T(pt));
                cur = pt;
                lastCurve = false;
            } else if (up == 'C' || up == 'S') {
                int n = up == 'C' ? 6 : 4;
                for (int i = 0; i < n; i++)
                    if (!number(v[i])) return nullptr;
                D2D1_POINT_2F c1, c2, pt;
                int k = 0;
                if (up == 'C') {
                    c1 = {rel ? cur.x + v[0] : v[0], rel ? cur.y + v[1] : v[1]};
                    k = 2;
                } else {
                    c1 = lastCurve ? D2D1_POINT_2F{2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y} : cur;
                }
                c2 = {rel ? cur.x + v[k] : v[k], rel ? cur.y + v[k + 1] : v[k + 1]};
                pt = {rel ? cur.x + v[k + 2] : v[k + 2], rel ? cur.y + v[k + 3] : v[k + 3]};
                sink->AddBezier(D2D1::BezierSegment(T(c1), T(c2), T(pt)));
                ctrl = c2;
                cur = pt;
                lastCurve = true;
            } else {
                return nullptr;  // unsupported command
            }
        } while (hasNumber());
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

// The Spotify icon (24x24 path from the Simple Icons set, CC0): a green disc
// with three arcs cut out, drawn over a black disc so the arcs are black.
static void DrawSpotifyLogo(Canvas& surf, ID2D1RenderTarget* rt, D2D1_POINT_2F c, float d,
                            float opacity) {
    static const char* kPath =
        "M12 0C5.4 0 0 5.4 0 12s5.4 12 12 12 12-5.4 12-12S18.66 0 12 0zm5.521 17.34c-.24.359-.66.48-1.021"
        ".24-2.82-1.74-6.36-2.101-10.561-1.141-.418.122-.779-.179-.899-.539-.12-.421.18-.78.54-.9 4.56-1.021"
        " 8.52-.6 11.64 1.32.42.18.479.659.301 1.02zm1.44-3.3c-.301.42-.841.6-1.262.3-3.239-1.98-8.159-2.58"
        "-11.939-1.38-.479.12-1.02-.12-1.14-.6-.12-.48.12-1.021.6-1.141C9.6 9.9 15 10.561 18.72 12.84c.361"
        ".181.54.78.241 1.2zm.12-3.36C15.24 8.4 8.82 8.16 5.16 9.301c-.6.179-1.2-.181-1.38-.721-.18-.601.18"
        "-1.2.72-1.381 4.26-1.26 11.28-1.02 15.721 1.621.539.3.719 1.02.419 1.56-.299.421-1.02.599-1.559.3z";
    float r = d / 2;
    rt->FillEllipse(D2D1::Ellipse(c, r * 0.94f, r * 0.94f), surf.Brush(Rgba(0x000000, opacity)));
    auto geo = SvgPathGeometry(kPath, d / 24.f, D2D1_POINT_2F{c.x - r, c.y - r});
    if (geo) rt->FillGeometry(geo.get(), surf.Brush(Rgba(0x1ED760, opacity)));
    else rt->FillEllipse(D2D1::Ellipse(c, r, r), surf.Brush(Rgba(0x1ED760, opacity)));
}

static Glyph DeviceGlyph(const std::wstring& type) {
    std::wstring t = Lower(type);
    if (t == L"computer") return Glyph::Laptop;
    if (t == L"smartphone") return Glyph::Phone;
    if (t == L"tablet") return Glyph::Tablet;
    if (t == L"tv" || t == L"castvideo" || t == L"stb") return Glyph::Tv;
    if (t == L"gameconsole") return Glyph::Game;
    if (t == L"automobile") return Glyph::Car;
    return Glyph::Speaker;
}

enum class Ease { Out, In, OutQuint };

// A tiny tween: value eases from its current value to a target.
struct Tween {
    float value = 0, from = 0, to = 0, dur = 0;
    ULONGLONG start = 0;
    Ease ease = Ease::Out;
    void Snap(float v) {
        value = from = to = v;
        dur = 0;
    }
    void To(float target, float ms, Ease e = Ease::Out) {
        if (target == to && (dur > 0 || value == target)) return;
        if (ms <= 0) {
            Snap(target);
            return;
        }
        from = value;
        to = target;
        start = Tick();
        dur = ms;
        ease = e;
    }
    bool Step() {
        if (dur <= 0) return false;
        float t = Clamp01((Tick() - start) / dur);
        float k = ease == Ease::In         ? t * t * t
                  : ease == Ease::OutQuint ? 1 - std::pow(1 - t, 5.f)
                                           : EaseOutCubic(t);
        value = from + (to - from) * k;
        if (t >= 1) {
            value = to;
            dur = 0;
            return false;
        }
        return true;
    }
};

static bool IsLightTaskbar() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return false;
    return value != 0;
}

}  // namespace tp

namespace tp {

// ============================================================================
// UI state: latest snapshot + optimistic overrides (UI thread only)
// ============================================================================

class UiState {
   public:
    void SetSnapshot(std::shared_ptr<const Snapshot> s) {
        raw_ = std::move(s);
        Recompute();
    }
    void Refresh() { Recompute(); }
    const Snapshot* Eff() const { return eff_.get(); }

    void OverridePlaying(bool v) {
        if (!eff_) return;
        playAnchorPos_ = eff_->PosNow();
        playAnchorTick_ = Tick();
        playing_ = v;
        playingUntil_ = Tick() + 3500;
        Recompute();
    }
    void OverrideSeek(int64_t pos) {
        seek_ = pos;
        seekTick_ = Tick();
        seekUntil_ = Tick() + 3000;
        Recompute();
    }
    void OverrideShuffle(bool v) {
        shuffle_ = v;
        shuffleUntil_ = Tick() + 3500;
        Recompute();
    }
    void OverrideRepeat(RepeatMode v) {
        repeat_ = v;
        repeatUntil_ = Tick() + 3500;
        Recompute();
    }
    void OverrideLiked(int v) {
        liked_ = v;
        likedUntil_ = Tick() + 5000;
        Recompute();
    }
    void OverrideVolume(int v) {
        volume_ = v;
        volumeUntil_ = Tick() + 3000;
        Recompute();
    }
    void OverrideDevice(const std::wstring& id) {
        device_ = id;
        deviceUntil_ = Tick() + 6000;
        Recompute();
    }

   private:
    void Recompute() {
        if (!raw_) {
            eff_ = nullptr;
            heldArt_ = nullptr;
            return;
        }
        auto e = std::make_shared<Snapshot>(*raw_);
        ULONGLONG now = Tick();
        // Keep the cover while the same track's artwork is being reloaded.
        if (!e->art && heldArt_ && heldArtKey_ == e->trackKey) e->art = heldArt_;
        if (e->art) {
            heldArt_ = e->art;
            heldArtKey_ = e->trackKey;
        }
        if (raw_->trackKey != trackKey_) {
            trackKey_ = raw_->trackKey;
            seek_.reset();
            liked_.reset();
        }
        if (playing_) {
            if (now > playingUntil_ || raw_->playing == *playing_) {
                playing_.reset();
            } else {
                e->playing = *playing_;
                e->posMs = playAnchorPos_;
                e->posTick = playAnchorTick_;
            }
        }
        if (seek_) {
            int64_t expected = *seek_ + (e->playing ? (int64_t)(now - seekTick_) : 0);
            if (now > seekUntil_ || std::llabs(raw_->PosNow() - expected) < 1500) {
                seek_.reset();
            } else {
                e->posMs = *seek_;
                e->posTick = seekTick_;
            }
        }
        if (shuffle_) {
            if (now > shuffleUntil_ || raw_->shuffle == *shuffle_) shuffle_.reset();
            else e->shuffle = *shuffle_;
        }
        if (repeat_) {
            if (now > repeatUntil_ || raw_->repeat == *repeat_) repeat_.reset();
            else e->repeat = *repeat_;
        }
        if (liked_) {
            if (now > likedUntil_ || raw_->liked == *liked_) liked_.reset();
            else e->liked = *liked_;
        }
        if (volume_) {
            if (now > volumeUntil_ || raw_->volume == *volume_) volume_.reset();
            else e->volume = *volume_;
        }
        if (device_) {
            if (now > deviceUntil_ || raw_->deviceId == *device_) {
                device_.reset();
            } else {
                e->deviceId = *device_;
                for (auto& d : e->devices) {
                    d.active = d.id == *device_;
                    if (d.active) {
                        e->deviceName = d.name;
                        e->deviceType = d.type;
                    }
                }
            }
        }
        eff_ = e;
    }

    std::shared_ptr<const Snapshot> raw_;
    std::shared_ptr<Snapshot> eff_;
    std::shared_ptr<Art> heldArt_;
    std::wstring heldArtKey_;
    std::wstring trackKey_;
    std::optional<bool> playing_;
    int64_t playAnchorPos_ = 0;
    ULONGLONG playAnchorTick_ = 0, playingUntil_ = 0;
    std::optional<int64_t> seek_;
    ULONGLONG seekTick_ = 0, seekUntil_ = 0;
    std::optional<bool> shuffle_;
    ULONGLONG shuffleUntil_ = 0;
    std::optional<RepeatMode> repeat_;
    ULONGLONG repeatUntil_ = 0;
    std::optional<int> liked_;
    ULONGLONG likedUntil_ = 0;
    std::optional<int> volume_;
    ULONGLONG volumeUntil_ = 0;
    std::optional<std::wstring> device_;
    ULONGLONG deviceUntil_ = 0;
};

static UiState& g_ui = *new UiState();  // never destroyed, see g_core

// Common actions used by the widget and the popup.
static void UiTogglePlay() {
    const Snapshot* e = g_ui.Eff();
    if (!e || !e->canPlayPause) return;
    bool want = !e->playing;
    g_ui.OverridePlaying(want);
    SendAct(Act::PlayPause, want ? 1 : 0);
}
static void UiNext() {
    const Snapshot* e = g_ui.Eff();
    if (e && e->canNext) SendAct(Act::Next);
}
static void UiPrev() {
    const Snapshot* e = g_ui.Eff();
    if (e && e->canPrev) SendAct(Act::Prev);
}

// Implemented by the popup.
static void PopupToggle(const RECT& anchor);
static void PopupOnSnapshot();
static void PopupRelayout();

// ============================================================================
// Taskbar widget
// ============================================================================

class Widget {
   public:
    static constexpr const wchar_t* kClass = L"TaskbarPlayer_Widget";

    bool Create(HWND taskbar) {
        taskbar_ = taskbar;
        overlay_ = GetSettings()->overlayMode;
        light_ = IsLightTaskbar();
        if (overlay_) {
            hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"",
                                    WS_POPUP, 0, 0, 1, 1, taskbar, nullptr, ModuleInstance(), this);
        } else {
            hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"",
                                    WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, taskbar, nullptr,
                                    ModuleInstance(), this);
        }
        if (!hwnd_) {
            Wh_Log(L"Widget window creation failed: %u", GetLastError());
            return false;
        }
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd_,
                                   nullptr, ModuleInstance(), nullptr);
        if (tooltip_) {
            TTTOOLINFOW ti{sizeof(ti)};
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = hwnd_;
            ti.uId = 1;
            ti.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 600);
            ApplyTooltipTheme();
        }
        SetTimer(hwnd_, kTimerHousekeeping, 2000, nullptr);
        Relayout();
        return true;
    }

    void Destroy() {
        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        tooltip_ = nullptr;
    }

    HWND Hwnd() const { return hwnd_; }
    bool Overlay() const { return overlay_; }
    float Scale() const { return scale_; }

    RECT ScreenRect() const {
        RECT r{};
        if (hwnd_) GetWindowRect(hwnd_, &r);
        if (r.right <= r.left) {
            GetWindowRect(taskbar_, &r);
            r.right = r.left + 1;
        }
        return r;
    }

    void OnSnapshot() {
        const Snapshot* e = g_ui.Eff();
        bool want = e && e->visible;
        if (want && winAlpha_ < 0.02f) playT_.Snap(e->playing ? 0.f : 1.f);  // no morph when appearing
        else SyncPlayIcon();
        if (!want) {
            hasPending_ = false;
            if (visible_) {
                visible_ = false;
                StartWindowFade(0.f);
            }
            return;
        }
        Content c = MakeContent(*e);
        if (!visible_) {
            visible_ = true;
            g_widgetVisible = true;
            bool hidden = !IsWindowVisible(hwnd_) || winAlpha_ < 0.02f;
            if (hidden || c.key != shown_.key) {
                shown_ = c;
                hasPending_ = false;
                phase_ = Phase::Idle;
                contentAlpha_ = 1.f;
                width_ = widthTarget_ = ContentWidth(shown_);
                widthAnim_ = false;
            }
            Render();
            if (!IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_SHOWNA);
            EnsureOnTop();
            StartWindowFade(1.f);
            return;
        }
        if (c.key != shown_.key) {
            pending_ = c;
            hasPending_ = true;
            if (phase_ != Phase::FadeOut) {
                phase_ = Phase::FadeOut;
                phaseFrom_ = contentAlpha_;
                phaseStart_ = Tick();
                StartAnimation();
            }
            return;
        }
        if (hasPending_) {
            // The track flipped back before the fade finished.
            hasPending_ = false;
            if (phase_ == Phase::FadeOut) {
                phase_ = Phase::FadeIn;
                phaseStart_ = Tick() - (ULONGLONG)(contentAlpha_ * 200);
                StartAnimation();
            }
        }
        shown_ = c;
        UpdateWidthTarget();
        Render();
    }

    void Relayout() {
        if (!hwnd_ || !taskbar_) return;
        UINT dpi = GetDpiForWindow(taskbar_);
        scale_ = (dpi ? dpi : 96) / 96.f;
        g_uiScaleMilli = (int)std::lround(scale_ * 1000);
        RECT rc{};
        GetClientRect(taskbar_, &rc);
        heightPx_ = std::max<int>(rc.bottom - rc.top, (int)(32 * scale_));
        tbClientWidth_ = rc.right - rc.left;
        widthTarget_ = ContentWidth(shown_);
        if (!widthAnim_) width_ = widthTarget_;
        Reposition();
        Render();
        PopupRelayout();
    }

    void OnSettingsChanged() {
        light_ = IsLightTaskbar();
        ApplyTooltipTheme();
        Relayout();
    }

#ifdef TP_HARNESS
    // Skips animations and renders with the given hover part (test harness only).
    void DebugRender(int hover) {
        winAlpha_ = 1.f;
        winAnim_ = false;
        if (hasPending_) shown_ = pending_;
        hasPending_ = false;
        phase_ = Phase::Idle;
        contentAlpha_ = 1.f;
        widthAnim_ = false;
        width_ = widthTarget_ = ContentWidth(shown_);
        hover_ = hover;
        Render();
    }
    const Surface& DebugSurface() const { return surf_; }
#endif

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Widget* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = (Widget*)((CREATESTRUCTW*)lp)->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = hwnd;
        } else {
            self = (Widget*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        return self->Handle(msg, wp, lp);
    }

   private:
    enum { kTimerAnim = 1, kTimerHousekeeping = 2 };
    enum Part { kNone = -1, kInfo = 0, kPrev = 1, kPlay = 2, kNext = 3 };
    enum class Phase { Idle, FadeOut, FadeIn };

    struct Content {
        std::wstring key, title, artist;
        std::shared_ptr<Art> art;
        std::shared_ptr<AppInfo> app;
        bool isSpotify = false, connect = false;
    };

    static Content MakeContent(const Snapshot& e) {
        Content c;
        c.key = e.trackKey;
        c.title = e.title;
        c.artist = e.artist;
        c.art = e.art;
        c.app = e.app;
        c.isSpotify = e.isSpotify;
        c.connect = e.connectPrompt;
        if (c.title.empty()) c.title = e.appName.empty() ? L"Unknown title" : e.appName;
        else if (c.artist.empty() && !e.connectPrompt) c.artist = e.appName;  // e.g. videos
        return c;
    }

    // Layout metrics in physical pixels.
    float S(float v) const { return v * scale_; }
    float Pad() const { return S(6); }
    float CoverSize() const { return std::min(S(32), heightPx_ - S(8)); }
    float GapCover() const { return S(9); }
    float GapText() const { return S(8); }
    float Btn() const { return S(32); }
    float PadRight() const { return S(2); }
    float HoverH() const { return std::min(heightPx_ - S(8), S(40)); }
    bool Controls(const Content& c) const { return GetSettings()->showControls && !c.connect; }
    IDWriteTextFormat* TitleFont() const { return g_gfx->Font(false, S(12.5f), DWRITE_FONT_WEIGHT_SEMI_BOLD); }
    IDWriteTextFormat* ArtistFont() const { return g_gfx->Font(false, S(11.5f), DWRITE_FONT_WEIGHT_NORMAL); }
    float LeftPx() const { return S((float)GetSettings()->leftMargin); }

    float TextW(const Content& c) const {
        float tw = std::max(TextWidth(c.title, TitleFont()), TextWidth(c.artist, ArtistFont()));
        tw = std::min(tw, S((float)GetSettings()->maxTextWidth));
        float fixed = FixedW(c);
        int start = g_startButtonLeft;
        float left = LeftPx();
        // Never run into the centered taskbar icons.
        if (start > left + fixed + S(24)) {
            float avail = start - left - S(10) - fixed;
            tw = std::min(tw, std::max(0.f, avail));
        }
        if (tw < S(24)) tw = 0;
        return std::floor(tw);
    }
    float FixedW(const Content& c) const {
        return Pad() + CoverSize() + GapCover() + GapText() + (Controls(c) ? 3 * Btn() + PadRight() : 0);
    }
    float ContentWidth(const Content& c) const {
        float tw = TextW(c);
        float w = FixedW(c) + tw;
        if (tw <= 0) w -= GapCover();
        return std::ceil(w);
    }

    void UpdateWidthTarget() {
        float t = ContentWidth(shown_);
        if (std::fabs(t - widthTarget_) < 0.5f) return;
        if (!visible_ || winAlpha_ < 0.02f) {
            width_ = widthTarget_ = t;
            Reposition();
            return;
        }
        widthFrom_ = width_;
        widthTarget_ = t;
        widthStart_ = Tick();
        widthAnim_ = true;
        StartAnimation();
    }

    D2D1_RECT_F InfoRect(const Content& c, float W) const {
        float right = Controls(c) ? W - PadRight() - 3 * Btn() : W;
        float hh = HoverH();
        return RectF(0, (heightPx_ - hh) / 2, std::max(right, 1.f), (heightPx_ + hh) / 2);
    }
    D2D1_RECT_F ButtonRect(int i, float W) const {
        float x0 = W - PadRight() - 3 * Btn() + i * Btn();
        return RectF(x0, 0, x0 + Btn(), (float)heightPx_);
    }

    int HitTest(float x, float y) const {
        float W = std::ceil(width_);
        if (x < 0 || y < 0 || x >= W || y >= heightPx_) return kNone;
        if (Controls(shown_)) {
            for (int i = 0; i < 3; i++)
                if (Contains(ButtonRect(i, W), x, y)) return kPrev + i;
        }
        D2D1_RECT_F info = InfoRect(shown_, W);
        if (x < info.right) return kInfo;
        return kNone;
    }

    void Reposition() {
        if (!hwnd_) return;
        int x = (int)std::lround(LeftPx());
        if (overlay_) {
            RECT tr{};
            GetWindowRect(taskbar_, &tr);
            screenPos_ = POINT{tr.left + x, tr.top};
        } else {
            SetWindowPos(hwnd_, nullptr, x, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (tooltip_) {
            TTTOOLINFOW ti{sizeof(ti)};
            ti.hwnd = hwnd_;
            ti.uId = 1;
            D2D1_RECT_F r = InfoRect(shown_, std::ceil(width_));
            ti.rect = RECT{(LONG)r.left, 0, (LONG)r.right, heightPx_};
            SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
    }

    void EnsureOnTop() {
        if (overlay_ || !hwnd_) return;
        if (GetWindow(taskbar_, GW_CHILD) != hwnd_)
            SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void ApplyTooltipTheme() {
        if (tooltip_) SetWindowTheme(tooltip_, light_ ? nullptr : L"DarkMode_Explorer", nullptr);
    }

    void StartWindowFade(float to) {
        winFrom_ = winAlpha_;
        winTo_ = to;
        winStart_ = Tick();
        winAnim_ = true;
        StartAnimation();
    }

    void StartAnimation() {
        if (!animating_) {
            animating_ = true;
            SetTimer(hwnd_, kTimerAnim, 15, nullptr);
        }
    }

    void Animate() {
        ULONGLONG now = Tick();
        bool active = false;
        if (winAnim_) {
            float dur = winTo_ > winFrom_ ? 220.f : 180.f;
            float t = (now - winStart_) / dur;
            winAlpha_ = winFrom_ + (winTo_ - winFrom_) * EaseOutCubic(t);
            if (t >= 1) {
                winAnim_ = false;
                winAlpha_ = winTo_;
                if (winTo_ <= 0) {
                    ShowWindow(hwnd_, SW_HIDE);
                    g_widgetVisible = false;
                }
            } else {
                active = true;
            }
        }
        if (phase_ == Phase::FadeOut) {
            float t = (now - phaseStart_) / 120.f;
            contentAlpha_ = phaseFrom_ * (1 - EaseOutCubic(t));
            if (t >= 1) {
                contentAlpha_ = 0;
                if (hasPending_) shown_ = pending_;
                hasPending_ = false;
                phase_ = Phase::FadeIn;
                phaseStart_ = now;
                UpdateWidthTarget();
            }
            active = true;
        } else if (phase_ == Phase::FadeIn) {
            float t = (now - phaseStart_) / 220.f;
            contentAlpha_ = EaseOutCubic(t);
            if (t >= 1) {
                phase_ = Phase::Idle;
                contentAlpha_ = 1;
            } else {
                active = true;
            }
        }
        if (widthAnim_) {
            float t = (now - widthStart_) / 240.f;
            width_ = widthFrom_ + (widthTarget_ - widthFrom_) * EaseInOutCubic(t);
            if (t >= 1) {
                widthAnim_ = false;
                width_ = widthTarget_;
                Reposition();
            } else {
                active = true;
            }
        }
        if (hlT_.Step()) active = true;
        if (playT_.Step()) active = true;
        if (winAlpha_ > 0 || winAnim_) Render();
        if (!active) {
            KillTimer(hwnd_, kTimerAnim);
            animating_ = false;
        }
    }

    void Render() {
        if (!hwnd_ || !g_gfx) return;
        int W = std::max(1, (int)std::ceil(width_));
        int H = std::max(1, heightPx_);
        if (!surf_.Resize(W, H)) return;
        ID2D1RenderTarget* rt = surf_.Begin();
        if (!rt) return;
        Draw(rt, (float)W);
        if (!surf_.End()) {
            InvalidateLater();
            return;
        }
        BYTE alpha = (BYTE)std::lround(Clamp01(winAlpha_) * 255);
        surf_.Present(hwnd_, overlay_ ? &screenPos_ : nullptr, alpha);
    }

    void InvalidateLater() { PostMessageW(hwnd_, WM_TP_RELAYOUT, 0, 0); }

    void Draw(ID2D1RenderTarget* rt, float W) {
        const Snapshot* e = g_ui.Eff();
        const Content& c = shown_;
        D2D1_COLOR_F fg = light_ ? Rgba(0x000000, 0.92f) : Rgba(0xFFFFFF, 1.f);
        D2D1_COLOR_F fg2 = light_ ? Rgba(0x000000, 0.62f) : Rgba(0xFFFFFF, 0.72f);
        D2D1_COLOR_F hoverBg = light_ ? Rgba(0x000000, 0.06f) : Rgba(0xFFFFFF, 0.085f);
        D2D1_COLOR_F pressBg = light_ ? Rgba(0x000000, 0.04f) : Rgba(0xFFFFFF, 0.055f);
        float radius = S((float)GetSettings()->hoverRadius);
        float hh = HoverH();

        // Layered windows let clicks through fully transparent pixels. An
        // invisible (alpha 2/255) backdrop makes the whole widget clickable.
        rt->FillRectangle(RectF(0, 0, W, (float)heightPx_), surf_.Brush(D2D1_COLOR_F{0, 0, 0, 2.f / 255}));

        // Hover highlight (fades in and out).
        int hl = pressed_ != kNone ? pressed_ : hlPart_;
        float hla = pressed_ != kNone ? 1.f : hlT_.value;
        if (hl != kNone && hla > 0.01f) {
            D2D1_RECT_F r = hl == kInfo ? InfoRect(c, W) : ButtonRect(hl - 1, W);
            if (hl != kInfo) r = RectF(r.left, (heightPx_ - hh) / 2, r.right, (heightPx_ + hh) / 2);
            float rr = std::min(radius, Height(r) / 2);
            D2D1_COLOR_F bg = pressed_ != kNone && pressed_ == hover_ ? pressBg : hoverBg;
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, rr, rr), surf_.Brush(WithAlpha(bg, bg.a * hla)));
        }

        float ca = contentAlpha_;
        float cs = CoverSize();
        D2D1_RECT_F cover = RectXYWH(Pad(), std::round((heightPx_ - cs) / 2), cs, cs);
        float coverRadius = S(5);
        if (c.connect) {
            DrawSpotifyLogo(surf_, rt, Center(cover), cs * 0.78f, ca);
        } else if (c.art) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(cover, coverRadius, coverRadius),
                                     surf_.Brush(WithAlpha(fg, 0.10f * ca)));
            IWICBitmapSource* src = c.art->small && c.art->smallPx == (UINT)std::lround(cs)
                                        ? (IWICBitmapSource*)c.art->small.get()
                                        : (IWICBitmapSource*)c.art->full.get();
            DrawRoundedBitmap(rt, surf_.Bitmap(src), cover, coverRadius, ca);
        } else {
            rt->FillRoundedRectangle(D2D1::RoundedRect(cover, coverRadius, coverRadius),
                                     surf_.Brush(WithAlpha(fg, 0.12f * ca)));
            if (c.isSpotify) DrawSpotifyLogo(surf_, rt, Center(cover), cs * 0.55f, ca);
            else DrawIcon(rt, Glyph::Music, Center(cover), cs * 0.45f, surf_.Brush(WithAlpha(fg2, ca)));
        }

        float tw = TextW(c);
        if (tw > 0) {
            float x = cover.right + GapCover();
            float maxRight = InfoRect(c, W).right - GapText();
            float w = std::max(0.f, std::min(tw, maxRight - x));
            float mid = heightPx_ / 2.f;
            if (!c.artist.empty()) {
                DrawTextLine(rt, c.title, TitleFont(), RectF(x, mid - S(17), x + w, mid + S(0.5f)),
                             surf_.Brush(WithAlpha(fg, fg.a * ca)));
                DrawTextLine(rt, c.artist, ArtistFont(), RectF(x, mid - S(0.5f), x + w, mid + S(16)),
                             surf_.Brush(WithAlpha(fg2, fg2.a * ca)));
            } else {
                DrawTextLine(rt, c.title, TitleFont(), RectF(x, mid - S(10), x + w, mid + S(10)),
                             surf_.Brush(WithAlpha(fg, fg.a * ca)));
            }
        }

        if (Controls(c)) {
            auto color = [&](bool enabled) { return enabled ? fg : WithAlpha(fg, 0.35f); };
            DrawTransport(rt, Transport::Prev, Center(ButtonRect(0, W)), S(13), surf_.Brush(color(e && e->canPrev)));
            DrawTransport(rt, Transport::Next, Center(ButtonRect(2, W)), S(13), surf_.Brush(color(e && e->canNext)));
            // Play/pause morph: one icon shrinks and fades while the other grows in.
            D2D1_POINT_2F pc = Center(ButtonRect(1, W));
            D2D1_COLOR_F pcol = color(e && e->canPlayPause);
            float m = Clamp01(playT_.value);
            if (m < 0.999f)
                DrawTransport(rt, Transport::Pause, pc, S(14) * (1.f - 0.35f * m),
                              surf_.Brush(WithAlpha(pcol, pcol.a * (1.f - m))));
            if (m > 0.001f)
                DrawTransport(rt, Transport::Play, pc, S(14) * (0.65f + 0.35f * m), surf_.Brush(WithAlpha(pcol, pcol.a * m)));
        }
    }

    void SetHover(int h) {
        if (h == hover_) return;
        hover_ = h;
        if (h != kNone) {
            if (hlPart_ != h) {
                hlPart_ = h;
                if (hlT_.value > 0.5f) hlT_.Snap(0.5f);  // moving between parts: quick re-fade
            }
            hlT_.To(1.f, GetSettings()->popupAnimations ? 120.f : 0.f);
        } else {
            hlT_.To(0.f, GetSettings()->popupAnimations ? 200.f : 0.f);
        }
        StartAnimation();
        Render();
    }

    void SyncPlayIcon() {
        const Snapshot* e = g_ui.Eff();
        float target = e && e->playing ? 0.f : 1.f;
        if (target != playT_.to) {
            playT_.To(target, GetSettings()->popupAnimations ? 200.f : 0.f);
            StartAnimation();
        }
    }

    void Click(int part) {
        Trace(L"Widget click: part=%d connectPrompt=%d", part, shown_.connect);
        switch (part) {
            case kInfo:
                if (shown_.connect) SendAct(Act::Connect);
                else PopupToggle(ScreenRect());
                break;
            case kPrev:
                UiPrev();
                break;
            case kPlay:
                UiTogglePlay();
                break;
            case kNext:
                UiNext();
                break;
        }
        SyncPlayIcon();
        Render();
    }

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_SETCURSOR:
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            case WM_MOUSEMOVE: {
                if (!tracking_) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                    TrackMouseEvent(&tme);
                    tracking_ = true;
                }
                SetHover(HitTest((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)));
                return 0;
            }
            case WM_MOUSELEAVE:
                tracking_ = false;
                SetHover(kNone);
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                pressed_ = HitTest((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                if (pressed_ != kNone) SetCapture(hwnd_);
                Render();
                return 0;
            case WM_LBUTTONUP: {
                int part = pressed_;
                pressed_ = kNone;
                if (GetCapture() == hwnd_) ReleaseCapture();
                int h = HitTest((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
                Trace(L"Widget mouse up at (%d,%d): pressed=%d now=%d", GET_X_LPARAM(lp), GET_Y_LPARAM(lp), part, h);
                if (part != kNone && part == h) Click(part);
                else Render();
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (pressed_ != kNone) {
                    pressed_ = kNone;
                    Render();
                }
                return 0;
            case WM_NOTIFY: {
                auto* hdr = (NMHDR*)lp;
                if (hdr->hwndFrom == tooltip_ && hdr->code == TTN_GETDISPINFOW) {
                    auto* di = (NMTTDISPINFOW*)lp;
                    tipText_ = shown_.title;
                    if (!shown_.artist.empty()) tipText_ += L"\n" + shown_.artist;
                    di->lpszText = tipText_.data();
                    return 0;
                }
                break;
            }
            case WM_TIMER:
                if (wp == kTimerAnim) {
                    Animate();
                } else if (wp == kTimerHousekeeping) {
                    RECT rc{};
                    GetClientRect(taskbar_, &rc);
                    UINT dpi = GetDpiForWindow(taskbar_);
                    if (rc.bottom - rc.top != heightPx_ || rc.right - rc.left != tbClientWidth_ ||
                        std::fabs((dpi ? dpi : 96) / 96.f - scale_) > 0.001f) {
                        Relayout();
                    } else if (overlay_) {
                        RECT tr{};
                        GetWindowRect(taskbar_, &tr);
                        if (tr.left + (LONG)std::lround(LeftPx()) != screenPos_.x || tr.top != screenPos_.y) {
                            Reposition();
                            Render();
                        }
                    }
                    if (visible_) EnsureOnTop();
                }
                return 0;
            case WM_TP_SNAPSHOT:
                g_ui.SetSnapshot(LatestSnapshot());
                OnSnapshot();
                PopupOnSnapshot();
                return 0;
            case WM_TP_RELAYOUT:
                Relayout();
                return 0;
            case WM_TP_SETTINGS:
                OnSettingsChanged();
                return 0;
            case WM_DPICHANGED_AFTERPARENT:
            case WM_DISPLAYCHANGE:
                Relayout();
                return 0;
            case WM_NCDESTROY: {
                HWND h = hwnd_;
                KillTimer(h, kTimerAnim);
                KillTimer(h, kTimerHousekeeping);
                surf_.Release();
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                tooltip_ = nullptr;
                animating_ = false;
                return DefWindowProcW(h, msg, wp, lp);
            }
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    HWND hwnd_ = nullptr, taskbar_ = nullptr, tooltip_ = nullptr;
    bool overlay_ = false, light_ = false;
    Surface surf_;
    float scale_ = 1.f;
    int heightPx_ = 48, tbClientWidth_ = 0;
    POINT screenPos_{0, 0};

    Content shown_, pending_;
    bool hasPending_ = false;
    bool visible_ = false;
    Phase phase_ = Phase::Idle;
    float phaseFrom_ = 1.f;
    ULONGLONG phaseStart_ = 0;
    float contentAlpha_ = 1.f;

    float winAlpha_ = 0.f, winFrom_ = 0.f, winTo_ = 0.f;
    ULONGLONG winStart_ = 0;
    bool winAnim_ = false;

    float width_ = 1.f, widthFrom_ = 1.f, widthTarget_ = 1.f;
    ULONGLONG widthStart_ = 0;
    bool widthAnim_ = false;

    bool animating_ = false, tracking_ = false;
    int hover_ = kNone, pressed_ = kNone;
    int hlPart_ = kNone;  // part whose highlight is fading
    Tween hlT_, playT_;
    std::wstring tipText_;
};

}  // namespace tp

namespace tp {

// ============================================================================
// Opening the source app / track
// ============================================================================

static const PROPERTYKEY kPKEY_AppUserModel_ID = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

struct FindWindowCtx {
    std::wstring aumid, exe;
    HWND found = nullptr;
};

static BOOL CALLBACK FindAppWindowProc(HWND hwnd, LPARAM lp) {
    auto* ctx = (FindWindowCtx*)lp;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
    com_ptr<IPropertyStore> store;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, __uuidof(IPropertyStore), store.put_void()))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(store->GetValue(kPKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR &&
            pv.pwszVal && _wcsicmp(pv.pwszVal, ctx->aumid.c_str()) == 0) {
            ctx->found = hwnd;
        }
        PropVariantClear(&pv);
        if (ctx->found) return FALSE;
    }
    if (!ctx->exe.empty()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            wchar_t path[MAX_PATH * 2];
            DWORD len = ARRAYSIZE(path);
            if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
                const wchar_t* name = wcsrchr(path, L'\\');
                name = name ? name + 1 : path;
                if (_wcsicmp(name, ctx->exe.c_str()) == 0) ctx->found = hwnd;
            }
            CloseHandle(proc);
        }
    }
    return ctx->found ? FALSE : TRUE;
}

static void ActivateApp(const std::wstring& aumid) {
    if (aumid.empty()) return;
    FindWindowCtx ctx;
    ctx.aumid = aumid;
    if (IContains(aumid, L".exe")) ctx.exe = aumid.substr(aumid.find_last_of(L"\\/") + 1);
    EnumWindows(FindAppWindowProc, (LPARAM)&ctx);
    if (ctx.found) {
        if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
        return;
    }
    std::wstring target = L"shell:AppsFolder\\" + aumid;
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenSpotifyUri(const std::wstring& uri) {
    HKEY key = nullptr;
    bool hasApp = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"spotify", 0, KEY_READ, &key) == ERROR_SUCCESS;
    if (key) RegCloseKey(key);
    if (hasApp) {
        ShellExecuteW(nullptr, L"open", uri.empty() ? L"spotify:" : uri.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return;
    }
    std::wstring url = L"https://open.spotify.com/";
    if (uri.rfind(L"spotify:", 0) == 0) {
        std::wstring rest = uri.substr(8);
        std::replace(rest.begin(), rest.end(), L':', L'/');
        url += rest;
    }
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// "ClassName (process.exe)" for diagnostics.
static std::wstring WindowDesc(HWND h) {
    if (!h) return L"(none)";
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    std::wstring exe;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH * 2];
        DWORD len = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
            const wchar_t* name = wcsrchr(path, L'\\');
            exe = name ? name + 1 : path;
        }
        CloseHandle(proc);
    }
    return std::wstring(cls) + L" (" + exe + L")";
}

static std::wstring ThisComputerName() {
    static std::wstring name = [] {
        wchar_t buf[256];
        DWORD len = ARRAYSIZE(buf);
        if (GetComputerNameExW(ComputerNameDnsHostname, buf, &len)) return std::wstring(buf, len);
        len = ARRAYSIZE(buf);
        if (GetComputerNameW(buf, &len)) return std::wstring(buf, len);
        return std::wstring();
    }();
    return name;
}

static std::wstring DeviceTypeLabel(const std::wstring& type) {
    std::wstring t = Lower(type);
    if (t == L"computer") return L"Computer";
    if (t == L"smartphone") return L"Phone";
    if (t == L"tablet") return L"Tablet";
    if (t == L"speaker") return L"Speaker";
    if (t == L"tv") return L"TV";
    if (t == L"avr") return L"Receiver";
    if (t == L"stb") return L"Set-top box";
    if (t == L"audiodongle") return L"Audio dongle";
    if (t == L"gameconsole") return L"Game console";
    if (t == L"castvideo" || t == L"castaudio") return L"Google Cast";
    if (t == L"automobile") return L"Car";
    return L"Device";
}

// ============================================================================
// Window backdrop (DWM blur / acrylic)
// ============================================================================

struct TpAccentPolicy {
    int AccentState;
    int AccentFlags;
    DWORD GradientColor;  // ABGR
    int AnimationId;
};
struct TpWinCompAttrData {
    int Attrib;
    PVOID pvData;
    SIZE_T cbData;
};
using SetWindowCompositionAttribute_t = BOOL(WINAPI*)(HWND, TpWinCompAttrData*);

static void ApplyBackdrop(HWND hwnd, Settings::Backdrop mode, uint32_t argb) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    DWORD corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));
    MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    static auto setWca = (SetWindowCompositionAttribute_t)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                         "SetWindowCompositionAttribute");
    TpAccentPolicy accent{};
    if (mode == Settings::Backdrop::Blur) {
        DWM_BLURBEHIND bb{};
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.fEnable = TRUE;
        HRGN rgn = CreateRectRgn(0, 0, -1, -1);
        bb.hRgnBlur = rgn;
        DwmEnableBlurBehindWindow(hwnd, &bb);
        DeleteObject(rgn);
        accent.AccentState = 4;  // ACCENT_ENABLE_ACRYLICBLURBEHIND
        uint32_t a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
        accent.GradientColor = (a << 24) | (b << 16) | (g << 8) | r;
    }
    if (setWca) {
        TpWinCompAttrData data{19 /*WCA_ACCENT_POLICY*/, &accent, sizeof(accent)};
        setWca(hwnd, &data);
    }
    DWORD backdrop = mode == Settings::Backdrop::Acrylic ? 3 /*DWMSBT_TRANSIENTWINDOW*/ : 1 /*DWMSBT_NONE*/;
    DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &backdrop, sizeof(backdrop));
}


// ============================================================================
// Popup player
// ============================================================================

class Popup {
   public:
    static constexpr const wchar_t* kClass = L"TaskbarPlayer_Popup";
    static constexpr const wchar_t* kClassComposited = L"TaskbarPlayer_PopupGpu";

    bool Create(HWND taskbar) {
        taskbar_ = taskbar;
        // Preferred: Windows.UI.Composition with the window's blurred backdrop,
        // our own 13px rounded corners and whole-panel fades.
        classic_ = GetSettings()->popupClassic;
        if (!classic_) {
            hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClass,
                                    L"Taskbar Player", WS_POPUP, 0, 0, 1, 1, taskbar, nullptr, ModuleInstance(), this);
            if (hwnd_ && wc_.Init(hwnd_)) {
                wincomp_ = true;
                Trace(L"Popup uses Windows.UI.Composition");
                return true;
            }
            if (hwnd_) DestroyWindow(hwnd_);
        }
        // Next: DirectComposition with a DWM blur (corners fixed by Windows).
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClassComposited,
                                L"Taskbar Player", WS_POPUP, 0, 0, 1, 1, taskbar, nullptr, ModuleInstance(), this);
        if (hwnd_ && comp_.Init(hwnd_)) {
            composited_ = true;
            ApplyBackdropSettings();
            Trace(L"Popup uses DirectComposition");
            return true;
        }
        if (hwnd_) DestroyWindow(hwnd_);
        // Fallback: a layered window with a self-drawn shadow and solid background.
        composited_ = false;
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"Taskbar Player",
                                WS_POPUP, 0, 0, 1, 1, taskbar, nullptr, ModuleInstance(), this);
        Trace(L"Popup uses the layered fallback");
        return hwnd_ != nullptr;
    }

    void Destroy() {
        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    HWND Hwnd() const { return hwnd_; }
    bool IsOpen() const { return open_ && !closing_; }
    bool CreatedClassic() const { return classic_; }

    void ApplyBackdropSettings() {
        if (!composited_ || !hwnd_) return;
        auto set = GetSettings();
        ApplyBackdrop(hwnd_, set->popupBackdrop, set->popupTint);
    }

    void Toggle(const RECT& anchor) {
        if (open_ && !closing_) Close();
        else Open(anchor);
    }

    void Open(const RECT& anchor) {
        const Snapshot* e = g_ui.Eff();
        if (!hwnd_ || !e || !e->visible || e->connectPrompt) return;
        anchor_ = anchor;
        open_ = true;
        closing_ = false;
        panelOpen_ = false;
        panelT_.Snap(0);
        hover_ = pressed_ = downPart_ = kNone;
        hoverT_.clear();
        drag_ = kNone;
        lastToastSeq_ = e->toastSeq;
        toastUntil_ = 0;
        activatedOnce_ = false;
        fgAtOpen_ = GetForegroundWindow();
        // Start from the current track without a transition.
        curKey_ = e->trackKey;
        prevArt_ = nullptr;
        trackT_.Snap(1);
        playT_.Snap(e->playing ? 0.f : 1.f);
        likeT_.Snap(0);
        accentFrom_ = accentTo_ = TargetAccent(*e);
        accentT_.Snap(1);
        openT_.Snap(0);
        openT_.To(1, Dur(composited_ ? 340 : wincomp_ ? 300 : 260), Ease::OutQuint);
        g_popupOpen = true;
        SendAct(Act::PopupOpened);
        Layout();
        Render();
        if (wincomp_) {
            // The window stays put; the compositor fades/moves the content.
            POINT pos = RestWindowPos();
            SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, (int)Wnd().cx, (int)Wnd().cy,
                         SWP_SHOWWINDOW | SWP_NOACTIVATE);
        } else if (composited_) {
            RECT r = AnimatedRect();
            SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_SHOWWINDOW | SWP_NOACTIVATE);
            lastRect_ = r;
        } else {
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
        BOOL fgOk = SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
        SetTimer(hwnd_, kTimerAnim, 15, nullptr);
        SetTimer(hwnd_, kTimerTick, 250, nullptr);
        Trace(L"Popup open: composited=%d setForeground=%d foreground=%s size %dx%d", composited_, fgOk,
              WindowDesc(GetForegroundWindow()).c_str(), (int)Wnd().cx, (int)Wnd().cy);
    }

    void Close(const wchar_t* reason = L"toggle") {
        if (!open_ || closing_) return;
        Trace(L"Popup close: %s", reason);
        closing_ = true;
        panelOpen_ = false;
        downPart_ = pressed_ = kNone;
        KillTimer(hwnd_, kTimerPress);
        openT_.To(0, Dur(composited_ ? 230 : wincomp_ ? 190 : 150), composited_ ? Ease::In : Ease::Out);
        lastCloseTick_ = Tick();
        if (GetCapture() == hwnd_) ReleaseCapture();
        g_popupOpen = false;
        SendAct(Act::PopupClosed);
        SetTimer(hwnd_, kTimerAnim, 15, nullptr);
    }

    void OnSnapshot() {
        if (!open_) return;
        const Snapshot* e = g_ui.Eff();
        if (!e || !e->visible || e->connectPrompt) {
            Close(L"nothing to show");
            return;
        }
        if (e->toastSeq != lastToastSeq_) {
            lastToastSeq_ = e->toastSeq;
            if (!e->toast.empty()) {
                toast_ = e->toast;
                toastUntil_ = Tick() + 3500;
                toastStart_ = Tick();
            }
        }
        if (e->trackKey != curKey_) {
            // Track changed while open: cross-fade the cover and slide the text.
            prevArt_ = shownArt_;
            prevTitle_ = shownTitle_;
            prevArtist_ = shownArtist_;
            curKey_ = e->trackKey;
            trackT_.Snap(0);
            trackT_.To(1, Dur(420));
            likeT_.Snap(0);
        }
        D2D1_COLOR_F target = TargetAccent(*e);
        if (target.r != accentTo_.r || target.g != accentTo_.g || target.b != accentTo_.b) {
            accentFrom_ = CurrentAccent();
            accentTo_ = target;
            accentT_.Snap(0);
            accentT_.To(1, Dur(500));
        }
        playT_.To(e->playing ? 0.f : 1.f, Dur(220));
        Layout();
        Kick();
    }

    void Relayout() {
        if (!open_) return;
        Layout();
        Render();
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Popup* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = (Popup*)((CREATESTRUCTW*)lp)->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = hwnd;
        } else {
            self = (Popup*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        return self->Handle(msg, wp, lp);
    }

   private:
    enum { kTimerAnim = 1, kTimerTick = 2, kTimerVolume = 3, kTimerPress = 4 };
    enum Part {
        kNone = -1,
        kHeader,
        kCover,
        kTitle,
        kProgress,
        kShuffle,
        kPrev,
        kPlay,
        kNext,
        kRepeat,
        kLike,
        kDevices,
        kVolIcon,
        kVolume,
        kConnect,
        kPanel,
        kPanelSignOut,
        kPanelRow = 100,
    };

    // Layout in physical pixels, window coordinates.
    struct L {
        float s = 1, M = 0, W = 0, H = 0;
        bool bottom = false, spotifyRow = false, likeShown = false;
        D2D1_RECT_F card, header, cover, title, artist, like, progress, track, times;
        D2D1_RECT_F shuffle, prev, play, next, repeat;
        D2D1_RECT_F volIcon, volume, volTrack, devices, connect;
        D2D1_RECT_F panel, panelList, panelSignOut;
        float rowH = 0;
    } l_;

    float S(float v) const { return v * l_.s; }
    static float Dur(float ms) { return GetSettings()->popupAnimations ? ms : 0.f; }
    SIZE Wnd() const { return SIZE{(LONG)(l_.W + 2 * l_.M), (LONG)(l_.H + 2 * l_.M)}; }
    Canvas& Cv() { return wincomp_ ? (Canvas&)wc_ : composited_ ? (Canvas&)comp_ : (Canvas&)layered_; }
    float CardRadius() const { return S(13); }

    void Layout() {
        const Snapshot* e = g_ui.Eff();
        UINT dpi = GetDpiForWindow(taskbar_);
        float s = (dpi ? dpi : 96) / 96.f;
        l_.s = s;
        l_.M = composited_ ? 0 : std::round((wincomp_ ? 34 : 28) * s);
        l_.spotifyRow = e && e->src == Src::Spotify && e->spotifyAuthorized;
        bool connectRow = e && e->src != Src::Spotify && e->spotifyConfigured && !e->spotifyAuthorized;
        l_.bottom = l_.spotifyRow || connectRow;
        l_.likeShown = e && e->canLike;
        float W = 340, H = l_.bottom ? 566 : 526;
        l_.W = std::round(W * s);
        l_.H = std::round(H * s);
        float ox = l_.M, oy = l_.M;
        auto R = [&](float x, float y, float w, float h) {
            return RectXYWH(std::round(ox + x * s), std::round(oy + y * s), std::round(w * s), std::round(h * s));
        };
        const float P = 20;
        l_.card = RectXYWH(ox, oy, l_.W, l_.H);
        l_.header = R(P, 14, W - 2 * P, 24);
        l_.cover = R(P, 50, 300, 300);
        // Title and artist stay centered; the heart sits on the right.
        l_.title = R(P + 36, 364, W - 2 * P - 72, 26);
        l_.artist = R(P + 36, 390, W - 2 * P - 72, 20);
        l_.like = R(W - P - 34, 369, 36, 36);
        l_.progress = R(P - 6, 420, W - 2 * P + 12, 20);
        l_.track = R(P, 428, W - 2 * P, 4);
        l_.times = R(P, 438, W - 2 * P, 16);
        float cy = 486;
        auto CircleR = [&](float cx, float d) { return R(cx - d / 2, cy - d / 2, d, d); };
        l_.shuffle = CircleR(W / 2 - 112, 40);
        l_.prev = CircleR(W / 2 - 56, 44);
        l_.play = CircleR(W / 2, 52);
        l_.next = CircleR(W / 2 + 56, 44);
        l_.repeat = CircleR(W / 2 + 112, 40);
        // Bottom row: volume on the left, the device picker on the right.
        float by = 518;
        l_.volIcon = R(P - 6, by, 32, 32);
        l_.volTrack = R(P + 32, by + 14, 110, 4);
        l_.volume = R(P + 26, by, 122, 32);
        float devTextW = 0;
        if (e && l_.spotifyRow && g_gfx)
            devTextW = TextWidth(DeviceLabel(*e), g_gfx->Font(false, S(12.5f), DWRITE_FONT_WEIGHT_NORMAL)) / s;
        bool vol = e && e->canVolume;
        float devMax = vol ? 160 : W - 2 * P;
        float devW = std::min(devMax, devTextW + 42);
        l_.devices = R(W - P + 6 - devW, by, devW, 32);
        l_.connect = R(W / 2 - 100, by, 200, 32);
        l_.panel = l_.cover;
        l_.rowH = std::round(44 * s);
        l_.panelList = RectF(l_.panel.left, l_.panel.top + S(46), l_.panel.right, l_.panel.bottom - S(40));
        l_.panelSignOut = RectF(l_.panel.left + S(12), l_.panel.bottom - S(36), l_.panel.left + S(150),
                                l_.panel.bottom - S(8));
    }

    std::wstring DeviceLabel(const Snapshot& e) const { return e.deviceName.empty() ? L"Devices" : e.deviceName; }

    POINT RestWindowPos() const {
        HMONITOR mon = MonitorFromWindow(taskbar_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        RECT tb{};
        GetWindowRect(taskbar_, &tb);
        int gap = (int)std::round(S(12));
        int left = std::clamp<int>(anchor_.left, mi.rcWork.left + gap,
                                   std::max<int>(mi.rcWork.left + gap, mi.rcWork.right - gap - (int)l_.W));
        bool topTaskbar = tb.top <= mi.rcMonitor.top && tb.bottom < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
        int top = topTaskbar ? std::max<int>(mi.rcWork.top, tb.bottom) + gap
                             : std::min<int>(mi.rcWork.bottom, tb.top) - gap - (int)l_.H;
        return POINT{left - (int)l_.M, top - (int)l_.M};
    }

    POINT WindowPos() const {
        HMONITOR mon = MonitorFromWindow(taskbar_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        RECT tb{};
        GetWindowRect(taskbar_, &tb);
        int gap = (int)std::round(S(12));
        int left = std::clamp<int>(anchor_.left, mi.rcWork.left + gap,
                                   std::max<int>(mi.rcWork.left + gap, mi.rcWork.right - gap - (int)l_.W));
        bool topTaskbar = tb.top <= mi.rcMonitor.top && tb.bottom < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
        float p = openT_.value;
        int slide = (int)std::round((1 - p) * S(closing_ ? 10 : 18));
        int top;
        if (topTaskbar) {
            top = std::max<int>(mi.rcWork.top, tb.bottom) + gap - slide;
        } else {
            int bottom = std::min<int>(mi.rcWork.bottom, tb.top) - gap;
            top = bottom - (int)l_.H + slide;
        }
        return POINT{left - (int)l_.M, top - (int)l_.M};
    }

    // GPU popup: the whole panel (blur included) slides out from behind the
    // taskbar's top edge, like the Start menu, and slides back in on close.
    // The window is clipped at that edge; the swap chain keeps its full size.
    RECT AnimatedRect() const {
        HMONITOR mon = MonitorFromWindow(taskbar_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        RECT tb{};
        GetWindowRect(taskbar_, &tb);
        int gap = (int)std::round(S(12));
        int W = (int)l_.W, H = (int)l_.H;
        int left = std::clamp<int>(anchor_.left, mi.rcWork.left + gap,
                                   std::max<int>(mi.rcWork.left + gap, mi.rcWork.right - gap - W));
        bool topTaskbar = tb.top <= mi.rcMonitor.top && tb.bottom < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
        float p = Clamp01(openT_.value);
        if (topTaskbar) {
            // Rare layout: a short slide is enough.
            int top = std::max<int>(mi.rcWork.top, tb.bottom) + gap - (int)std::round((1 - p) * S(18));
            return RECT{left, top, left + W, top + H};
        }
        int edge = std::min<int>(mi.rcWork.bottom, tb.top);
        int restTop = edge - gap - H;
        int top = restTop + (int)std::round((1 - p) * (H + gap));
        int bottom = std::min(top + H, edge);
        return RECT{left, top, left + W, std::max(top + 1, bottom)};
    }

    // ------------------------------------------------------------------
    // Animation state
    // ------------------------------------------------------------------

    float HoverV(int part) const {
        auto it = hoverT_.find(part);
        return it == hoverT_.end() ? 0.f : it->second.value;
    }

    void SetHover(int part) {
        if (part == hover_) return;
        hover_ = part;
        if (part != kNone && !hoverT_.count(part)) hoverT_[part].Snap(0);
        for (auto& [p, t] : hoverT_) t.To(p == part ? 1.f : 0.f, Dur(p == part ? 120 : 200));
        Kick();
    }

    // Starts the frame timer if something is animating.
    void Kick() {
        if (!hwnd_) return;
        Render();
        if (Animating()) SetTimer(hwnd_, kTimerAnim, 15, nullptr);
    }

    bool Animating() const {
        if (openT_.dur > 0 || trackT_.dur > 0 || playT_.dur > 0 || likeT_.dur > 0 || accentT_.dur > 0 ||
            panelT_.dur > 0 || pressT_.dur > 0)
            return true;
        for (auto& [p, t] : hoverT_)
            if (t.dur > 0) return true;
        if (Tick() < toastUntil_ + 300) return true;
        return false;
    }

    void StepAll() {
        openT_.Step();
        trackT_.Step();
        playT_.Step();
        likeT_.Step();
        accentT_.Step();
        panelT_.Step();
        pressT_.Step();
        for (auto& [p, t] : hoverT_) t.Step();
    }

    D2D1_COLOR_F TargetAccent(const Snapshot& e) const {
        return e.art ? e.art->accent : Rgba(e.isSpotify ? 0x1ED760 : 0xFFFFFF);
    }
    D2D1_COLOR_F CurrentAccent() const {
        float t = accentT_.value;
        return D2D1_COLOR_F{accentFrom_.r + (accentTo_.r - accentFrom_.r) * t,
                            accentFrom_.g + (accentTo_.g - accentFrom_.g) * t,
                            accentFrom_.b + (accentTo_.b - accentFrom_.b) * t, 1.f};
    }

    // ------------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------------

    void Render() {
        if (!hwnd_ || !g_gfx || !open_) return;
        const Snapshot* e = g_ui.Eff();
        if (!e) return;
        StepAll();
        SIZE sz = Wnd();
        ID2D1RenderTarget* rt = nullptr;
        if (wincomp_) {
            if (!wc_.Resize(sz.cx, sz.cy)) return;
            rt = wc_.Begin();
        } else if (composited_) {
            if (!comp_.Resize(sz.cx, sz.cy)) return;
            rt = comp_.Begin();
        } else {
            if (!layered_.Resize(sz.cx, sz.cy)) return;
            rt = layered_.Begin();
        }
        if (!rt) return;
        float p = Clamp01(openT_.value);
        if (wincomp_) DrawShadowedCard(rt, false);
        else if (composited_) DrawCompositedBackground(rt, *e);
        else DrawShadowedCard(rt, true);

        if (!composited_ && !wincomp_) {
            // Layered fallback: content scales up slightly while the window fades in.
            float sc = 0.965f + 0.035f * p;
            D2D1_POINT_2F origin{(l_.card.left + l_.card.right) / 2, l_.card.bottom};
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, origin));
        }
        DrawContent(rt, *e);
        rt->SetTransform(D2D1::Matrix3x2F::Identity());

        bool ok = wincomp_ ? wc_.End() : composited_ ? comp_.End() : layered_.End();
        if (!ok) {
            bg_ = nullptr;
            return;
        }
        if (wincomp_) {
            // The compositor fades, lifts and scales the whole panel, blur included.
            auto set = GetSettings();
            uint32_t t = set->popupTint;
            D2D1_COLOR_F tint = Rgba(t & 0xFFFFFF, (t >> 24) / 255.f);
            if (set->popupBackdrop == Settings::Backdrop::Acrylic) tint.a = std::max(tint.a, 0.62f);
            bool backdrop = set->popupBackdrop != Settings::Backdrop::Solid;
            if (!backdrop) tint = Rgba(0x1F1F1F, 1.f);
            wc_.SetCard(l_.card, CardRadius(), tint, backdrop);
            bool topTaskbar = false;
            {
                RECT tb{};
                GetWindowRect(taskbar_, &tb);
                HMONITOR mon = MonitorFromWindow(taskbar_, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(mon, &mi);
                topTaskbar = tb.top <= mi.rcMonitor.top && tb.bottom < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
            }
            float dy = (1.f - p) * S(closing_ ? 10.f : 22.f) * (topTaskbar ? -1.f : 1.f);
            float sc = 0.96f + 0.04f * p;
            D2D1_POINT_2F center{(l_.card.left + l_.card.right) / 2, topTaskbar ? l_.card.top : l_.card.bottom};
            wc_.SetRoot(p, dy, sc, center);
            return;
        }
        if (composited_) {
            // The panel itself moves (and is clipped at the taskbar), so the blur
            // travels with the content from start to finish.
            RECT r = AnimatedRect();
            if (!EqualRect(&r, &lastRect_)) {
                SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
                lastRect_ = r;
            }
        } else {
            POINT pos = WindowPos();
            layered_.Present(hwnd_, &pos, (BYTE)std::lround(p * 255));
        }
    }

    void DrawCompositedBackground(ID2D1RenderTarget* rt, const Snapshot& e) {
        auto set = GetSettings();
        if (set->popupBackdrop == Settings::Backdrop::Solid) {
            rt->FillRectangle(l_.card, Cv().Brush(Rgba(0x1F1F1F)));
        } else if (set->popupBackdrop == Settings::Backdrop::Acrylic) {
            uint32_t t = set->popupTint;
            rt->FillRectangle(l_.card, Cv().Brush(Rgba(t & 0xFFFFFF, (t >> 24) / 255.f)));
        }
        DrawGlow(rt, e);
    }

    // Soft shadow around the card (cached). With `fillCard` the card gets a solid
    // background (layered fallback); otherwise the card area is left clear so
    // the compositor's blurred backdrop shows through.
    void DrawShadowedCard(ID2D1RenderTarget* rt, bool fillCard) {
        Canvas& cv = Cv();
        int Wp = cv.w, Hp = cv.h;
        float radius = CardRadius();
        if (!bg_ || bgW_ != Wp || bgH_ != Hp || bgGen_ != cv.gen || bgFilled_ != fillCard) {
            bg_ = nullptr;
            com_ptr<ID2D1BitmapRenderTarget> brt;
            if (SUCCEEDED(rt->CreateCompatibleRenderTarget(D2D1::SizeF((float)Wp, (float)Hp), brt.put()))) {
                brt->BeginDraw();
                brt->Clear(D2D1_COLOR_F{0, 0, 0, 0});
                com_ptr<ID2D1SolidColorBrush> b;
                brt->CreateSolidColorBrush(D2D1_COLOR_F{0, 0, 0, 0}, b.put());
                // Mask that excludes the card, so the shadow never darkens the glass.
                com_ptr<ID2D1PathGeometry> outside;
                com_ptr<ID2D1Layer> layer;
                if (!fillCard) {
                    com_ptr<ID2D1RectangleGeometry> full;
                    com_ptr<ID2D1RoundedRectangleGeometry> card;
                    g_gfx->D2D()->CreateRectangleGeometry(RectF(0, 0, (float)Wp, (float)Hp), full.put());
                    g_gfx->D2D()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(l_.card, radius, radius), card.put());
                    if (full && card && SUCCEEDED(g_gfx->D2D()->CreatePathGeometry(outside.put()))) {
                        com_ptr<ID2D1GeometrySink> sink;
                        outside->Open(sink.put());
                        full->CombineWithGeometry(card.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get());
                        sink->Close();
                        brt->CreateLayer(nullptr, layer.put());
                        brt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), outside.get()), layer.get());
                    }
                }
                auto layers = [&](float blur, float dy, float total, int n) {
                    float a = 1 - std::pow(1 - total, 1.f / n);
                    for (int i = n; i >= 1; i--) {
                        float d = blur * i / n;
                        D2D1_RECT_F r = Inflate(l_.card, d, d);
                        r.top += dy;
                        r.bottom += dy;
                        b->SetColor(D2D1_COLOR_F{0, 0, 0, a});
                        brt->FillRoundedRectangle(D2D1::RoundedRect(r, radius + d, radius + d), b.get());
                    }
                };
                layers(S(26), S(8), 0.36f, 26);
                layers(S(4), S(1), 0.22f, 4);
                if (layer) brt->PopLayer();
                if (fillCard) {
                    b->SetColor(Rgba(0x1F1F1F, 1.f));
                    brt->FillRoundedRectangle(D2D1::RoundedRect(l_.card, radius, radius), b.get());
                }
                brt->EndDraw();
                brt->GetBitmap(bg_.put());
                bgW_ = Wp;
                bgH_ = Hp;
                bgGen_ = cv.gen;
                bgFilled_ = fillCard;
            }
        }
        if (bg_) rt->DrawBitmap(bg_.get());
        const Snapshot* e = g_ui.Eff();
        if (e) DrawGlow(rt, *e);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(l_.card, -0.5f, -0.5f), radius - 0.5f, radius - 0.5f),
                                 cv.Brush(Rgba(0xFFFFFF, 0.08f)), 1.f);
    }

    // Soft glow tinted by the cover art.
    void DrawGlow(ID2D1RenderTarget* rt, const Snapshot& e) {
        D2D1_COLOR_F accent = CurrentAccent();
        bool hasArt = e.art != nullptr;
        D2D1_GRADIENT_STOP stops[3] = {
            {0.f, WithAlpha(accent, hasArt ? 0.16f : 0.05f)},
            {0.55f, WithAlpha(accent, hasArt ? 0.05f : 0.015f)},
            {1.f, WithAlpha(accent, 0.f)},
        };
        com_ptr<ID2D1GradientStopCollection> coll;
        if (FAILED(rt->CreateGradientStopCollection(stops, 3, coll.put()))) return;
        com_ptr<ID2D1RadialGradientBrush> glow;
        D2D1_POINT_2F c{(l_.card.left + l_.card.right) / 2, l_.card.top + S(40)};
        if (SUCCEEDED(rt->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(c, D2D1::Point2F(0, 0), l_.W * 0.85f, l_.H * 0.62f), coll.get(),
                glow.put())))
            composited_ ? rt->FillRectangle(l_.card, glow.get())
                        : rt->FillRoundedRectangle(D2D1::RoundedRect(l_.card, CardRadius(), CardRadius()), glow.get());
    }

    void DrawHoverCircle(ID2D1RenderTarget* rt, int part, D2D1_RECT_F r, float alpha = 0.09f) {
        float h = HoverV(part);
        if (h <= 0.01f) return;
        float a = alpha * h * (pressed_ == part ? 0.7f : 1.f);
        D2D1_POINT_2F c = Center(r);
        float rad = std::min(Width(r), Height(r)) / 2 * (0.85f + 0.15f * h);
        rt->FillEllipse(D2D1::Ellipse(c, rad, rad), Cv().Brush(Rgba(0xFFFFFF, a)));
    }

    // Scale for a pressed button (a quick squish).
    float PressScale(int part) const { return downPart_ == part ? 0.9f : 1.f; }

    void DrawCover(ID2D1RenderTarget* rt, const Snapshot& e, IWICBitmapSource* src, float alpha, float scale) {
        float cr = S(8);
        D2D1_RECT_F rc = l_.cover;
        if (scale != 1.f) {
            D2D1_POINT_2F c = Center(rc);
            float hw = Width(rc) / 2 * scale, hh = Height(rc) / 2 * scale;
            rc = RectF(c.x - hw, c.y - hh, c.x + hw, c.y + hh);
        }
        if (src) {
            DrawRoundedBitmap(rt, Cv().Bitmap(src), rc, cr, alpha);
        } else {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rc, cr, cr), Cv().Brush(Rgba(0x2B2B2B, 0.85f * alpha)));
            if (e.isSpotify) DrawSpotifyLogo(Cv(), rt, Center(rc), S(72), 0.9f * alpha);
            else DrawIcon(rt, Glyph::Music, Center(rc), S(64), Cv().Brush(Rgba(0xFFFFFF, 0.35f * alpha)));
        }
    }

    IWICBitmapSource* CoverSource(const Snapshot& e) const {
        if (!e.art) return nullptr;
        UINT want = (UINT)std::lround(Width(l_.cover));
        return e.art->large && e.art->largePx == want ? (IWICBitmapSource*)e.art->large.get()
                                                       : (IWICBitmapSource*)e.art->full.get();
    }

    void DrawContent(ID2D1RenderTarget* rt, const Snapshot& e) {
        Canvas& cv = Cv();
        D2D1_COLOR_F white = Rgba(0xFFFFFF);
        D2D1_COLOR_F accent = CurrentAccent();

        // Header: app icon + name.
        D2D1_RECT_F icon = RectXYWH(l_.header.left, l_.header.top + (Height(l_.header) - S(20)) / 2, S(20), S(20));
        if (e.isSpotify) {
            DrawSpotifyLogo(cv, rt, Center(icon), S(20), 1.f);
        } else if (e.app && e.app->icon) {
            ID2D1Bitmap* bmp = cv.Bitmap(e.app->icon.get());
            if (bmp) rt->DrawBitmap(bmp, icon, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            DrawIcon(rt, Glyph::Music, Center(icon), S(15), cv.Brush(Rgba(0xFFFFFF, 0.8f)));
        }
        std::wstring appName = e.appName.empty() ? L"Media" : e.appName;
        DrawTextLine(rt, appName, g_gfx->Font(false, S(14), DWRITE_FONT_WEIGHT_NORMAL),
                     RectF(icon.right + S(10), l_.header.top, l_.header.right, l_.header.bottom),
                     cv.Brush(Rgba(0xFFFFFF, 0.9f + 0.1f * HoverV(kHeader))));

        // Cover, cross-fading on track changes.
        IWICBitmapSource* cur = CoverSource(e);
        shownArt_.copy_from(cur);
        float t = Clamp01(trackT_.value);
        if (t < 1.f) {
            DrawCover(rt, e, prevArt_.get(), 1.f, 1.f);
            DrawCover(rt, e, cur, t, 0.96f + 0.04f * t);
        } else {
            DrawCover(rt, e, cur, 1.f, 1.f);
        }
        float coverHover = panelOpen_ ? 0.f : HoverV(kCover);
        if (coverHover > 0.01f)
            rt->FillRoundedRectangle(D2D1::RoundedRect(l_.cover, S(8), S(8)), cv.Brush(Rgba(0x000000, 0.14f * coverHover)));

        // Title + artist, sliding in on track changes.
        std::wstring title = e.title.empty() ? L"Unknown title" : e.title;
        shownTitle_ = title;
        shownArtist_ = e.artist;
        IDWriteTextFormat* titleFont = g_gfx->Font(true, S(18), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        IDWriteTextFormat* artistFont = g_gfx->Font(false, S(14), DWRITE_FONT_WEIGHT_NORMAL);
        float artistAlpha = 0.72f + 0.18f * HoverV(kTitle);
        auto drawText = [&](const std::wstring& ti, const std::wstring& ar, float alpha, float dx) {
            D2D1_RECT_F tr = l_.title, ar2 = l_.artist;
            tr.left += dx;
            tr.right += dx;
            ar2.left += dx;
            ar2.right += dx;
            DrawTextLine(rt, ti, titleFont, tr, cv.Brush(Rgba(0xFFFFFF, alpha)), DWRITE_TEXT_ALIGNMENT_CENTER);
            DrawTextLine(rt, ar, artistFont, ar2, cv.Brush(Rgba(0xFFFFFF, artistAlpha * alpha)),
                         DWRITE_TEXT_ALIGNMENT_CENTER);
        };
        if (t < 1.f) {
            float out = Clamp01(t * 1.6f);
            drawText(prevTitle_, prevArtist_, 1.f - out, -S(18) * out);
            drawText(title, e.artist, t, S(18) * (1.f - t));
        } else {
            drawText(title, e.artist, 1.f, 0.f);
        }

        // Like, on the right of the title.
        if (l_.likeShown) {
            DrawHoverCircle(rt, kLike, l_.like);
            bool liked = e.liked == 1;
            float pop = likeT_.value;
            float sc = (1.f + 0.32f * std::sin(pop * 3.14159f)) * PressScale(kLike);
            D2D1_POINT_2F c = Center(l_.like);
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, c) * CurrentTransform(rt));
            DrawIcon(rt, liked ? Glyph::HeartFill : Glyph::Heart, c, S(17),
                     cv.Brush(liked ? accent : Rgba(0xFFFFFF, e.liked < 0 ? 0.55f : 0.8f)));
            RestoreTransform(rt);
        }

        // Progress.
        int64_t dur = e.durMs;
        int64_t pos = e.PosNow();
        float ratio = dur > 0 ? std::clamp((float)pos / (float)dur, 0.f, 1.f) : 0.f;
        if (drag_ == kProgress) ratio = dragValue_;
        float hot = e.canSeek ? std::max(HoverV(kProgress), drag_ == kProgress ? 1.f : 0.f) : 0.f;
        float th = S(4) + S(2) * hot;
        D2D1_POINT_2F tc = Center(l_.track);
        D2D1_RECT_F track = RectF(l_.track.left, tc.y - th / 2, l_.track.right, tc.y + th / 2);
        rt->FillRoundedRectangle(D2D1::RoundedRect(track, th / 2, th / 2),
                                 cv.Brush(Rgba(0xFFFFFF, dur > 0 ? 0.2f : 0.1f)));
        if (dur > 0) {
            D2D1_RECT_F fill = track;
            fill.right = track.left + Width(track) * ratio;
            D2D1_COLOR_F fillCol{white.r + (accent.r - white.r) * hot, white.g + (accent.g - white.g) * hot,
                                 white.b + (accent.b - white.b) * hot, 0.95f};
            if (Width(fill) > 0.5f)
                rt->FillRoundedRectangle(D2D1::RoundedRect(fill, th / 2, th / 2), cv.Brush(fillCol));
            if (hot > 0.01f) {
                float kr = S(6.5f) * hot;
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fill.right, tc.y), kr, kr), cv.Brush(white));
            }
        }
        IDWriteTextFormat* tf = g_gfx->Font(false, S(11.5f), DWRITE_FONT_WEIGHT_NORMAL);
        if (dur > 0 || pos > 0) {
            int64_t shownPos = drag_ == kProgress ? (int64_t)(dragValue_ * dur) : pos;
            DrawTextLine(rt, FormatTime(shownPos), tf, l_.times, cv.Brush(Rgba(0xFFFFFF, 0.62f)));
            if (dur > 0)
                DrawTextLine(rt, FormatTime(dur), tf, l_.times, cv.Brush(Rgba(0xFFFFFF, 0.62f)),
                             DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        // Transport controls.
        auto toggleColor = [&](bool enabled, bool active) {
            if (!enabled) return Rgba(0xFFFFFF, 0.25f);
            return active ? accent : Rgba(0xFFFFFF, 0.8f);
        };
        auto scaled = [&](int part, D2D1_RECT_F r, auto&& draw) {
            float sc = PressScale(part);
            D2D1_POINT_2F c = Center(r);
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, c) * CurrentTransform(rt));
            draw(c);
            RestoreTransform(rt);
        };
        if (e.canShuffle) DrawHoverCircle(rt, kShuffle, l_.shuffle);
        scaled(kShuffle, l_.shuffle, [&](D2D1_POINT_2F c) {
            DrawIcon(rt, Glyph::Shuffle, c, S(16), cv.Brush(toggleColor(e.canShuffle, e.shuffle)));
        });
        if (e.shuffle && e.canShuffle) {
            D2D1_POINT_2F c = Center(l_.shuffle);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + S(14)), S(2), S(2)), cv.Brush(accent));
        }
        if (e.canPrev) DrawHoverCircle(rt, kPrev, l_.prev);
        scaled(kPrev, l_.prev, [&](D2D1_POINT_2F c) {
            DrawTransport(rt, Transport::Prev, c, S(19), cv.Brush(e.canPrev ? white : Rgba(0xFFFFFF, 0.3f)));
        });
        if (e.canPlayPause) DrawHoverCircle(rt, kPlay, l_.play, 0.11f);
        scaled(kPlay, l_.play, [&](D2D1_POINT_2F c) {
            // Morph: pause shrinks/fades out while play grows/fades in (and back).
            float m = Clamp01(playT_.value);
            float base = e.canPlayPause ? 1.f : 0.3f;
            if (m < 0.999f) {
                float s1 = S(24) * (1.f - 0.35f * m);
                DrawTransport(rt, Transport::Pause, c, s1, cv.Brush(Rgba(0xFFFFFF, base * (1.f - m))));
            }
            if (m > 0.001f) {
                float s2 = S(24) * (0.65f + 0.35f * m);
                DrawTransport(rt, Transport::Play, c, s2, cv.Brush(Rgba(0xFFFFFF, base * m)));
            }
        });
        if (e.canNext) DrawHoverCircle(rt, kNext, l_.next);
        scaled(kNext, l_.next, [&](D2D1_POINT_2F c) {
            DrawTransport(rt, Transport::Next, c, S(19), cv.Brush(e.canNext ? white : Rgba(0xFFFFFF, 0.3f)));
        });
        if (e.canRepeat) DrawHoverCircle(rt, kRepeat, l_.repeat);
        bool repeatOn = e.repeat != RepeatMode::Off;
        scaled(kRepeat, l_.repeat, [&](D2D1_POINT_2F c) {
            DrawIcon(rt, e.repeat == RepeatMode::Track ? Glyph::RepeatOne : Glyph::RepeatAll, c, S(16),
                     cv.Brush(toggleColor(e.canRepeat, repeatOn)));
        });
        if (repeatOn && e.canRepeat) {
            D2D1_POINT_2F c = Center(l_.repeat);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + S(14)), S(2), S(2)), cv.Brush(accent));
        }

        // Bottom row.
        if (l_.spotifyRow) {
            if (e.canVolume) {
                int vol = drag_ == kVolume ? (int)std::lround(dragValue_ * 100) : std::max(0, e.volume);
                Glyph vg = vol == 0 ? Glyph::Mute : vol < 34 ? Glyph::Volume1 : vol < 67 ? Glyph::Volume2 : Glyph::Volume3;
                DrawHoverCircle(rt, kVolIcon, Inflate(l_.volIcon, -S(1), -S(1)));
                scaled(kVolIcon, l_.volIcon, [&](D2D1_POINT_2F c) {
                    DrawIcon(rt, vg, c, S(16), cv.Brush(Rgba(0xFFFFFF, 0.8f)));
                });
                float vhot = std::max(HoverV(kVolume), drag_ == kVolume ? 1.f : 0.f);
                float vth = S(4) + S(2) * vhot;
                D2D1_POINT_2F vc = Center(l_.volTrack);
                D2D1_RECT_F vt = RectF(l_.volTrack.left, vc.y - vth / 2, l_.volTrack.right, vc.y + vth / 2);
                rt->FillRoundedRectangle(D2D1::RoundedRect(vt, vth / 2, vth / 2), cv.Brush(Rgba(0xFFFFFF, 0.2f)));
                D2D1_RECT_F vf = vt;
                vf.right = vt.left + Width(vt) * (vol / 100.f);
                D2D1_COLOR_F vcol{white.r + (accent.r - white.r) * vhot, white.g + (accent.g - white.g) * vhot,
                                  white.b + (accent.b - white.b) * vhot, 0.95f};
                if (Width(vf) > 0.5f) rt->FillRoundedRectangle(D2D1::RoundedRect(vf, vth / 2, vth / 2), cv.Brush(vcol));
                if (vhot > 0.01f) {
                    float kr = S(6) * vhot;
                    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(vf.right, vc.y), kr, kr), cv.Brush(white));
                }
            }

            bool remote = !e.deviceName.empty() && !IEquals(e.deviceName, ThisComputerName());
            D2D1_RECT_F dev = l_.devices;
            float dh = std::max(HoverV(kDevices), panelOpen_ ? 1.f : 0.f);
            if (dh > 0.01f)
                rt->FillRoundedRectangle(D2D1::RoundedRect(dev, S(6), S(6)),
                                         cv.Brush(Rgba(0xFFFFFF, (panelOpen_ ? 0.11f : 0.08f) * dh)));
            D2D1_COLOR_F devCol = remote ? accent : Rgba(0xFFFFFF, 0.8f);
            DrawIcon(rt, e.deviceName.empty() ? Glyph::Devices : DeviceGlyph(e.deviceType),
                     D2D1::Point2F(dev.left + S(16), Center(dev).y), S(15), cv.Brush(devCol));
            DrawTextLine(rt, DeviceLabel(e), g_gfx->Font(false, S(12.5f), DWRITE_FONT_WEIGHT_NORMAL),
                         RectF(dev.left + S(30), dev.top, dev.right - S(8), dev.bottom), cv.Brush(devCol));
        } else if (l_.bottom) {
            D2D1_RECT_F c = l_.connect;
            bool pending = e.authPending;
            float h = HoverV(kConnect);
            float a = (pending ? 0.06f : 0.09f) + 0.05f * h;
            float sc = PressScale(kConnect);
            D2D1_POINT_2F cc = Center(c);
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, cc) * CurrentTransform(rt));
            rt->FillRoundedRectangle(D2D1::RoundedRect(c, Height(c) / 2, Height(c) / 2), cv.Brush(Rgba(0xFFFFFF, a)));
            std::wstring label = pending ? L"Reopen Spotify sign-in" : L"Connect Spotify";
            IDWriteTextFormat* f = g_gfx->Font(false, S(13), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            float tw = TextWidth(label, f);
            float total = S(18) + S(8) + tw;
            float x = cc.x - total / 2;
            DrawSpotifyLogo(cv, rt, D2D1::Point2F(x + S(9), cc.y), S(18), pending ? 0.6f : 1.f);
            DrawTextLine(rt, label, f, RectF(x + S(26), c.top, x + S(26) + tw + 2, c.bottom),
                         cv.Brush(Rgba(0xFFFFFF, pending ? 0.6f : 0.95f)));
            RestoreTransform(rt);
        }

        float pv = panelT_.value;
        if (pv > 0.01f) DrawPanel(rt, e, pv);

        // Toast (fades in and out).
        if (Tick() < toastUntil_ + 250 && !toast_.empty()) {
            float in = Clamp01((Tick() - toastStart_) / 180.f);
            float out = Tick() > toastUntil_ ? 1.f - Clamp01((Tick() - toastUntil_) / 250.f) : 1.f;
            float ta = in * out;
            IDWriteTextFormat* f = g_gfx->Font(false, S(12.5f), DWRITE_FONT_WEIGHT_NORMAL);
            float tw = std::min(TextWidth(toast_, f), Width(l_.cover) - S(40));
            float dy = S(8) * (1.f - in);
            D2D1_RECT_F r = RectF(Center(l_.cover).x - tw / 2 - S(14), l_.cover.bottom - S(44) + dy,
                                  Center(l_.cover).x + tw / 2 + S(14), l_.cover.bottom - S(14) + dy);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, Height(r) / 2, Height(r) / 2), cv.Brush(Rgba(0x111111, 0.88f * ta)));
            DrawTextLine(rt, toast_, f, Inflate(r, -S(14), 0), cv.Brush(Rgba(0xFFFFFF, ta)), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // Saves/restores the world transform around per-button scaling.
    D2D1_MATRIX_3X2_F CurrentTransform(ID2D1RenderTarget* rt) {
        rt->GetTransform(&savedTransform_);
        return savedTransform_;
    }
    void RestoreTransform(ID2D1RenderTarget* rt) { rt->SetTransform(savedTransform_); }

    void DrawPanel(ID2D1RenderTarget* rt, const Snapshot& e, float pv) {
        Canvas& cv = Cv();
        D2D1_COLOR_F accent = CurrentAccent();
        float dy = S(12) * (1.f - pv);
        D2D1_RECT_F p = l_.panel;
        p.top += dy;
        p.bottom += dy;
        float cr = S(8);
        rt->FillRoundedRectangle(D2D1::RoundedRect(p, cr, cr), cv.Brush(Rgba(0x202020, 0.94f * pv)));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(p, -0.5f, -0.5f), cr, cr), cv.Brush(Rgba(0xFFFFFF, 0.07f * pv)), 1.f);
        DrawTextLine(rt, L"Connect to a device", g_gfx->Font(false, S(14), DWRITE_FONT_WEIGHT_SEMI_BOLD),
                     RectF(p.left + S(16), p.top + S(10), p.right - S(16), p.top + S(40)), cv.Brush(Rgba(0xFFFFFF, pv)));

        D2D1_RECT_F list = l_.panelList;
        list.top += dy;
        list.bottom += dy;
        rt->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_ALIASED);
        if (e.devices.empty()) {
            std::wstring msg = e.devicesLoading ? L"Looking for devices\x2026" : L"No devices found";
            DrawTextLine(rt, msg, g_gfx->Font(false, S(13), DWRITE_FONT_WEIGHT_NORMAL),
                         RectF(list.left, list.top, list.right, list.top + S(60)), cv.Brush(Rgba(0xFFFFFF, 0.8f * pv)),
                         DWRITE_TEXT_ALIGNMENT_CENTER);
            if (!e.devicesLoading)
                DrawTextLine(rt, L"Open Spotify on a device to see it here.", g_gfx->Font(false, S(12), DWRITE_FONT_WEIGHT_NORMAL),
                             RectF(list.left, list.top + S(44), list.right, list.top + S(68)),
                             cv.Brush(Rgba(0xFFFFFF, 0.55f * pv)), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        for (size_t i = 0; i < e.devices.size(); i++) {
            const DeviceInfo& d = e.devices[i];
            D2D1_RECT_F row = RowRect((int)i);
            row.top += dy;
            row.bottom += dy;
            if (row.bottom < list.top || row.top > list.bottom) continue;
            int part = kPanelRow + (int)i;
            float h = HoverV(part);
            if (h > 0.01f)
                rt->FillRoundedRectangle(D2D1::RoundedRect(Inflate(row, -S(6), -S(2)), S(6), S(6)),
                                         cv.Brush(Rgba(0xFFFFFF, 0.08f * h * pv)));
            D2D1_COLOR_F col = d.active ? accent : Rgba(0xFFFFFF, 0.95f);
            col.a *= pv;
            DrawIcon(rt, DeviceGlyph(d.type), D2D1::Point2F(row.left + S(30), Center(row).y), S(18), cv.Brush(col));
            DrawTextLine(rt, d.name, g_gfx->Font(false, S(13.5f), DWRITE_FONT_WEIGHT_NORMAL),
                         RectF(row.left + S(52), row.top + S(4), row.right - S(16), Center(row).y + S(1)), cv.Brush(col));
            std::wstring sub = d.active ? L"Playing now" : DeviceTypeLabel(d.type);
            if (d.restricted && !d.active) sub += L" \x00B7 Can't be controlled";
            D2D1_COLOR_F subCol = d.active ? WithAlpha(accent, 0.85f * pv) : Rgba(0xFFFFFF, 0.55f * pv);
            DrawTextLine(rt, sub, g_gfx->Font(false, S(11.5f), DWRITE_FONT_WEIGHT_NORMAL),
                         RectF(row.left + S(52), Center(row).y + S(1), row.right - S(16), row.bottom - S(4)), cv.Brush(subCol));
        }
        rt->PopAxisAlignedClip();
        D2D1_RECT_F so = l_.panelSignOut;
        so.top += dy;
        so.bottom += dy;
        DrawTextLine(rt, L"Sign out of Spotify", g_gfx->Font(false, S(12), DWRITE_FONT_WEIGHT_NORMAL), so,
                     cv.Brush(Rgba(0xFFFFFF, (0.55f + 0.35f * HoverV(kPanelSignOut)) * pv)));
    }

    D2D1_RECT_F RowRect(int i) const {
        float top = l_.panelList.top + i * l_.rowH - panelScroll_;
        return RectF(l_.panelList.left, top, l_.panelList.right, top + l_.rowH);
    }

    float MaxPanelScroll(const Snapshot& e) const {
        float content = e.devices.size() * l_.rowH;
        return std::max(0.f, content - Height(l_.panelList));
    }

    // ------------------------------------------------------------------
    // Input
    // ------------------------------------------------------------------

    int HitTest(float x, float y) const {
        const Snapshot* e = g_ui.Eff();
        if (!e) return kNone;
        if (panelOpen_) {
            if (Contains(l_.panel, x, y)) {
                if (Contains(l_.panelSignOut, x, y)) return kPanelSignOut;
                if (Contains(l_.panelList, x, y)) {
                    for (size_t i = 0; i < e->devices.size(); i++)
                        if (Contains(RowRect((int)i), x, y)) return kPanelRow + (int)i;
                }
                return kPanel;
            }
            if (Contains(l_.devices, x, y) && l_.spotifyRow) return kDevices;
            return kNone;
        }
        if (Contains(l_.header, x, y) &&
            x < l_.header.left + S(30) + TextWidth(e->appName, g_gfx->Font(false, S(14), DWRITE_FONT_WEIGHT_NORMAL)))
            return kHeader;
        if (Contains(l_.cover, x, y)) return kCover;
        if (l_.likeShown && Contains(l_.like, x, y)) return kLike;
        if (Contains(l_.title, x, y) || Contains(l_.artist, x, y)) return kTitle;
        if (Contains(l_.progress, x, y) && e->canSeek) return kProgress;
        auto inCircle = [&](const D2D1_RECT_F& r) { return Contains(Inflate(r, S(2), S(2)), x, y); };
        if (inCircle(l_.shuffle)) return kShuffle;
        if (inCircle(l_.prev)) return kPrev;
        if (inCircle(l_.play)) return kPlay;
        if (inCircle(l_.next)) return kNext;
        if (inCircle(l_.repeat)) return kRepeat;
        if (l_.spotifyRow) {
            if (Contains(l_.devices, x, y)) return kDevices;
            if (e->canVolume) {
                if (Contains(l_.volIcon, x, y)) return kVolIcon;
                if (Contains(l_.volume, x, y)) return kVolume;
            }
        } else if (l_.bottom && Contains(l_.connect, x, y)) {
            return kConnect;
        }
        return kNone;
    }

    float RatioIn(const D2D1_RECT_F& track, float x) const {
        return Clamp01((x - track.left) / std::max(1.f, Width(track)));
    }

    void OpenSource() {
        const Snapshot* e = g_ui.Eff();
        if (!e) return;
        if (e->isSpotify) OpenSpotifyUri(e->trackUri);
        else ActivateApp(e->aumid);
        Close(L"opened the app");
    }

    void SetVolume(int v, bool sendNow) {
        v = std::clamp(v, 0, 100);
        g_ui.OverrideVolume(v);
        pendingVolume_ = v;
        if (sendNow || Tick() - lastVolumeSend_ > 200) {
            SendAct(Act::Volume, v);
            lastVolumeSend_ = Tick();
            pendingVolume_ = -1;
        } else {
            SetTimer(hwnd_, kTimerVolume, 220, nullptr);
        }
    }

    void Click(int part) {
        const Snapshot* e = g_ui.Eff();
        Trace(L"Popup click: part=%d", part);
        if (!e) return;
        switch (part) {
            case kHeader:
            case kCover:
            case kTitle:
                OpenSource();
                return;
            case kShuffle:
                if (e->canShuffle) {
                    bool want = !e->shuffle;
                    g_ui.OverrideShuffle(want);
                    SendAct(Act::Shuffle, want ? 1 : 0);
                }
                break;
            case kRepeat:
                if (e->canRepeat) {
                    RepeatMode next = e->repeat == RepeatMode::Off       ? RepeatMode::Context
                                      : e->repeat == RepeatMode::Context ? RepeatMode::Track
                                                                         : RepeatMode::Off;
                    g_ui.OverrideRepeat(next);
                    SendAct(Act::Repeat, (int)next);
                }
                break;
            case kPrev:
                UiPrev();
                break;
            case kPlay:
                UiTogglePlay();
                break;
            case kNext:
                UiNext();
                break;
            case kLike:
                if (e->canLike) {
                    int want = e->liked == 1 ? 0 : 1;
                    g_ui.OverrideLiked(want);
                    SendAct(Act::Like, want);
                    if (want) {
                        likeT_.Snap(0);
                        likeT_.To(1, Dur(380));
                    }
                }
                break;
            case kDevices:
                panelOpen_ = !panelOpen_;
                panelScroll_ = 0;
                panelT_.To(panelOpen_ ? 1.f : 0.f, Dur(200));
                if (panelOpen_) SendAct(Act::DevicesOpened);
                break;
            case kVolIcon:
                if (e->canVolume) {
                    if (e->volume > 0) {
                        volumeBeforeMute_ = e->volume;
                        SetVolume(0, true);
                    } else {
                        SetVolume(volumeBeforeMute_ > 0 ? volumeBeforeMute_ : 50, true);
                    }
                }
                break;
            case kConnect:
                SendAct(Act::Connect);
                break;
            case kPanelSignOut:
                ClosePanel();
                SendAct(Act::SignOut);
                break;
            default:
                if (part >= kPanelRow) {
                    size_t i = part - kPanelRow;
                    if (i < e->devices.size()) {
                        const DeviceInfo& d = e->devices[i];
                        if (!d.active) {
                            g_ui.OverrideDevice(d.id);
                            SendAct(Act::Transfer, e->playing ? 1 : 0, d.id);
                        }
                        ClosePanel();
                    }
                }
                break;
        }
        // Reflect optimistic changes right away (play/pause morph etc.).
        if (const Snapshot* n = g_ui.Eff()) playT_.To(n->playing ? 0.f : 1.f, Dur(220));
        Kick();
    }

    void ClosePanel() {
        panelOpen_ = false;
        panelT_.To(0, Dur(160));
    }

    // Completes a button press: clicks if the pointer is still on the same part.
    void FinishPress(int partNow) {
        KillTimer(hwnd_, kTimerPress);
        int part = downPart_;
        downPart_ = kNone;
        pressed_ = kNone;
        if (part != kNone && part == partNow) Click(part);
        else Kick();
    }

    void OnAnimTimer() {
        Render();
        if (closing_ && openT_.dur <= 0) {
            KillTimer(hwnd_, kTimerAnim);
            KillTimer(hwnd_, kTimerTick);
            ShowWindow(hwnd_, SW_HIDE);
            open_ = false;
            closing_ = false;
            if (!composited_) {
                layered_.Release();
                bg_ = nullptr;
            }
            prevArt_ = nullptr;
            shownArt_ = nullptr;
            return;
        }
        if (!Animating()) KillTimer(hwnd_, kTimerAnim);
    }

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_ACTIVATE:
                Trace(L"Popup WM_ACTIVATE %d, other window: %s", LOWORD(wp), WindowDesc((HWND)lp).c_str());
                if (LOWORD(wp) == WA_INACTIVE) {
                    if (open_ && !closing_) Close(L"deactivated");
                } else {
                    activatedOnce_ = true;
                }
                return 0;
            case WM_MOUSEACTIVATE:
                return MA_ACTIVATE;
            case WM_NCHITTEST:
                return HTCLIENT;
            case WM_SETCURSOR: {
                bool hand = hover_ == kCover || hover_ == kTitle || hover_ == kHeader;
                SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            case WM_MOUSEMOVE: {
                if (!tracking_) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                    TrackMouseEvent(&tme);
                    tracking_ = true;
                }
                float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
                if (drag_ == kProgress) {
                    dragValue_ = RatioIn(l_.track, x);
                    Render();
                    return 0;
                }
                if (drag_ == kVolume) {
                    dragValue_ = RatioIn(l_.volTrack, x);
                    SetVolume((int)std::lround(dragValue_ * 100), false);
                    Render();
                    return 0;
                }
                SetHover(HitTest(x, y));
                return 0;
            }
            case WM_MOUSELEAVE:
                tracking_ = false;
                if (drag_ == kNone) SetHover(kNone);
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK: {
                float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
                int h = HitTest(x, y);
                Trace(L"Popup mouse down at (%.0f,%.0f) -> part %d", x, y, h);
                const Snapshot* e = g_ui.Eff();
                if (panelOpen_ && h == kNone) {
                    ClosePanel();
                    Kick();
                    return 0;
                }
                if (!Contains(l_.card, x, y)) {
                    Close(L"click outside the card");
                    return 0;
                }
                if (h == kProgress && e && e->canSeek) {
                    // Sliders need mouse capture to follow the drag.
                    SetCapture(hwnd_);
                    drag_ = kProgress;
                    dragValue_ = RatioIn(l_.track, x);
                } else if (h == kVolume && e && e->canVolume) {
                    SetCapture(hwnd_);
                    drag_ = kVolume;
                    dragValue_ = RatioIn(l_.volTrack, x);
                    SetVolume((int)std::lround(dragValue_ * 100), false);
                } else if (h != kNone) {
                    // Buttons don't take capture: something else in explorer can
                    // steal it and swallow the button-up. A timer also watches the
                    // physical button so the click happens even then.
                    pressed_ = downPart_ = h;
                    SetTimer(hwnd_, kTimerPress, 30, nullptr);
                }
                Kick();
                return 0;
            }
            case WM_LBUTTONUP: {
                float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
                Trace(L"Popup mouse up at (%.0f,%.0f) -> part %d (down %d, drag %d)", x, y, HitTest(x, y), downPart_,
                      drag_);
                int drag = drag_;
                drag_ = kNone;  // before ReleaseCapture, so WM_CAPTURECHANGED doesn't commit twice
                if (GetCapture() == hwnd_) ReleaseCapture();
                const Snapshot* e = g_ui.Eff();
                if (drag == kProgress) {
                    if (e && e->durMs > 0) {
                        int64_t ms = (int64_t)(RatioIn(l_.track, x) * e->durMs);
                        g_ui.OverrideSeek(ms);
                        SendAct(Act::Seek, ms);
                    }
                    SetHover(HitTest(x, y));
                    Kick();
                    return 0;
                }
                if (drag == kVolume) {
                    SetVolume((int)std::lround(RatioIn(l_.volTrack, x) * 100), true);
                    SetHover(HitTest(x, y));
                    Kick();
                    return 0;
                }
                FinishPress(HitTest(x, y));
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (drag_ != kNone) {
                    // Capture was taken away mid-drag: keep the last value.
                    Trace(L"Popup lost mouse capture during a drag");
                    int drag = drag_;
                    drag_ = kNone;
                    const Snapshot* e = g_ui.Eff();
                    if (drag == kProgress && e && e->durMs > 0) {
                        int64_t ms = (int64_t)(dragValue_ * e->durMs);
                        g_ui.OverrideSeek(ms);
                        SendAct(Act::Seek, ms);
                    } else if (drag == kVolume) {
                        SetVolume((int)std::lround(dragValue_ * 100), true);
                    }
                    Kick();
                }
                return 0;
            case WM_MOUSEWHEEL: {
                const Snapshot* e = g_ui.Eff();
                if (!e) return 0;
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                if (panelOpen_) {
                    panelScroll_ = std::clamp(panelScroll_ - delta / 120.f * l_.rowH, 0.f, MaxPanelScroll(*e));
                } else if (e->canVolume) {
                    SetVolume(std::max(0, e->volume) + (delta > 0 ? 5 : -5), false);
                }
                Kick();
                return 0;
            }
            case WM_KEYDOWN: {
                const Snapshot* e = g_ui.Eff();
                switch (wp) {
                    case VK_ESCAPE:
                        if (panelOpen_) ClosePanel();
                        else Close(L"Esc");
                        Kick();
                        return 0;
                    case VK_SPACE:
                        UiTogglePlay();
                        break;
                    case VK_LEFT:
                    case VK_RIGHT:
                        if (e && e->canSeek && e->durMs > 0) {
                            int64_t ms = std::clamp<int64_t>(e->PosNow() + (wp == VK_LEFT ? -10000 : 10000), 0, e->durMs);
                            g_ui.OverrideSeek(ms);
                            SendAct(Act::Seek, ms);
                        }
                        break;
                    case VK_UP:
                    case VK_DOWN:
                        if (e && e->canVolume) SetVolume(std::max(0, e->volume) + (wp == VK_UP ? 5 : -5), false);
                        break;
                }
                if (const Snapshot* n = g_ui.Eff()) playT_.To(n->playing ? 0.f : 1.f, Dur(220));
                Kick();
                return 0;
            }
            case WM_TIMER:
                if (wp == kTimerAnim) {
                    OnAnimTimer();
                } else if (wp == kTimerTick) {
                    g_ui.Refresh();
                    HWND fg = GetForegroundWindow();
                    if (open_ && !closing_ && fg != hwnd_ && !activatedOnce_ && fg != fgAtOpen_) {
                        Trace(L"Foreground moved to %s", WindowDesc(fg).c_str());
                        Close(L"foreground changed");
                        return 0;
                    }
                    if (!Animating()) Render();
                } else if (wp == kTimerVolume) {
                    KillTimer(hwnd_, kTimerVolume);
                    if (pendingVolume_ >= 0) SetVolume(pendingVolume_, true);
                } else if (wp == kTimerPress) {
                    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
                    if (downPart_ == kNone) {
                        KillTimer(hwnd_, kTimerPress);
                    } else if (!(GetAsyncKeyState(vk) & 0x8000)) {
                        POINT pt;
                        GetCursorPos(&pt);
                        ScreenToClient(hwnd_, &pt);
                        Trace(L"Button released without a mouse-up message (part %d)", downPart_);
                        FinishPress(HitTest((float)pt.x, (float)pt.y));
                    }
                }
                return 0;
            case WM_DPICHANGED:
                Relayout();
                return 0;
            case WM_NCDESTROY: {
                HWND h = hwnd_;
                KillTimer(h, kTimerAnim);
                KillTimer(h, kTimerTick);
                KillTimer(h, kTimerVolume);
                KillTimer(h, kTimerPress);
                comp_.Release();
                wc_.Release();
                layered_.Release();
                bg_ = nullptr;
                prevArt_ = nullptr;
                shownArt_ = nullptr;
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                open_ = false;
                if (g_popupOpen) {
                    g_popupOpen = false;
                    SendAct(Act::PopupClosed);
                }
                return DefWindowProcW(h, msg, wp, lp);
            }
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    HWND hwnd_ = nullptr, taskbar_ = nullptr;
    bool composited_ = false;  // DirectComposition + DWM blur
    bool wincomp_ = false;     // Windows.UI.Composition + host backdrop
    bool classic_ = false;     // the "Classic" engine was requested
    WinCompCanvas wc_;
    bool bgFilled_ = false;
    CompositionCanvas comp_;
    Surface layered_;
    com_ptr<ID2D1Bitmap> bg_;
    int bgW_ = 0, bgH_ = 0;
    uint32_t bgGen_ = 0;
    RECT anchor_{};
    RECT lastRect_{INT_MIN, INT_MIN, INT_MIN, INT_MIN};
    bool open_ = false, closing_ = false;
    ULONGLONG lastCloseTick_ = 0;
    bool activatedOnce_ = false;
    HWND fgAtOpen_ = nullptr;
    int hover_ = kNone, pressed_ = kNone, drag_ = kNone, downPart_ = kNone;
    float dragValue_ = 0;
    bool tracking_ = false;
    bool panelOpen_ = false;
    float panelScroll_ = 0;
    uint32_t lastToastSeq_ = 0;
    std::wstring toast_;
    ULONGLONG toastUntil_ = 0, toastStart_ = 0;
    int pendingVolume_ = -1;
    ULONGLONG lastVolumeSend_ = 0;
    int volumeBeforeMute_ = 50;

    // Animations.
    Tween openT_, trackT_, playT_, likeT_, accentT_, panelT_, pressT_;
    std::map<int, Tween> hoverT_;
    D2D1_COLOR_F accentFrom_{1, 1, 1, 1}, accentTo_{1, 1, 1, 1};
    std::wstring curKey_, shownTitle_, shownArtist_, prevTitle_, prevArtist_;
    com_ptr<IWICBitmapSource> shownArt_, prevArt_;
    D2D1_MATRIX_3X2_F savedTransform_ = D2D1::Matrix3x2F::Identity();

#ifdef TP_HARNESS
   public:
    // Renders the popup without showing it (test harness only). Uses the
    // layered canvas so the pixels can be saved.
    void DebugRender(const RECT& anchor, int hover, bool panel, const std::wstring& toast) {
        bool wasComposited = composited_, wasWinComp = wincomp_;
        wincomp_ = false;
        composited_ = false;
        anchor_ = anchor;
        open_ = true;
        closing_ = false;
        openT_.Snap(1);
        trackT_.Snap(1);
        accentT_.Snap(1);
        if (const Snapshot* e = g_ui.Eff()) {
            accentFrom_ = accentTo_ = TargetAccent(*e);
            playT_.Snap(e->playing ? 0.f : 1.f);
            curKey_ = e->trackKey;
        }
        hoverT_.clear();
        hover_ = hover;
        if (hover != kNone) hoverT_[hover].Snap(1);
        panelOpen_ = panel;
        panelT_.Snap(panel ? 1.f : 0.f);
        toast_ = toast;
        toastUntil_ = toast.empty() ? 0 : Tick() + 100000;
        toastStart_ = Tick() - 1000;
        Layout();
        Render();
        composited_ = wasComposited;
        wincomp_ = wasWinComp;
        bg_ = nullptr;
    }
    const Surface& DebugSurface() const { return layered_; }
    POINT DebugConnectCenter() const {
        D2D1_POINT_2F c = Center(l_.connect);
        return POINT{(LONG)c.x, (LONG)c.y};
    }
    bool DebugComposited() const { return composited_ || wincomp_; }
    bool DebugWinComp() const { return wincomp_; }
    // Renders a few frames through the GPU path without showing the window.
    bool DebugRenderComposited(const RECT& anchor) {
        if (!composited_ && !wincomp_) return false;
        bg_ = nullptr;
        anchor_ = anchor;
        open_ = true;
        closing_ = false;
        openT_.Snap(0);
        openT_.To(1, 200);
        bool ok = true;
        for (int i = 0; i < 20 && ok; i++) {
            Layout();
            SIZE sz = Wnd();
            ok = wincomp_ ? wc_.Resize(sz.cx, sz.cy) : comp_.Resize(sz.cx, sz.cy);
            if (ok) Render();
            Sleep(16);
        }
        open_ = false;
        return ok && (wincomp_ ? wc_.Ready() : comp_.Ready());
    }
#endif
};

static Popup* g_popup = nullptr;

static void PopupToggle(const RECT& anchor) {
    if (g_popup) g_popup->Toggle(anchor);
}
static void PopupOnSnapshot() {
    if (g_popup) g_popup->OnSnapshot();
}
static void PopupRelayout() {
    if (g_popup) g_popup->Relayout();
}
static void PopupApplySettings() {
    if (g_popup) {
        g_popup->ApplyBackdropSettings();
        g_popup->Relayout();
    }
}

}  // namespace tp

namespace tp {

// ============================================================================
// Taskbar integration
// ============================================================================

static UINT g_msgAttach = 0, g_msgDetach = 0, g_msgSettings = 0;
static Widget* g_widget = nullptr;
static std::atomic<bool> g_attached{false};
static bool g_controllerStarted = false;  // core thread
static int g_attachAttempts = 0;          // core thread

#ifdef TP_HARNESS
static const wchar_t* kTaskbarClass = L"TP_FakeTaskbar";
#else
static const wchar_t* kTaskbarClass = L"Shell_TrayWnd";
#endif

static void RegisterClasses() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = ModuleInstance();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = Widget::Proc;
    wc.lpszClassName = Widget::kClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = Popup::Proc;
    wc.lpszClassName = Popup::kClass;
    RegisterClassExW(&wc);
    // The GPU popup gets the system's rounded drop shadow.
    wc.style = CS_DROPSHADOW;
    wc.lpszClassName = Popup::kClassComposited;
    RegisterClassExW(&wc);
}

static void UnregisterClasses() {
    UnregisterClassW(Widget::kClass, ModuleInstance());
    UnregisterClassW(Popup::kClass, ModuleInstance());
    UnregisterClassW(Popup::kClassComposited, ModuleInstance());
}

static void CreateWidget(HWND taskbar) {
    g_widget = new Widget();
    if (!g_widget->Create(taskbar)) {
        delete g_widget;
        g_widget = nullptr;
        return;
    }
    g_uiHwnd = g_widget->Hwnd();
    g_ui.SetSnapshot(LatestSnapshot());
    g_widget->OnSnapshot();
}

static void DestroyWidget() {
    g_uiHwnd = nullptr;
    if (g_widget) {
        g_widget->Destroy();
        delete g_widget;
        g_widget = nullptr;
    }
    g_widgetVisible = false;
}

// Runs on the taskbar thread.
static void UiInit(HWND taskbar) {
    if (g_gfx) return;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    g_gfx = new Gfx();
    if (!g_gfx->Init()) {
        Wh_Log(L"Direct2D initialization failed");
        delete g_gfx;
        g_gfx = nullptr;
        return;
    }
    RegisterClasses();
    g_popup = new Popup();
    if (!g_popup->Create(taskbar)) {
        delete g_popup;
        g_popup = nullptr;
    }
    CreateWidget(taskbar);
    Wh_Log(L"UI created");
}

// Runs on the taskbar thread.
static void UiShutdown() {
    if (g_popup) {
        g_popup->Destroy();
        delete g_popup;
        g_popup = nullptr;
    }
    DestroyWidget();
    g_ui.SetSnapshot(nullptr);
    if (g_gfx) {
        UnregisterClasses();
        delete g_gfx;
        g_gfx = nullptr;
    }
}

// Runs on the taskbar thread.
static void UiApplySettings(HWND taskbar) {
    if (!g_gfx) return;
    if (g_widget && g_widget->Overlay() != GetSettings()->overlayMode) {
        DestroyWidget();
        CreateWidget(taskbar);
    } else if (g_widget) {
        g_widget->OnSettingsChanged();
    }
    if (g_popup && g_popup->CreatedClassic() != GetSettings()->popupClassic) {
        // The popup engine changed: rebuild the popup window.
        g_popup->Destroy();
        delete g_popup;
        g_popup = new Popup();
        if (!g_popup->Create(taskbar)) {
            delete g_popup;
            g_popup = nullptr;
        }
    }
    PopupApplySettings();
}

static void TryAttach();

static LRESULT CALLBACK TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, DWORD_PTR) {
    if (msg == g_msgAttach && msg) {
        UiInit(hwnd);
        return 0;
    }
    if (msg == g_msgDetach && msg) {
        UiShutdown();
        return 0;
    }
    if (msg == g_msgSettings && msg) {
        UiApplySettings(hwnd);
        return 0;
    }
    switch (msg) {
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (g_widget) PostMessageW(g_widget->Hwnd(), WM_TP_SETTINGS, 0, 0);
            break;
        case WM_WINDOWPOSCHANGED:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (g_widget) PostMessageW(g_widget->Hwnd(), WM_TP_RELAYOUT, 0, 0);
            break;
        case WM_NCDESTROY:
            // The taskbar is going away (e.g. explorer restarting it).
            UiShutdown();
            g_taskbarHwnd = nullptr;
            g_attached = false;
            g_core.Post([] { TryAttach(); }, 2000);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND FindOwnTaskbar() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, kTaskbarClass, nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return h;
    }
    return nullptr;
}

// Runs on the core thread until the taskbar of this explorer process is found.
static void TryAttach() {
    if (g_shuttingDown || g_attached) return;
    HWND tb = FindOwnTaskbar();
    if (tb && WindhawkUtils::SetWindowSubclassFromAnyThread(tb, TaskbarSubclassProc, 0)) {
        g_taskbarHwnd = tb;
        g_attached = true;
        g_attachAttempts = 0;
        Trace(L"Attached to the taskbar");
        // Only the explorer process that owns the taskbar runs the rest of the
        // mod. Helper explorer.exe processes stay at a single idle thread.
        Http::Init();
        g_spot.Start();
        g_cmd.Start();
        g_img.Start();
        if (!g_controllerStarted) {
            g_controllerStarted = true;
            g_controller.Init();
        }
        SendMessageW(tb, g_msgAttach, 0, 0);
        g_controller.Publish();
        return;
    }
    g_attachAttempts++;
    g_core.Post([] { TryAttach(); }, g_attachAttempts < 60 ? 1000 : 5000);
}

static void ModInit() {
    LoadSettings();
    InitTrace();
    Trace(L"Mod init (pid %u, clientIdSet=%d)", GetCurrentProcessId(), !GetSettings()->clientId.empty());
    g_shuttingDown = false;
    g_authCancel = false;
    g_msgAttach = RegisterWindowMessageW(L"TaskbarPlayer_Attach");
    g_msgDetach = RegisterWindowMessageW(L"TaskbarPlayer_Detach");
    g_msgSettings = RegisterWindowMessageW(L"TaskbarPlayer_Settings");
    g_core.Start();
    g_core.Post([] { TryAttach(); });
}

static void ModUninit() {
    g_shuttingDown = true;
    g_authCancel = true;
    HWND tb = g_taskbarHwnd;
    if (tb && g_attached) {
        SendMessageW(tb, g_msgDetach, 0, 0);
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(tb, TaskbarSubclassProc);
    }
    g_attached = false;
    g_taskbarHwnd = nullptr;
    Http::CancelAll();
    g_core.Sync([] { g_controller.Shutdown(); });
    g_img.Sync([] { ReleaseImgThreadObjects(); });
    g_spot.Stop();
    g_cmd.Stop();
    g_img.Stop();
    g_core.Stop();
    if (g_authThread.joinable()) g_authThread.join();
    Http::Cleanup();
    std::lock_guard<std::mutex> lock(g_wicMutex);
    g_wic = nullptr;
}

static void ModSettingsChanged() {
    LoadSettings();
    g_core.Post([] { g_controller.OnSettingsChanged(); });
    HWND tb = g_taskbarHwnd;
    if (tb && g_attached) SendMessageW(tb, g_msgSettings, 0, 0);
}

}  // namespace tp

BOOL Wh_ModInit() {
    Wh_Log(L"Taskbar Player starting");
    tp::ModInit();
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"Taskbar Player stopping");
    tp::ModUninit();
}

void Wh_ModSettingsChanged() {
    tp::ModSettingsChanged();
}
