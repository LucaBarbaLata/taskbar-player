// ==WindhawkMod==
// @id              taskbar-player
// @name            Taskbar Player
// @description     A compact now-playing widget on the left side of the taskbar with a popup player and Spotify Connect support
// @version         1.2.0
// @author          lucariki
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
* **Scroll to change the volume**: scroll the mouse wheel over the widget.
  For Spotify it changes Spotify's own volume (also on your phone or a
  speaker); for other apps it changes their slider in the Windows Volume
  Mixer. Hold Shift for 1% steps. Can also change the Windows master volume
  instead.
* **Audio visualizer** next to the playback buttons while sound plays on this
  PC: bars, mirrored bars, a smooth line, LED dots or a wave, with many
  options (size, colors, speed, sensitivity, ...) in the settings. It only
  listens to what the PC plays (like screen recorders do); nothing is
  recorded or saved.

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
* Scrolling over the widget needs the Windows setting "Scroll inactive
  windows when I hover over them" (on by default).
* The visualizer's "Only the app shown on the widget" source needs Windows 11
  (or Windows 10 21H2+). With "Everything playing on this PC" it also reacts
  to other sounds, unless "Only while the shown track plays on this PC" is on.
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
- scrollVolume:
  - enabled: true
    $name: Scroll on the widget to change the volume
  - target: app
    $name: What scrolling changes
    $options:
    - app: The playing app (Spotify's volume, or the app's Volume Mixer slider)
    - system: Windows master volume
  - step: 5
    $name: Step per scroll notch (%)
    $description: Hold Shift while scrolling for 1% steps.
  - invert: false
    $name: Reverse scroll direction
  - indicator: true
    $name: Show the volume on the widget while scrolling
  $name: Scroll volume
- visualizer:
  - enabled: true
    $name: Show an audio visualizer
    $description: >-
      Moves with the sound playing on this PC. It only listens to what the PC
      plays; nothing is recorded or saved.
  - style: bars
    $name: Style
    $options:
    - bars: Bars
    - mirrored: Mirrored bars
    - line: Smooth line
    - dots: Dots (LED meter)
    - wave: Wave (oscilloscope)
  - position: afterControls
    $name: Position
    $options:
    - afterControls: Right of the playback buttons
    - beforeControls: Left of the playback buttons
    - beforeCover: Left of the cover
  - width: 32
    $name: Width
    $description: In pixels at 100% scaling (8-200).
  - height: 20
    $name: Height
    $description: In pixels at 100% scaling (4-48, at most the taskbar height).
  - bars: 6
    $name: Number of bars
    $description: Also the number of points of the smooth line (2-48).
  - gap: 2
    $name: Gap between bars
    $description: In pixels at 100% scaling.
  - roundness: 100
    $name: Roundness (%)
    $description: 0 gives square bars and dots, 100 fully rounded ones.
  - peaks: false
    $name: Falling peak markers
  - mirror: false
    $name: Bass on the right
  - colorMode: accent
    $name: Color
    $options:
    - accent: From the album cover
    - text: Taskbar text color
    - custom: Custom color
    - gradient: Gradient (custom color to second color)
    - rainbow: Rainbow
  - color: "FF1ED760"
    $name: Custom color
    $description: >-
      RRGGBB or AARRGGBB. Used by "Custom color", and as the start of the
      gradient (at the base of the bars).
  - color2: "FFB15CFF"
    $name: Second gradient color
    $description: RRGGBB or AARRGGBB. The end of the gradient (at the tips of the bars).
  - opacity: 100
    $name: Opacity (%)
  - sensitivity: 100
    $name: Sensitivity (%)
    $description: Higher makes the bars taller (10-400).
  - autoLevel: true
    $name: Auto level
    $description: Adapts to the loudness, so quiet and loud music look alike.
  - riseSpeed: 80
    $name: Rise speed (1-100)
  - fallSpeed: 35
    $name: Fall speed (1-100)
  - frameRate: 60
    $name: Frames per second
    $description: 10-120. Lower uses less CPU.
  - minFrequency: 50
    $name: Lowest frequency (Hz)
  - maxFrequency: 10000
    $name: Highest frequency (Hz)
  - source: all
    $name: Audio source
    $options:
    - all: Everything playing on this PC
    - app: Only the app shown on the widget
  - onlyWhenPlaying: true
    $name: Only while the shown track plays on this PC
    $description: >-
      Ignores other sounds (notifications, videos) while the widget shows
      paused music or music playing on another device.
  - whenSilent: shrink
    $name: When it's silent
    $options:
    - shrink: Fade out and make the widget narrower
    - keep: Fade out but keep the space
    - rest: Keep showing flat bars
  - silenceDelay: 2
    $name: Seconds of silence before it hides
  $name: Audio visualizer
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
#include <appmodel.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
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

    struct Scroll {
        bool enabled = true;
        bool system = false;  // Windows master volume instead of the playing app
        int step = 5;         // percent per wheel notch
        bool invert = false;
        bool indicator = true;
    } scroll;

    struct Viz {
        bool enabled = true;
        enum class Style { Bars, Mirrored, Line, Dots, Wave } style = Style::Bars;
        enum class Pos { BeforeControls, AfterControls, BeforeCover } position = Pos::AfterControls;
        int width = 32, height = 20;  // px at 100%
        int bars = 6, gap = 2;
        int roundness = 100;  // percent
        bool peaks = false, mirror = false;
        enum class Color { Accent, Text, Custom, Gradient, Rainbow } colorMode = Color::Accent;
        uint32_t color = 0xFF1ED760, color2 = 0xFFB15CFF;  // AARRGGBB
        int opacity = 100, sensitivity = 100;
        bool autoLevel = true;
        int riseSpeed = 80, fallSpeed = 35;
        int frameRate = 60;
        int minHz = 50, maxHz = 10000;
        bool appOnly = false;  // capture only the app shown on the widget
        bool onlyWhenPlaying = true;
        enum class Silent { Shrink, Keep, Rest } whenSilent = Silent::Shrink;
        int silenceDelayMs = 2000;
    } viz;
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

