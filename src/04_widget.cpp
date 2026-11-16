
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
