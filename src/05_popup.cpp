
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