static std::wstring StringSetting(const wchar_t* name) {
    PCWSTR v = Wh_GetStringSetting(name);
    std::wstring s = v ? v : L"";
    Wh_FreeStringSetting(v);
    return s;
}

static int IntSetting(const wchar_t* name, int lo, int hi) {
    return std::clamp(Wh_GetIntSetting(name), lo, hi);
}

static void LoadVisualizerSettings(Settings::Viz& v) {
    using V = Settings::Viz;
    v.enabled = Wh_GetIntSetting(L"visualizer.enabled") != 0;
    std::wstring style = StringSetting(L"visualizer.style");
    v.style = style == L"mirrored" ? V::Style::Mirrored
              : style == L"line"   ? V::Style::Line
              : style == L"dots"   ? V::Style::Dots
              : style == L"wave"   ? V::Style::Wave
                                   : V::Style::Bars;
    std::wstring pos = StringSetting(L"visualizer.position");
    v.position = pos == L"beforeControls" ? V::Pos::BeforeControls
                 : pos == L"beforeCover"  ? V::Pos::BeforeCover
                                          : V::Pos::AfterControls;
    v.width = IntSetting(L"visualizer.width", 8, 200);
    v.height = IntSetting(L"visualizer.height", 4, 48);
    v.bars = IntSetting(L"visualizer.bars", 2, 48);
    v.gap = IntSetting(L"visualizer.gap", 0, 20);
    v.roundness = IntSetting(L"visualizer.roundness", 0, 100);
    v.peaks = Wh_GetIntSetting(L"visualizer.peaks") != 0;
    v.mirror = Wh_GetIntSetting(L"visualizer.mirror") != 0;
    std::wstring color = StringSetting(L"visualizer.colorMode");
    v.colorMode = color == L"text"       ? V::Color::Text
                  : color == L"custom"   ? V::Color::Custom
                  : color == L"gradient" ? V::Color::Gradient
                  : color == L"rainbow"  ? V::Color::Rainbow
                                         : V::Color::Accent;
    if (!ParseArgb(StringSetting(L"visualizer.color"), v.color)) v.color = 0xFF1ED760;
    if (!ParseArgb(StringSetting(L"visualizer.color2"), v.color2)) v.color2 = 0xFFB15CFF;
    v.opacity = IntSetting(L"visualizer.opacity", 0, 100);
    v.sensitivity = IntSetting(L"visualizer.sensitivity", 10, 400);
    v.autoLevel = Wh_GetIntSetting(L"visualizer.autoLevel") != 0;
    v.riseSpeed = IntSetting(L"visualizer.riseSpeed", 1, 100);
    v.fallSpeed = IntSetting(L"visualizer.fallSpeed", 1, 100);
    v.frameRate = IntSetting(L"visualizer.frameRate", 10, 120);
    v.minHz = IntSetting(L"visualizer.minFrequency", 20, 15000);
    v.maxHz = IntSetting(L"visualizer.maxFrequency", 100, 22000);
    if (v.maxHz < v.minHz * 2) v.maxHz = std::min(22000, v.minHz * 2);
    v.appOnly = StringSetting(L"visualizer.source") == L"app";
    v.onlyWhenPlaying = Wh_GetIntSetting(L"visualizer.onlyWhenPlaying") != 0;
    std::wstring silent = StringSetting(L"visualizer.whenSilent");
    v.whenSilent = silent == L"keep"   ? V::Silent::Keep
                   : silent == L"rest" ? V::Silent::Rest
                                       : V::Silent::Shrink;
    v.silenceDelayMs = IntSetting(L"visualizer.silenceDelay", 0, 600) * 1000;
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
    s->scroll.enabled = Wh_GetIntSetting(L"scrollVolume.enabled") != 0;
    s->scroll.system = StringSetting(L"scrollVolume.target") == L"system";
    s->scroll.step = IntSetting(L"scrollVolume.step", 1, 25);
    s->scroll.invert = Wh_GetIntSetting(L"scrollVolume.invert") != 0;
    s->scroll.indicator = Wh_GetIntSetting(L"scrollVolume.indicator") != 0;
    LoadVisualizerSettings(s->viz);
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = s;
}

// The name Spotify gives this PC as a Connect device.
static std::wstring ThisComputerName() {
    static std::wstring& name = *new std::wstring([] {
        wchar_t buf[256];
        DWORD len = ARRAYSIZE(buf);
        if (GetComputerNameExW(ComputerNameDnsHostname, buf, &len)) return std::wstring(buf, len);
        len = ARRAYSIZE(buf);
        if (GetComputerNameW(buf, &len)) return std::wstring(buf, len);
        return std::wstring();
    }());
    return name;
}

}  // namespace tp
