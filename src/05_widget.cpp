
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

#ifdef TP_HARNESS
static float g_testScale = 0;  // overrides the DPI scale of the widget (renders at 150%)
#endif

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
        // The visualizer sits on top of the widget: a sibling child of the
        // taskbar, or (compatibility mode) a popup owned by the widget.
        viz_.Create(overlay_ ? hwnd_ : taskbar, overlay_, [this] { StartAnimation(); });
        SetTimer(hwnd_, kTimerHousekeeping, 2000, nullptr);
        Relayout();
        return true;
    }

    void Destroy() {
        viz_.Destroy();
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
        UpdateShown();
        SyncViz();
    }

    // Tells the visualizer what the widget shows and whether it may react.
    void SyncViz() {
        if (!viz_.Hwnd()) return;
        const Snapshot* e = g_ui.Eff();
        const auto& v = GetSettings()->viz;
        Visualizer::Context c;
        c.on = v.enabled && visible_ && e && e->visible && !e->connectPrompt && !shown_.connect;
        // Music on another Spotify Connect device makes no sound here.
        bool local = e && (e->src == Src::Session ||
                           (e->src == Src::Spotify && (e->deviceName.empty() || g_localSpotifyPlaying ||
                                                       IEquals(e->deviceName, ThisComputerName()))));
        c.gateOpen = !v.onlyWhenPlaying || (e && e->playing && local);
        if (e) c.aumid = e->src == Src::Spotify ? std::wstring(L"Spotify.exe") : e->aumid;
        c.hasAccent = e && e->art;
        if (c.hasAccent) c.accent = e->art->accent;
        c.light = light_;
        c.scale = scale_;
        viz_.SetContext(c);
    }

    void UpdateShown() {
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
#ifdef TP_HARNESS
        if (g_testScale > 0) scale_ = g_testScale;
#endif
        g_uiScaleMilli = (int)std::lround(scale_ * 1000);
        RECT rc{};
        GetClientRect(taskbar_, &rc);
        heightPx_ = std::max<int>(rc.bottom - rc.top, (int)(32 * scale_));
        tbClientWidth_ = rc.right - rc.left;
        widthTarget_ = ContentWidth(shown_);
        if (!widthAnim_) width_ = widthTarget_;
        SyncViz();
        Reposition();
        Render();
        PopupRelayout();
    }

    void OnSettingsChanged() {
        light_ = IsLightTaskbar();
        ApplyTooltipTheme();
        SyncViz();
        viz_.OnSettingsChanged();
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
        volT_.Snap(volT_.to);
        Render();
    }
    const Surface& DebugSurface() const { return surf_; }
    Visualizer& DebugViz() { return viz_; }
    // Where the visualizer window sits relative to the widget.
    int DebugVizOffset() const { return viz_.DebugX() - (int)std::lround(LeftPx()); }
    // A frozen frame of the volume indicator: how far it rolled in, where the
    // bar is (-1: at the level) and how much of the bump is left.
    void DebugVolume(int level, bool muted, const std::wstring& message, float shown, float fill = -1,
                     float bump = 0) {
        volLevel_ = level;
        volMuted_ = muted;
        volMessage_ = message;
        volT_.Snap(shown);
        volFillT_.Snap(fill >= 0 ? fill : (float)std::max(0, level));
        volBumpT_.Snap(bump);
        volPctW_.Snap(level >= 0 ? TextWidth(VolumeText(level), ArtistFont()) : 0.f);
    }
    std::pair<float, float> DebugVolumeFill() const { return {volFillT_.from, volFillT_.to}; }
    void DebugWheel(int delta, WORD keys) { OnWheel(delta, keys); }
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
    enum { kTimerAnim = 1, kTimerHousekeeping = 2, kTimerVolumeSend = 3, kTimerVolumeHide = 4 };
    using VizPos = Settings::Viz::Pos;
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

    // The visualizer's slot: its full width, and how much of it is open now
    // (it grows and shrinks when sound starts and stops).
    bool VizOn(const Content& c) const { return GetSettings()->viz.enabled && !c.connect && viz_.Hwnd(); }
    float SlotFull(const Content& c) const {
        return VizOn(c) ? std::round(S((float)GetSettings()->viz.width + 8)) : 0.f;
    }
    float SlotNow() const { return SlotFull(shown_) * viz_.SlotValue(); }
    VizPos SlotPos() const { return GetSettings()->viz.position; }
    // Full widget width: the content plus the open part of the slot.
    float TotalW() const { return std::ceil(width_ + SlotNow()); }

    float ButtonsLeft(float W) const {
        return W - PadRight() - 3 * Btn() - (SlotPos() == VizPos::AfterControls ? SlotNow() : 0);
    }
    float CoverLeft() const { return Pad() + (SlotPos() == VizPos::BeforeCover ? SlotNow() : 0); }
    // The slot's left and right edge in widget coordinates.
    std::pair<float, float> SlotRange(const Content& c, float W) const {
        float s = SlotNow();
        if (SlotPos() == VizPos::BeforeCover) return {Pad() - S(4), Pad() - S(4) + s};
        if (!Controls(c)) return {W - s, W};
        float bl = ButtonsLeft(W);
        if (SlotPos() == VizPos::AfterControls) return {bl + 3 * Btn(), bl + 3 * Btn() + s};
        return {bl - s, bl};
    }
    // Where the title and artist must end.
    float TextRight(const Content& c, float W) const {
        float right = InfoRect(c, W).right - GapText();
        if (SlotPos() == VizPos::BeforeControls || (!Controls(c) && SlotPos() == VizPos::AfterControls))
            right -= SlotNow();
        return right;
    }

    float TextW(const Content& c) const {
        float tw = std::max(TextWidth(c.title, TitleFont()), TextWidth(c.artist, ArtistFont()));
        tw = std::min(tw, S((float)GetSettings()->maxTextWidth));
        float fixed = FixedW(c) + SlotFull(c);
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
        float right = Controls(c) ? ButtonsLeft(W) : W;
        float hh = HoverH();
        return RectF(0, (heightPx_ - hh) / 2, std::max(right, 1.f), (heightPx_ + hh) / 2);
    }
    D2D1_RECT_F ButtonRect(int i, float W) const {
        float x0 = ButtonsLeft(W) + i * Btn();
        return RectF(x0, 0, x0 + Btn(), (float)heightPx_);
    }

    int HitTest(float x, float y) const {
        float W = TotalW();
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
            D2D1_RECT_F r = InfoRect(shown_, TotalW());
            ti.rect = RECT{(LONG)r.left, 0, (LONG)r.right, heightPx_};
            SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
    }

    // Keeps the widget (and the visualizer just above it) over the taskbar's
    // own content.
    void EnsureOnTop() {
        if (overlay_ || !hwnd_) return;
        const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
        HWND viz = viz_.Hwnd();
        bool vizShown = viz_.Shown();
        HWND top = GetWindow(taskbar_, GW_CHILD);
        if (top != hwnd_ && !(vizShown && top == viz && GetWindow(viz, GW_HWNDNEXT) == hwnd_)) {
            SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, flags);
            top = hwnd_;
        }
        if (vizShown && top == hwnd_) SetWindowPos(viz, HWND_TOP, 0, 0, 0, 0, flags);
    }

    // Moves the visualizer window to its slot.
    void PlaceViz(float W) {
        if (!viz_.Hwnd()) return;
        float full = SlotFull(shown_);
        auto [x0, x1] = SlotRange(shown_, W);
        int winW = std::max(1, (int)std::ceil(full));
        int left = (int)std::lround((x0 + x1) / 2 - winW / 2.f);
        POINT origin = overlay_ ? screenPos_ : POINT{(LONG)std::lround(LeftPx()), 0};
        float squeeze = full > 0 ? Clamp01((x1 - x0) / full) : 0.f;
        float alpha = visible_ || winAnim_ ? Clamp01(winAlpha_) : 0.f;
        viz_.Place(origin.x + left, origin.y, winW, heightPx_, squeeze, alpha);
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
        if (volT_.Step()) active = true;
        if (volFillT_.Step()) active = true;
        if (volBumpT_.Step()) active = true;
        if (volPctW_.Step()) active = true;
        if (viz_.StepSlot()) {
            active = true;
            slotAnim_ = true;
        } else if (slotAnim_) {
            slotAnim_ = false;
            Reposition();  // the tooltip area follows the final width
        }
        if (winAlpha_ > 0 || winAnim_) Render();
        if (!active) {
            KillTimer(hwnd_, kTimerAnim);
            animating_ = false;
        }
    }

    void Render() {
        if (!hwnd_ || !g_gfx) return;
        int W = std::max(1, (int)TotalW());
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
        PlaceViz((float)W);
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
        D2D1_RECT_F cover = RectXYWH(CoverLeft(), std::round((heightPx_ - cs) / 2), cs, cs);
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
        // While scrolling, the artist line (or a lone title) rolls up and out
        // and the volume rolls in from below (and back when it hides).
        float vi = Clamp01(volT_.value);
        if (tw > 0) {
            float x = cover.right + GapCover();
            float maxRight = TextRight(c, W);
            float w = std::max(0.f, std::min(tw, maxRight - x));
            float mid = heightPx_ / 2.f;
            bool artist = !c.artist.empty();
            if (artist)
                DrawTextLine(rt, c.title, TitleFont(), RectF(x, mid - S(17), x + w, mid + S(0.5f)),
                             surf_.Brush(WithAlpha(fg, fg.a * ca)));
            D2D1_RECT_F line = artist ? RectF(x, mid - S(0.5f), x + w, mid + S(16)) : RectF(x, mid - S(10), x + w, mid + S(10));
            float roll = Height(line) * 0.55f;
            bool rolling = vi > 0.01f && vi < 0.99f;
            if (rolling) rt->PushAxisAlignedClip(line, D2D1_ANTIALIAS_MODE_ALIASED);
            if (vi < 0.99f) {
                D2D1_RECT_F out = RectF(line.left, line.top - roll * vi, line.right, line.bottom - roll * vi);
                if (artist)
                    DrawTextLine(rt, c.artist, ArtistFont(), out, surf_.Brush(WithAlpha(fg2, fg2.a * ca * (1 - vi))));
                else
                    DrawTextLine(rt, c.title, TitleFont(), out, surf_.Brush(WithAlpha(fg, fg.a * ca * (1 - vi))));
            }
            if (vi > 0.01f) {
                float d = roll * (1 - vi);
                DrawVolume(rt, RectF(line.left, line.top + d, line.right, line.bottom + d), vi, fg, fg2);
            }
            if (rolling) rt->PopAxisAlignedClip();
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

    static std::wstring VolumeText(int level) { return level < 0 ? L"--" : std::to_wstring(level) + L"%"; }

    // A speaker icon, a small bar and the percentage (or a short message).
    void DrawVolume(ID2D1RenderTarget* rt, D2D1_RECT_F r, float a, D2D1_COLOR_F fg, D2D1_COLOR_F fg2) {
        if (!volMessage_.empty()) {
            DrawTextLine(rt, volMessage_, ArtistFont(), r, surf_.Brush(WithAlpha(fg2, fg2.a * a)));
            return;
        }
        float cy = std::round((r.top + r.bottom) / 2);
        // The bar and the number follow the level smoothly; each step gives
        // the bar and the speaker a short bump.
        float level = Clamp01(volFillT_.value / 100.f);
        int v = (int)std::lround(level * 100);
        float bump = Clamp01(volBumpT_.value);
        Glyph g = volMuted_ ? Glyph::Mute
                  : v == 0  ? Glyph::Volume0
                  : v < 34  ? Glyph::Volume1
                  : v < 67  ? Glyph::Volume2
                            : Glyph::Volume3;
        float iconW = S(15);
        DrawIcon(rt, g, D2D1::Point2F(r.left + iconW / 2, cy), S(12) * (1 + 0.14f * bump),
                 surf_.Brush(WithAlpha(fg, fg.a * a)));
        std::wstring pct = VolumeText(volLevel_ < 0 ? -1 : v);
        // The number sits right after the bar; the bar takes the rest. When
        // the number gets a digit more or less, the bar's end slides.
        float pctW = volPctW_.value > 0 ? volPctW_.value : TextWidth(pct, ArtistFont());
        float gap = S(6);
        float barL = r.left + iconW + S(6), barR = r.right - pctW - gap;
        if (barR - barL >= S(8)) {
            // Like the popup's slider while it's used: thicker and in the
            // cover's accent color. Each step makes it pop, then it settles
            // back to white.
            float bh = S(4) + S(2) * bump;
            float dim = volMuted_ ? 0.4f : 1.f;
            D2D1_RECT_F track = RectF(barL, cy - bh / 2, barR, cy + bh / 2);
            rt->FillRoundedRectangle(D2D1::RoundedRect(track, bh / 2, bh / 2), surf_.Brush(WithAlpha(fg, 0.22f * a)));
            D2D1_COLOR_F fillCol = fg;
            if (!light_ && shown_.art) {
                D2D1_COLOR_F ac = shown_.art->accent;
                float t = 0.25f + 0.75f * bump;
                fillCol = D2D1_COLOR_F{fg.r + (ac.r - fg.r) * t, fg.g + (ac.g - fg.g) * t, fg.b + (ac.b - fg.b) * t, fg.a};
            }
            float fill = barL + (barR - barL) * level;
            if (level > 0.001f)
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(RectF(barL, cy - bh / 2, std::max(fill, barL + bh), cy + bh / 2), bh / 2, bh / 2),
                    surf_.Brush(WithAlpha(fillCol, fillCol.a * a * dim)));
            DrawTextLine(rt, pct, ArtistFont(), RectF(barR + gap, r.top, r.right, r.bottom),
                         surf_.Brush(WithAlpha(fg2, fg2.a * a)));
        } else {
            DrawTextLine(rt, pct, ArtistFont(), RectF(r.left + iconW + S(3), r.top, r.right, r.bottom),
                         surf_.Brush(WithAlpha(fg2, fg2.a * a)));
        }
    }

    // ---- Scroll to change the volume ----

    void OnWheel(int delta, WORD keys) {
        auto set = GetSettings();
        const Snapshot* e = g_ui.Eff();
        if (!set->scroll.enabled || !visible_ || !e || !e->visible || shown_.connect) return;
        if (Tick() - wheelTick_ > 500) wheelAcc_ = 0;
        wheelTick_ = Tick();
        int step = (keys & MK_SHIFT) ? 1 : set->scroll.step;
        int change = WheelSteps(wheelAcc_, set->scroll.invert ? -delta : delta, step);
        if (!change) return;
        if (!set->scroll.system && e->src == Src::Spotify) {
            // Spotify's own volume (also works for phones and speakers).
            if (!e->canVolume) {
                // Some devices (e.g. phones) don't allow it.
                ShowVolume(-1, false, L"Volume locked");
                return;
            }
            int old = std::clamp(e->volume, 0, 100);
            int v = std::clamp(old + change, 0, 100);
            SetSpotifyVolume(v);  // (refreshes the snapshot: `e` is stale after this)
            ShowVolume(v, false, L"", old);
            return;
        }
        bool system = set->scroll.system || e->src != Src::Session || e->aumid.empty();
        g_mixer.Nudge(system, system ? L"" : e->aumid, change, hwnd_);
    }

    // Sends Spotify volume changes at most about four times a second.
    void SetSpotifyVolume(int v) {
        g_ui.OverrideVolume(v);
        PopupOnSnapshot();
        pendingSpotifyVolume_ = v;
        if (Tick() - lastSpotifyVolumeSend_ >= 250) {
            SendAct(Act::Volume, v);
            lastSpotifyVolumeSend_ = Tick();
            pendingSpotifyVolume_ = -1;
        } else {
            SetTimer(hwnd_, kTimerVolumeSend, 260, nullptr);
        }
    }

    // `from` is the level before this change (-1 if unknown): the bar then
    // slides from there even when it just appeared.
    void ShowVolume(int level, bool muted, std::wstring message, int from = -1) {
        if (!GetSettings()->scroll.indicator) return;
        bool anim = GetSettings()->popupAnimations;
        bool barUp = volT_.to > 0.5f && volMessage_.empty() && volLevel_ >= 0;
        if (level >= 0) {
            if (!barUp) volFillT_.Snap((float)(from >= 0 ? from : level));
            // Every notch pops the bar; a smaller pop when it's already at 0 or 100.
            bool changed = barUp ? level != volLevel_ : from >= 0 && from != level;
            volBumpT_.Snap(changed ? 1.f : std::max(volBumpT_.value, 0.5f));
            volBumpT_.To(0.f, anim ? 420.f : 0.f);
            volFillT_.To((float)level, anim ? 260.f : 0.f, Ease::OutQuint);
            float pw = TextWidth(VolumeText(level), ArtistFont());
            if (!barUp) volPctW_.Snap(pw);
            else volPctW_.To(pw, anim ? 200.f : 0.f);
        }
        volLevel_ = level;
        volMuted_ = muted;
        volMessage_ = std::move(message);
        volT_.To(1.f, anim ? 200.f : 0.f);
        SetTimer(hwnd_, kTimerVolumeHide, volMessage_.empty() ? 1400 : 2400, nullptr);
        StartAnimation();
        Render();
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
            case WM_MOUSEWHEEL:
                OnWheel(GET_WHEEL_DELTA_WPARAM(wp), GET_KEYSTATE_WPARAM(wp));
                return 0;
            case WM_TP_VOLUME:
                if (wp & kVolFailed) ShowVolume(-1, false, L"Volume unavailable");
                else
                    ShowVolume((int)(wp & 0xFF), (wp & kVolMuted) != 0, L"",
                               (wp & kVolHasBefore) ? (int)((wp >> 16) & 0xFF) : -1);
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
                } else if (wp == kTimerVolumeSend) {
                    KillTimer(hwnd_, kTimerVolumeSend);
                    if (pendingSpotifyVolume_ >= 0) {
                        SendAct(Act::Volume, pendingSpotifyVolume_);
                        lastSpotifyVolumeSend_ = Tick();
                        pendingSpotifyVolume_ = -1;
                    }
                } else if (wp == kTimerVolumeHide) {
                    KillTimer(hwnd_, kTimerVolumeHide);
                    volT_.To(0.f, GetSettings()->popupAnimations ? 260.f : 0.f);
                    StartAnimation();
                    Render();
                } else if (wp == kTimerHousekeeping) {
                    RECT rc{};
                    GetClientRect(taskbar_, &rc);
                    UINT dpi = GetDpiForWindow(taskbar_);
                    float scale = (dpi ? dpi : 96) / 96.f;
#ifdef TP_HARNESS
                    if (g_testScale > 0) scale = g_testScale;
#endif
                    if (rc.bottom - rc.top != heightPx_ || rc.right - rc.left != tbClientWidth_ ||
                        std::fabs(scale - scale_) > 0.001f) {
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
                viz_.Destroy();
                KillTimer(h, kTimerAnim);
                KillTimer(h, kTimerHousekeeping);
                KillTimer(h, kTimerVolumeSend);
                KillTimer(h, kTimerVolumeHide);
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

    Visualizer viz_;
    bool slotAnim_ = false;

    // Scroll volume.
    float wheelAcc_ = 0;
    ULONGLONG wheelTick_ = 0;
    int pendingSpotifyVolume_ = -1;
    ULONGLONG lastSpotifyVolumeSend_ = 0;
    Tween volT_;      // volume indicator visibility (rolls in and out)
    Tween volFillT_;  // the bar's level, 0..100, follows changes smoothly
    Tween volBumpT_;  // 1 right after a change, fading to 0
    Tween volPctW_;   // width of the percentage text (px), slides when digits change
    int volLevel_ = -1;
    bool volMuted_ = false;
    std::wstring volMessage_;
};

}  // namespace tp
