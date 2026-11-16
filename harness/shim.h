// Minimal stand-ins for the Windhawk API so the mod can run in a normal process.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

inline std::map<std::wstring, std::wstring>& HarnessSettings() {
    static std::map<std::wstring, std::wstring> m;
    return m;
}
inline std::wstring& HarnessStorePath() {
    static std::wstring p;
    return p;
}

inline void HarnessLog(const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfwprintf(stderr, fmt, a);
    va_end(a);
    fflush(stderr);
}
#define Wh_Log(fmt, ...) HarnessLog(L"[%d:%S] " fmt L"\n", __LINE__, __FUNCTION__, ##__VA_ARGS__)

inline int Wh_GetIntSetting(PCWSTR name, ...) {
    auto it = HarnessSettings().find(name);
    return it == HarnessSettings().end() ? 0 : _wtoi(it->second.c_str());
}
inline PCWSTR Wh_GetStringSetting(PCWSTR name, ...) {
    auto it = HarnessSettings().find(name);
    return _wcsdup(it == HarnessSettings().end() ? L"" : it->second.c_str());
}
inline void Wh_FreeStringSetting(PCWSTR s) { free((void*)s); }
inline size_t Wh_GetStringValue(PCWSTR name, PWSTR buf, size_t len) {
    return GetPrivateProfileStringW(L"store", name, L"", buf, (DWORD)len, HarnessStorePath().c_str());
}
inline BOOL Wh_SetStringValue(PCWSTR name, PCWSTR value) {
    return WritePrivateProfileStringW(L"store", name, value, HarnessStorePath().c_str());
}
inline BOOL Wh_DeleteValue(PCWSTR name) {
    return WritePrivateProfileStringW(L"store", name, nullptr, HarnessStorePath().c_str());
}

namespace WindhawkUtils {
using WH_SUBCLASSPROC = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM, DWORD_PTR);

inline UINT SubclassMsg() {
    static UINT m = RegisterWindowMessageW(L"Harness_SetWindowSubclassFromAnyThread");
    return m;
}
inline LRESULT CALLBACK SubclassWrapper(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    if (m == WM_NCDESTROY || (m == SubclassMsg() && !w && (UINT_PTR)l == id))
        RemoveWindowSubclass(h, SubclassWrapper, id);
    return ((WH_SUBCLASSPROC)id)(h, m, w, l, ref);
}
struct SubclassParam {
    UINT_PTR id;
    DWORD_PTR ref;
    BOOL result;
};
inline LRESULT CALLBACK SubclassHook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        auto* cwp = (const CWPSTRUCT*)l;
        if (cwp->message == SubclassMsg() && cwp->wParam) {
            auto* p = (SubclassParam*)cwp->lParam;
            p->result = SetWindowSubclass(cwp->hwnd, SubclassWrapper, p->id, p->ref);
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
inline BOOL SetWindowSubclassFromAnyThread(HWND h, WH_SUBCLASSPROC proc, DWORD_PTR ref) {
    DWORD tid = GetWindowThreadProcessId(h, nullptr);
    if (tid == GetCurrentThreadId()) return SetWindowSubclass(h, SubclassWrapper, (UINT_PTR)proc, ref);
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, SubclassHook, nullptr, tid);
    if (!hook) return FALSE;
    SubclassParam p{(UINT_PTR)proc, ref, FALSE};
    SendMessageW(h, SubclassMsg(), TRUE, (LPARAM)&p);
    UnhookWindowsHookEx(hook);
    return p.result;
}
inline void RemoveWindowSubclassFromAnyThread(HWND h, WH_SUBCLASSPROC proc) {
    SendMessageW(h, SubclassMsg(), FALSE, (LPARAM)proc);
}
}  // namespace WindhawkUtils

inline size_t Wh_GetModStoragePath(PWSTR buf, size_t len) {
    std::wstring dir = HarnessStorePath();
    size_t slash = dir.find_last_of(L'\x5C');
    if (slash != std::wstring::npos) dir.resize(slash);
    if (dir.size() + 1 > len) return 0;
    wcscpy(buf, dir.c_str());
    return dir.size();
}
