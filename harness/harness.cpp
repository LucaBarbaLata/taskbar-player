// Test harness: runs the mod's UI outside explorer and saves renders as PNG files.
//   harness.exe render <outDir> <image> [cropX cropY cropSize]
//   harness.exe live <outDir> <seconds>
//   harness.exe audio <outDir> <seconds> [style] [all|app]   (real visualizer + CPU use)
//   harness.exe volume <outDir> [aumid...]                 (reads, never changes, volumes)
#include "shim.h"
#include <shellscalingapi.h>

#include "../taskbar-player.wh.cpp"

#include <systemmediatransportcontrolsinterop.h>

using namespace tp;

static int g_failures = 0;

static bool SavePixels(const BYTE* src, int w, int h, const std::wstring& path, uint32_t bg) {
    if (!src || w <= 0 || h <= 0) return false;
    std::vector<BYTE> px((size_t)w * h * 4);
    float bgB = (float)(bg & 255), bgG = (float)((bg >> 8) & 255), bgR = (float)((bg >> 16) & 255);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        float a = src[i * 4 + 3] / 255.f;
        px[i * 4 + 0] = (BYTE)std::min(255.f, src[i * 4 + 0] + bgB * (1 - a));
        px[i * 4 + 1] = (BYTE)std::min(255.f, src[i * 4 + 1] + bgG * (1 - a));
        px[i * 4 + 2] = (BYTE)std::min(255.f, src[i * 4 + 2] + bgR * (1 - a));
        px[i * 4 + 3] = 255;
    }
    com_ptr<IWICBitmap> bmp;
    if (FAILED(Wic()->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, (UINT)px.size(),
                                             px.data(), bmp.put())))
        return false;
    com_ptr<IWICStream> stream;
    Wic()->CreateStream(stream.put());
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    com_ptr<IWICBitmapEncoder> enc;
    Wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put());
    enc->Initialize(stream.get(), WICBitmapEncoderNoCache);
    com_ptr<IWICBitmapFrameEncode> frame;
    enc->CreateNewFrame(frame.put(), nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WriteSource(bmp.get(), nullptr);
    frame->Commit();
    enc->Commit();
    fwprintf(stderr, L"saved %s (%dx%d)\n", path.c_str(), w, h);
    return true;
}

static bool SavePng(const Surface& s, const std::wstring& path, uint32_t bg) {
    return SavePixels((const BYTE*)s.Bits(), s.w, s.h, path, bg);
}

// Premultiplied BGRA pixels.
struct Image {
    int w = 0, h = 0;
    std::vector<BYTE> px;
};

static Image Grab(const Surface& s) {
    Image img;
    img.w = s.w;
    img.h = s.h;
    const BYTE* p = (const BYTE*)s.Bits();
    if (p) img.px.assign(p, p + (size_t)s.w * s.h * 4);
    return img;
}

// Draws `src` over `dst` at (ox, oy).
static void Over(Image& dst, const Surface& src, int ox, int oy) {
    const BYTE* sp = (const BYTE*)src.Bits();
    if (!sp) return;
    for (int y = 0; y < src.h; y++) {
        for (int x = 0; x < src.w; x++) {
            int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            const BYTE* s = sp + ((size_t)y * src.w + x) * 4;
            BYTE* d = dst.px.data() + ((size_t)dy * dst.w + dx) * 4;
            float a = s[3] / 255.f;
            for (int c = 0; c < 4; c++) d[c] = (BYTE)std::min(255.f, s[c] + d[c] * (1 - a));
        }
    }
}

// The widget as it appears on the taskbar: its window plus the visualizer window.
static Image WidgetImage() {
    Image img = Grab(g_widget->DebugSurface());
    HWND viz = g_widget->DebugViz().Hwnd();
    if (viz && g_widget->DebugViz().Shown()) Over(img, g_widget->DebugViz().DebugSurface(), g_widget->DebugVizOffset(), 0);
    return img;
}

static bool SaveImage(const Image& img, const std::wstring& path, uint32_t bg) {
    return SavePixels(img.px.data(), img.w, img.h, path, bg);
}

// A labelled grid of images saved as one PNG.
struct Sheet {
    struct Row {
        std::wstring label;
        std::vector<std::pair<std::wstring, Image>> cells;
    };
    std::vector<Row> rows;

    void Add(const std::wstring& row, const std::wstring& label, Image img) {
        if (rows.empty() || rows.back().label != row) rows.push_back(Row{row, {}});
        rows.back().cells.push_back({label, std::move(img)});
    }

    void Save(const std::wstring& path, uint32_t bg, float scale) {
        float pad = 10 * scale, labelH = 18 * scale, rowLabelW = 120 * scale;
        int maxCols = 0, cellW = 0, cellH = 0;
        for (auto& r : rows) {
            maxCols = std::max(maxCols, (int)r.cells.size());
            for (auto& c : r.cells) {
                cellW = std::max(cellW, c.second.w);
                cellH = std::max(cellH, c.second.h);
            }
        }
        int W = (int)(rowLabelW + maxCols * (cellW + pad) + pad);
        int H = (int)(pad + rows.size() * (cellH + labelH + pad));
        Surface s;
        s.Resize(W, H);
        ID2D1RenderTarget* rt = s.Begin();
        if (!rt) return;
        rt->Clear(Rgba(bg));
        auto* font = g_gfx->Font(false, 11.5f * scale, DWRITE_FONT_WEIGHT_NORMAL);
        auto* bold = g_gfx->Font(false, 12 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        for (size_t ri = 0; ri < rows.size(); ri++) {
            float y = pad + ri * (cellH + labelH + pad);
            DrawTextLine(rt, rows[ri].label, bold, RectF(pad, y + labelH, rowLabelW, y + labelH + cellH),
                         s.Brush(Rgba(0xFFFFFF, 0.9f)));
            for (size_t ci = 0; ci < rows[ri].cells.size(); ci++) {
                auto& [label, img] = rows[ri].cells[ci];
                float x = rowLabelW + ci * (cellW + pad);
                DrawTextLine(rt, label, font, RectF(x, y, x + cellW, y + labelH), s.Brush(Rgba(0xFFFFFF, 0.6f)));
                com_ptr<IWICBitmap> wic;
                if (FAILED(Wic()->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppPBGRA, img.w * 4,
                                                         (UINT)img.px.size(), img.px.data(), wic.put())))
                    continue;
                com_ptr<ID2D1Bitmap> bmp;
                if (FAILED(rt->CreateBitmapFromWicBitmap(wic.get(), nullptr, bmp.put()))) continue;
                rt->DrawBitmap(bmp.get(), RectXYWH(x, y + labelH, (float)img.w, (float)img.h));
            }
        }
        s.End();
        SavePng(s, path, bg);
    }
};

static LRESULT CALLBACK FakeTaskbarProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static HWND CreateFakeTaskbar() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = FakeTaskbarProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TP_FakeTaskbar";
    RegisterClassExW(&wc);
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    int h = MulDiv(48, dpiX, 96);
    return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, mi.rcMonitor.left,
                           mi.rcMonitor.bottom - h, mi.rcMonitor.right - mi.rcMonitor.left, h, nullptr,
                           nullptr, wc.hInstance, nullptr);
}

static std::shared_ptr<Art> LoadArt(const std::wstring& path, int cx, int cy, int cs) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    std::vector<BYTE> data(size);
    ReadFile(f, data.data(), size, &read, nullptr);
    CloseHandle(f);
    UINT smallPx, largePx;
    ArtSizes(smallPx, largePx);
    if (cs <= 0) return MakeArt(L"test", data.data(), data.size(), smallPx, largePx);
    auto src = DecodeImage(data.data(), data.size());
    if (!src) return nullptr;
    com_ptr<IWICBitmapClipper> clip;
    Wic()->CreateBitmapClipper(clip.put());
    WICRect rc{cx, cy, cs, cs};
    clip->Initialize(src.get(), &rc);
    auto cropped = ToBitmap(clip.get());
    // Re-encode is unnecessary: build the Art directly.
    auto art = std::make_shared<Art>();
    art->key = L"test";
    art->full = SquareScaled(cropped.get(), std::min(cs, 640));
    art->small = SquareScaled(art->full.get(), smallPx);
    art->smallPx = smallPx;
    art->large = SquareScaled(art->full.get(), largePx);
    art->largePx = largePx;
    art->accent = ComputeAccent(art->full.get());
    return art;
}

// A generated cover (purple to orange), so renders show the accent color.
static std::shared_ptr<Art> SyntheticArt() {
    const int N = 300;
    Surface s;
    s.Resize(N, N);
    ID2D1RenderTarget* rt = s.Begin();
    if (!rt) return nullptr;
    D2D1_GRADIENT_STOP stops[] = {{0.f, Rgba(0x2B1055)}, {0.55f, Rgba(0xC2185B)}, {1.f, Rgba(0xFF8A1E)}};
    com_ptr<ID2D1GradientStopCollection> coll;
    rt->CreateGradientStopCollection(stops, 3, coll.put());
    com_ptr<ID2D1LinearGradientBrush> g;
    rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {(float)N, (float)N}), coll.get(),
                                  g.put());
    rt->FillRectangle(RectF(0, 0, (float)N, (float)N), g.get());
    rt->FillEllipse(D2D1::Ellipse({N * 0.62f, N * 0.4f}, N * 0.2f, N * 0.2f), s.Brush(Rgba(0xFFD54F)));
    s.End();
    com_ptr<IWICBitmap> bmp;
    if (FAILED(Wic()->CreateBitmapFromMemory(N, N, GUID_WICPixelFormat32bppPBGRA, N * 4, N * N * 4,
                                             (BYTE*)s.Bits(), bmp.put())))
        return nullptr;
    UINT smallPx, largePx;
    ArtSizes(smallPx, largePx);
    auto art = std::make_shared<Art>();
    art->key = L"synthetic";
    art->full = ToPbgra(bmp.get());
    art->small = SquareScaled(art->full.get(), smallPx);
    art->smallPx = smallPx;
    art->large = SquareScaled(art->full.get(), largePx);
    art->largePx = largePx;
    art->accent = ComputeAccent(art->full.get());
    return art;
}

static void DefaultSettings() {
    auto& s = HarnessSettings();
    s[L"redirectPort"] = L"8974";
    s[L"maxTextWidth"] = L"220";
    s[L"showTaskbarControls"] = L"1";
    s[L"leftMargin"] = L"6";
    s[L"hoverRadius"] = L"6";
    s[L"popupBackground"] = L"blur";
    s[L"popupTint"] = L"3A232323";
    s[L"popupAnimations"] = L"1";
    s[L"hideWhenPausedMinutes"] = L"0";
    s[L"overlayMode"] = L"0";
    // The defaults from the settings block of the mod.
    s[L"scrollVolume.enabled"] = L"1";
    s[L"scrollVolume.target"] = L"app";
    s[L"scrollVolume.step"] = L"5";
    s[L"scrollVolume.invert"] = L"0";
    s[L"scrollVolume.indicator"] = L"1";
    s[L"visualizer.enabled"] = L"1";
    s[L"visualizer.style"] = L"bars";
    s[L"visualizer.position"] = L"afterControls";
    s[L"visualizer.width"] = L"32";
    s[L"visualizer.height"] = L"20";
    s[L"visualizer.bars"] = L"6";
    s[L"visualizer.gap"] = L"2";
    s[L"visualizer.roundness"] = L"100";
    s[L"visualizer.peaks"] = L"0";
    s[L"visualizer.mirror"] = L"0";
    s[L"visualizer.colorMode"] = L"accent";
    s[L"visualizer.color"] = L"FF1ED760";
    s[L"visualizer.color2"] = L"FFB15CFF";
    s[L"visualizer.opacity"] = L"100";
    s[L"visualizer.sensitivity"] = L"100";
    s[L"visualizer.autoLevel"] = L"1";
    s[L"visualizer.riseSpeed"] = L"80";
    s[L"visualizer.fallSpeed"] = L"35";
    s[L"visualizer.frameRate"] = L"60";
    s[L"visualizer.minFrequency"] = L"50";
    s[L"visualizer.maxFrequency"] = L"10000";
    s[L"visualizer.source"] = L"all";
    s[L"visualizer.onlyWhenPlaying"] = L"1";
    s[L"visualizer.whenSilent"] = L"shrink";
    s[L"visualizer.silenceDelay"] = L"2";
}

// Deterministic "music" for renders.
static std::vector<float> FakeLevels(int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; i++) v[i] = 0.22f + 0.7f * std::fabs(std::sin(i * 1.7f + 0.6f));
    return v;
}
static std::vector<float> FakeWave(int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / std::max(1, n - 1);
        v[i] = 0.62f * std::sin(6.2832f * 2 * t) + 0.25f * std::sin(6.2832f * 5 * t + 1.f);
    }
    return v;
}

static std::shared_ptr<Snapshot> SpotifySnap(std::shared_ptr<Art> art) {
    auto s = std::make_shared<Snapshot>();
    s->visible = true;
    s->src = Src::Spotify;
    s->isSpotify = true;
    s->appName = L"Spotify";
    s->spotifyConfigured = s->spotifyAuthorized = true;
    s->title = L"Summer of 90";
    s->artist = L"gelowler";
    s->trackKey = L"k1";
    s->trackUri = L"spotify:track:123";
    s->art = art;
    s->playing = true;
    s->posMs = 83000;
    s->posTick = Tick();
    s->durMs = 201000;
    s->canPlayPause = s->canNext = s->canPrev = s->canSeek = s->canShuffle = s->canRepeat = true;
    s->shuffle = true;
    s->repeat = RepeatMode::Context;
    s->canLike = true;
    s->liked = 1;
    s->canVolume = true;
    s->volume = 64;
    s->canDevices = true;
    s->deviceId = L"d2";
    s->deviceName = L"Living Room";
    s->deviceType = L"Speaker";
    s->devices = {
        {L"d1", L"DESKTOP-LUCA", L"Computer", false, false, true, 80},
        {L"d2", L"Living Room", L"Speaker", true, false, true, 64},
        {L"d3", L"Pixel 9", L"Smartphone", false, false, true, 50},
        {L"d4", L"Bedroom TV", L"TV", false, true, false, -1},
    };
    return s;
}

static void Pump(DWORD ms) {
    ULONGLONG end = Tick() + ms;
    while (Tick() < end) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
}

static void RenderMode(const std::wstring& out, const std::wstring& image, int cx, int cy, int cs) {
    LoadSettings();
    HWND tb = CreateFakeTaskbar();
    g_taskbarHwnd = tb;
    UiInit(tb);
    if (!g_widget || !g_popup) {
        fwprintf(stderr, L"UI init failed\n");
        return;
    }
    auto art = image.empty() ? SyntheticArt() : LoadArt(image, cx, cy, cs);
    fwprintf(stderr, L"art=%p accent=%.2f,%.2f,%.2f scale=%.2f\n", art.get(), art ? art->accent.r : 0,
             art ? art->accent.g : 0, art ? art->accent.b : 0, g_widget->Scale());
    const uint32_t taskbarBg = 0x1B1B1F, desktopBg = 0x2E3440;
    RECT anchor = g_widget->ScreenRect();

    auto show = [&](std::shared_ptr<Snapshot> s) {
        g_ui.SetSnapshot(s);
        g_widget->OnSnapshot();
        g_widget->DebugRender(-1);
        anchor = g_widget->ScreenRect();
    };

    {
        Surface logo;
        logo.Resize(520, 240);
        if (ID2D1RenderTarget* rt = logo.Begin()) {
            DrawSpotifyLogo(logo, rt, D2D1::Point2F(120, 120), 200, 1.f);
            DrawSpotifyLogo(logo, rt, D2D1::Point2F(300, 120), 48, 1.f);
            DrawSpotifyLogo(logo, rt, D2D1::Point2F(380, 120), 30, 1.f);
            DrawSpotifyLogo(logo, rt, D2D1::Point2F(440, 120), 20, 1.f);
            logo.End();
            SavePng(logo, out + L"\\logo.png", 0x1F1F1F);
        }
    }

    // Playback icons: big (a 512 px cell each, the icon 448 px tall, to
    // compare with the reference artwork), then at the sizes the widget and the
    // popup use at 100%, 125%, 150% and 200% scaling.
    {
        const Transport kinds[] = {Transport::Play, Transport::Prev, Transport::Pause, Transport::Next};
        Surface big;
        big.Resize(2048, 512);
        if (ID2D1RenderTarget* rt = big.Begin()) {
            for (int i = 0; i < 4; i++)
                DrawTransport(rt, kinds[i], D2D1::Point2F(256.f + 512 * i, 256), 448 / 0.78f,
                              big.Brush(Rgba(0x000000)));
            big.End();
            SavePng(big, out + L"\\transport_big.png", 0xFFFFFF);
        }
        Surface small;
        small.Resize(620, 300);
        if (ID2D1RenderTarget* rt = small.Begin()) {
            const float scales[] = {1.f, 1.25f, 1.5f, 2.f};
            for (int row = 0; row < 4; row++) {
                float sc = scales[row], y = 30.f + 70 * row;
                // Widget: 13 (prev/next) and 14 (play/pause) at this scale.
                for (int i = 0; i < 4; i++) {
                    float sz = (kinds[i] == Transport::Play || kinds[i] == Transport::Pause ? 14 : 13) * sc;
                    DrawTransport(rt, kinds[i], D2D1::Point2F(30 + 34 * sc * i, y), sz, small.Brush(Rgba(0xFFFFFF)));
                }
                // Popup: 19 (prev/next) and 24 (play/pause).
                for (int i = 0; i < 4; i++) {
                    float sz = (kinds[i] == Transport::Play || kinds[i] == Transport::Pause ? 24 : 19) * sc;
                    DrawTransport(rt, kinds[i], D2D1::Point2F(290 + 70 * sc * i / 1.5f, y), sz,
                                  small.Brush(Rgba(0xFFFFFF)));
                }
            }
            small.End();
            SavePng(small, out + L"\\transport_small.png", 0x1B1B1F);
        }
    }

    auto snap = SpotifySnap(art);
    show(snap);
    InitTrace();
    fwprintf(stderr, L"popup composited=%d\n", g_popup->DebugComposited());
    fwprintf(stderr, L"GPU render frames ok=%d\n", g_popup->DebugRenderComposited(anchor));
    SavePng(g_widget->DebugSurface(), out + L"\\widget.png", taskbarBg);
    g_widget->DebugRender(0);
    SavePng(g_widget->DebugSurface(), out + L"\\widget_hover_info.png", taskbarBg);
    g_widget->DebugRender(2);
    SavePng(g_widget->DebugSurface(), out + L"\\widget_hover_play.png", taskbarBg);

    g_popup->DebugRender(anchor, -1, false, L"");
    SavePng(g_popup->DebugSurface(), out + L"\\popup.png", desktopBg);
    g_popup->DebugRender(anchor, 3, false, L"");
    SavePng(g_popup->DebugSurface(), out + L"\\popup_hover_progress.png", desktopBg);
    g_popup->DebugRender(anchor, -1, true, L"");
    SavePng(g_popup->DebugSurface(), out + L"\\popup_devices.png", desktopBg);
    g_popup->DebugRender(anchor, 4, false, L"Spotify Premium is required for this");
    SavePng(g_popup->DebugSurface(), out + L"\\popup_toast.png", desktopBg);

    // A browser session without artwork and with limited controls.
    auto ses = std::make_shared<Snapshot>();
    ses->visible = true;
    ses->src = Src::Session;
    ses->appName = L"Google Chrome";
    ses->aumid = L"Chrome";
    ses->spotifyConfigured = true;
    ses->title = L"Lofi hip hop radio \x2013 beats to relax/study to (a very long title that gets truncated)";
    ses->artist = L"Lofi Girl";
    ses->trackKey = L"k2";
    ses->playing = false;
    ses->canPlayPause = ses->canNext = true;
    show(ses);
    SavePng(g_widget->DebugSurface(), out + L"\\widget_session.png", taskbarBg);
    g_popup->DebugRender(anchor, -1, false, L"");
    SavePng(g_popup->DebugSurface(), out + L"\\popup_session.png", desktopBg);

    // Click handling: a normal click, then a press whose button-up never arrives.
    {
        InitTrace();
        POINT c = g_popup->DebugConnectCenter();
        HWND ph = g_popup->Hwnd();
        SendMessageW(ph, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
        SendMessageW(ph, WM_LBUTTONUP, 0, MAKELPARAM(c.x, c.y));
        SendMessageW(ph, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(c.x, c.y));
        Pump(200);
        fwprintf(stderr, L"click test done (see taskbar-player.log)\n");
    }

    auto connect = std::make_shared<Snapshot>();
    connect->visible = true;
    connect->connectPrompt = true;
    connect->isSpotify = true;
    connect->spotifyConfigured = true;
    connect->title = L"Connect Spotify";
    connect->artist = L"Click to sign in";
    connect->trackKey = L"#connect";
    show(connect);
    SavePng(g_widget->DebugSurface(), out + L"\\widget_connect.png", taskbarBg);

    // ---- Scroll volume (Spotify: changes the shown volume right away) ----
    {
        auto s = SpotifySnap(art);
        show(s);
        g_widget->DebugVolume(-1, false, L"", 0.f);
        int v0 = g_ui.Eff()->volume;
        g_widget->DebugWheel(120, 0);
        int v1 = g_ui.Eff()->volume;
        auto fill1 = g_widget->DebugVolumeFill();  // the bar slides from the old level
        for (int i = 0; i < 3; i++) g_widget->DebugWheel(-40, 0);  // touchpad: three small steps
        int v2 = g_ui.Eff()->volume;
        g_widget->DebugWheel(120, MK_SHIFT);
        int v3 = g_ui.Eff()->volume;
        auto fill3 = g_widget->DebugVolumeFill();
        bool ok = v0 == 64 && v1 == 69 && v2 == 64 && v3 == 65;
        fwprintf(stderr, L"%s scroll test: %d -> %d (notch up) -> %d (touchpad down) -> %d (Shift)\n",
                 ok ? L"ok  " : L"FAIL", v0, v1, v2, v3);
        bool slide = fill1.first == 64.f && fill1.second == 69.f && fill3.second == 65.f;
        fwprintf(stderr, L"%s volume bar slides: %.0f -> %.0f on the first notch, last target %.0f\n",
                 slide ? L"ok  " : L"FAIL", fill1.first, fill1.second, fill3.second);
        g_widget->DebugVolume(-1, false, L"", 0.f);
    }

    // ---- Visualizer: every style and color mode, positions, sizes ----
    {
        Sheet sheet;
        auto set = [&](std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> kv) {
            for (auto& [k, v] : kv) HarnessSettings()[k] = v;
            LoadSettings();
            g_widget->OnSettingsChanged();
        };
        auto reset = [&] {
            DefaultSettings();
            LoadSettings();
            g_widget->OnSettingsChanged();
        };
        auto frame = [&](float slot = 1.f, float peakBoost = 0.14f) {
            int bars = GetSettings()->viz.bars;
            g_widget->DebugViz().DebugSet(FakeLevels(bars), FakeWave(64), peakBoost, slot);
            g_widget->DebugVolume(-1, false, L"", false);
            g_widget->DebugRender(-1);
            return WidgetImage();
        };
        auto s = SpotifySnap(art);  // plays on "Living Room": no real audio is captured
        show(s);
        const wchar_t* styles[][2] = {{L"bars", L"Bars"}, {L"mirrored", L"Mirrored bars"}, {L"line", L"Smooth line"},
                                      {L"dots", L"Dots (LED)"}, {L"wave", L"Wave"}};
        const wchar_t* colors[][2] = {{L"accent", L"album accent"}, {L"text", L"text color"},
                                      {L"custom", L"custom"}, {L"gradient", L"gradient"}, {L"rainbow", L"rainbow"}};
        for (auto& st : styles) {
            for (auto& co : colors) {
                reset();
                set({{L"visualizer.style", st[0]}, {L"visualizer.colorMode", co[0]}});
                sheet.Add(st[1], co[1], frame());
            }
        }
        reset();
        sheet.Add(L"Position", L"right of buttons (default)", frame());
        set({{L"visualizer.position", L"beforeControls"}});
        sheet.Add(L"Position", L"left of buttons", frame());
        set({{L"visualizer.position", L"beforeCover"}});
        sheet.Add(L"Position", L"left of cover", frame());
        reset();
        set({{L"showTaskbarControls", L"0"}});
        sheet.Add(L"Position", L"no buttons", frame());

        reset();
        set({{L"visualizer.peaks", L"1"}});
        sheet.Add(L"Options", L"peak markers", frame(1.f, 0.22f));
        set({{L"visualizer.peaks", L"1"}, {L"visualizer.style", L"dots"}});
        sheet.Add(L"Options", L"dots + peaks", frame(1.f, 0.3f));
        reset();
        set({{L"visualizer.bars", L"16"}, {L"visualizer.width", L"60"}, {L"visualizer.gap", L"1"}, {L"visualizer.height", L"28"}});
        sheet.Add(L"Options", L"16 bars, 60x28", frame());
        reset();
        set({{L"visualizer.roundness", L"0"}, {L"visualizer.colorMode", L"gradient"}, {L"visualizer.style", L"mirrored"}});
        sheet.Add(L"Options", L"square, gradient", frame());
        reset();
        set({{L"visualizer.opacity", L"55"}, {L"visualizer.mirror", L"1"}});
        sheet.Add(L"Options", L"55% opacity, bass right", frame());

        reset();
        sheet.Add(L"Silence", L"full", frame(1.f));
        sheet.Add(L"Silence", L"shrinking (half)", frame(0.5f));
        sheet.Add(L"Silence", L"hidden", frame(0.f));
        set({{L"visualizer.whenSilent", L"rest"}});
        g_widget->DebugViz().DebugSet(std::vector<float>(6, 0.f), std::vector<float>(64, 0.f), 0.f, 1.f);
        g_widget->DebugRender(-1);
        sheet.Add(L"Silence", L"flat bars (rest)", WidgetImage());

        reset();
        auto volume = [&](int level, bool muted, const wchar_t* msg) {
            int bars = GetSettings()->viz.bars;
            g_widget->DebugViz().DebugSet(FakeLevels(bars), FakeWave(64), 0.14f, 1.f);
            g_widget->DebugVolume(level, muted, msg, true);
            g_widget->DebugRender(-1);
            Image img = WidgetImage();
            g_widget->DebugVolume(-1, false, L"", false);
            return img;
        };
        sheet.Add(L"Scroll volume", L"64%", volume(64, false, L""));
        sheet.Add(L"Scroll volume", L"muted", volume(30, true, L""));
        sheet.Add(L"Scroll volume", L"100%", volume(100, false, L""));
        sheet.Add(L"Scroll volume", L"not possible", volume(-1, false, L"Volume locked"));
        // Frames of the animation: rolling in, the bar sliding 40 -> 64 with
        // the bump fading, then settled.
        auto frameOf = [&](float shown, float fill, float bump) {
            int bars = GetSettings()->viz.bars;
            g_widget->DebugViz().DebugSet(FakeLevels(bars), FakeWave(64), 0.14f, 1.f);
            g_widget->DebugVolume(64, false, L"", shown, fill, bump);
            g_widget->DebugRender(-1);
            Image img = WidgetImage();
            g_widget->DebugVolume(-1, false, L"", 0.f);
            return img;
        };
        sheet.Add(L"Volume animation", L"rolling in 35%", frameOf(0.35f, 40, 1.f));
        sheet.Add(L"Volume animation", L"rolling in 75%", frameOf(0.75f, 48, 0.8f));
        sheet.Add(L"Volume animation", L"sliding, bump", frameOf(1.f, 57, 0.45f));
        sheet.Add(L"Volume animation", L"settled", frameOf(1.f, 64, 0.f));
        sheet.Add(L"Volume animation", L"rolling out 50%", frameOf(0.5f, 64, 0.f));
        sheet.Save(out + L"\\visualizer_sheet.png", taskbarBg, g_widget->Scale());

        // The same at 150% scaling (a 72 px taskbar).
        Sheet big;
        g_testScale = 1.5f;
        RECT tr{};
        GetWindowRect(tb, &tr);
        SetWindowPos(tb, nullptr, 0, 0, tr.right - tr.left, 72, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        g_widget->OnSettingsChanged();
        for (auto& st : styles) {
            reset();
            set({{L"visualizer.style", st[0]}});
            big.Add(L"150%", st[1], frame());
        }
        big.Add(L"150% volume", L"64%", volume(64, false, L""));
        big.Save(out + L"\\visualizer_sheet_150.png", taskbarBg, 1.5f);
        g_testScale = 0;
        SetWindowPos(tb, nullptr, 0, 0, tr.right - tr.left, tr.bottom - tr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        reset();

        // A short title with Luca's settings (a wide mirrored visualizer).
        Sheet shortSheet;
        set({{L"visualizer.style", L"mirrored"}, {L"visualizer.width", L"100"}, {L"visualizer.bars", L"20"},
             {L"visualizer.peaks", L"1"}, {L"visualizer.sensitivity", L"80"}, {L"scrollVolume.step", L"1"}});
        auto shortSnap = SpotifySnap(art);
        shortSnap->title = L"My Sacrifice";
        shortSnap->artist = L"Creed";
        shortSnap->trackKey = L"short";
        show(shortSnap);
        auto vframe = [&](int level, float fill, float bump, bool muted = false) {
            int bars = GetSettings()->viz.bars;
            g_widget->DebugViz().DebugSet(FakeLevels(bars), FakeWave(64), 0.14f, 1.f);
            g_widget->DebugVolume(level, muted, L"", 1.f, fill, bump);
            g_widget->DebugRender(-1);
            Image img = WidgetImage();
            g_widget->DebugVolume(-1, false, L"", 0.f);
            return img;
        };
        shortSheet.Add(L"My Sacrifice", L"9%, resting", vframe(9, 9, 0.f));
        shortSheet.Add(L"My Sacrifice", L"9%, just scrolled", vframe(9, 8.6f, 1.f));
        shortSheet.Add(L"My Sacrifice", L"64%, half settled", vframe(64, 64, 0.5f));
        shortSheet.Add(L"My Sacrifice", L"100%", vframe(100, 100, 0.f));
        shortSheet.Add(L"My Sacrifice", L"muted", vframe(30, 30, 0.f, true));
        shortSheet.Save(out + L"\\volume_short_title.png", taskbarBg, g_widget->Scale());
        reset();
    }

    UiShutdown();
    g_tap.Shutdown();
    DestroyWindow(tb);
}

// The real visualizer with the sound playing on this PC; prints levels and CPU use.
static void AudioMode(const std::wstring& out, int seconds, const std::wstring& style, const std::wstring& source) {
    if (!style.empty()) HarnessSettings()[L"visualizer.style"] = style;
    if (!source.empty()) HarnessSettings()[L"visualizer.source"] = source;
    LoadSettings();
    InitTrace();
    HWND tb = CreateFakeTaskbar();
    g_taskbarHwnd = tb;
    UiInit(tb);
    if (!g_widget) return;
    auto s = SpotifySnap(SyntheticArt());
    s->deviceName = ThisComputerName();  // "playing on this PC"
    g_ui.SetSnapshot(s);
    g_widget->OnSnapshot();
    FILETIME c0, e0, k0, u0, c1, e1, k1, u1;
    GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
    ULONGLONG t0 = Tick();
    int framesSeen = 0;
    uint64_t lastSeq = 0;
    for (int sec = 0; sec < seconds; sec++) {
        Pump(1000);
        TapFrame f;
        g_tap.Read(f);
        int delta = (int)(f.seq - lastSeq);
        framesSeen += delta;
        lastSeq = f.seq;
        std::wstring bars;
        for (float b : f.bands) bars += std::to_wstring(std::clamp((int)std::lround(b * 9), 0, 9));
        long long age = f.lastSound ? (long long)(Tick() - f.lastSound) : -1;
        fwprintf(stderr, L"  %2ds capturing=%d last sound %5lld ms ago  levels 0-9 [%s]  frames/s=%d\n", sec + 1,
                 f.capturing, age, bars.c_str(), delta);
    }
    GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);
    auto ft = [](FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    double cpuMs = (ft(k1) - ft(k0) + ft(u1) - ft(u0)) / 10000.0;
    double wallMs = (double)(Tick() - t0);
    fwprintf(stderr, L"CPU: %.0f ms over %.0f ms = %.2f%% of one core (%d analysis frames)\n", cpuMs, wallMs,
             100.0 * cpuMs / wallMs, framesSeen);
    g_widget->DebugRender(-1);
    SaveImage(WidgetImage(), out + L"\\live_visualizer.png", 0x1B1B1F);
    UiShutdown();
    g_tap.Shutdown();
    DestroyWindow(tb);
}

// Restores the master mute state when it goes out of scope.
struct MasterMuteGuard {
    com_ptr<IAudioEndpointVolume> vol;
    BOOL was = FALSE;
    bool active = false;
    bool Mute() {
        auto dev = DefaultRenderDevice();
        if (!dev || FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, vol.put_void())))
            return false;
        vol->GetMute(&was);
        active = SUCCEEDED(vol->SetMute(TRUE, nullptr));
        return active;
    }
    ~MasterMuteGuard() {
        if (active) vol->SetMute(was, nullptr);
    }
};

// Loudness of what the PC plays right now (dBFS), measured for `ms`.
static float SystemLoudnessDb(int ms) {
    CaptureStream cs;
    std::wstring id;
    if (!cs.OpenSystem(id)) return -200;
    std::vector<float> mono;
    ULONGLONG end = Tick() + ms;
    while (Tick() < end) {
        cs.Read(mono);
        Sleep(20);
    }
    double sum = 0;
    for (float v : mono) sum += (double)v * v;
    return mono.empty() ? -200.f : 10.f * std::log10((float)(sum / mono.size()) + 1e-12f);
}

// Plays a tone from this process while the master output is muted (nothing
// is heard; Windows captures before the master volume) and checks what the
// visualizer sees, with "only the app shown" and with "everything".
static void ToneTest(const std::wstring& out) {
    float before = SystemLoudnessDb(600);
    if (before > -70) {
        fwprintf(stderr, L"something is playing (%.1f dB): not muting the PC, test skipped\n", before);
        return;
    }
    MasterMuteGuard mute;
    if (!mute.Mute()) {
        fwprintf(stderr, L"could not mute the output, test skipped\n");
        return;
    }
    fwprintf(stderr, L"master output muted for the test (was muted: %d)\n", mute.was);
    std::atomic<float> hz{1000.f};
    std::atomic<bool> stop{false}, started{false};
    std::thread render([&] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto dev = DefaultRenderDevice();
        com_ptr<IAudioClient> client;
        WAVEFORMATEX* fmt = nullptr;
        if (!dev || FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void())) ||
            FAILED(client->GetMixFormat(&fmt)) ||
            FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, fmt, nullptr))) {
            fwprintf(stderr, L"tone: render setup failed\n");
            CoUninitialize();
            return;
        }
        com_ptr<ISimpleAudioVolume> vol;
        client->GetService(__uuidof(ISimpleAudioVolume), vol.put_void());
        // Windows remembers per-app volume, so set it explicitly.
        if (!vol || FAILED(vol->SetMute(FALSE, nullptr)) || FAILED(vol->SetMasterVolume(1.f, nullptr))) {
            fwprintf(stderr, L"tone: could not set the app volume, not playing\n");
            CoUninitialize();
            return;
        }
        com_ptr<IAudioRenderClient> rc;
        client->GetService(__uuidof(IAudioRenderClient), rc.put_void());
        UINT32 bufFrames = 0;
        client->GetBufferSize(&bufFrames);
        bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                       (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)fmt)->SubFormat.Data1 == 3);
        int ch = fmt->nChannels, rate = fmt->nSamplesPerSec;
        fwprintf(stderr, L"tone: -9 dB sine, %d Hz, %d ch, float=%d\n", rate, ch, isFloat);
        client->Start();
        started = true;
        double phase = 0;
        while (!stop) {
            UINT32 pad = 0;
            client->GetCurrentPadding(&pad);
            UINT32 n = bufFrames - pad;
            BYTE* d = nullptr;
            if (n && SUCCEEDED(rc->GetBuffer(n, &d))) {
                for (UINT32 f = 0; f < n; f++) {
                    float v = 0.5f * (float)std::sin(phase);
                    phase += 6.283185307 * hz / rate;
                    for (int c = 0; c < ch; c++) {
                        if (isFloat) ((float*)d)[f * ch + c] = v;
                        else ((int16_t*)d)[f * ch + c] = (int16_t)(v * 32767);
                    }
                }
                rc->ReleaseBuffer(n, 0);
            }
            Sleep(10);
        }
        client->Stop();
        CoTaskMemFree(fmt);
        CoUninitialize();
    });
    for (int i = 0; i < 100 && !started; i++) Sleep(20);

    HarnessSettings()[L"visualizer.source"] = L"app";
    LoadSettings();
    InitTrace();
    HWND tb = CreateFakeTaskbar();
    g_taskbarHwnd = tb;
    UiInit(tb);
    auto s = std::make_shared<Snapshot>();
    s->visible = true;
    s->src = Src::Session;
    s->aumid = L"harness.exe";
    s->appName = L"Harness";
    s->title = L"Test tone";
    s->artist = L"1 kHz, then 100 Hz";
    s->trackKey = L"tone";
    s->playing = true;
    s->canPlayPause = true;
    g_ui.SetSnapshot(s);
    g_widget->OnSnapshot();
    auto measure = [&](const wchar_t* what, int wantBand) {
        Pump(1500);
        TapFrame f;
        g_tap.Read(f);
        std::wstring bars;
        for (float b : f.bands) bars += std::to_wstring(std::clamp((int)std::lround(b * 9), 0, 9));
        int loudest = f.bands.empty() ? -1
                                      : (int)(std::max_element(f.bands.begin(), f.bands.end()) - f.bands.begin());
        bool sound = f.lastSound && Tick() - f.lastSound < 300;
        bool ok = f.capturing && sound && loudest == wantBand;
        fwprintf(stderr, L"  %s %s: capturing=%d sound=%d levels [%s] loudest band %d (captured %.1f dB, %llu samples)\n",
                 ok ? L"ok  " : L"FAIL", what, f.capturing, sound, bars.c_str(), loudest, f.rmsDb,
                 (unsigned long long)g_tap.samplesRead.load());
        if (!ok) g_failures++;
    };
    fwprintf(stderr, L"[only the app shown (process loopback)]\n");
    uint64_t s0 = g_tap.samplesRead;
    ULONGLONG t0 = Tick();
    measure(L"1 kHz", 3);
    double rateSeen = (g_tap.samplesRead - s0) * 1000.0 / (Tick() - t0);
    fwprintf(stderr, L"  %s audio arrives at %.0f samples/s (48000 expected, minus start-up)\n",
             rateSeen > 30000 ? L"ok  " : L"FAIL", rateSeen);
    if (rateSeen <= 30000) g_failures++;
    hz = 100.f;
    measure(L"100 Hz", 0);
    hz = 6000.f;
    measure(L"6 kHz", 5);
    g_widget->DebugRender(-1);
    SaveImage(WidgetImage(), out + L"\\tone_visualizer.png", 0x1B1B1F);

    fwprintf(stderr, L"[everything on this PC (loopback)]\n");
    HarnessSettings()[L"visualizer.source"] = L"all";
    LoadSettings();
    g_widget->OnSettingsChanged();
    hz = 1000.f;
    measure(L"1 kHz", 3);
    {
        // CPU while animating at 60 fps (includes generating the tone).
        FILETIME c0, e0, k0, u0, c1, e1, k1, u1;
        GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
        uint64_t seq0;
        {
            TapFrame f;
            g_tap.Read(f);
            seq0 = f.seq;
        }
        ULONGLONG t0 = Tick();
        g_dbgAnalysisUs = g_dbgDrawUs = g_dbgPresentUs = 0;
        for (int i = 0; i < 6; i++) {
            hz = i % 2 ? 300.f : 2500.f;  // keep the bars moving
            Pump(500);
        }
        fwprintf(stderr, L"  per second: analysis %.1f ms, drawing %.1f ms, presenting %.1f ms\n",
                 g_dbgAnalysisUs / 3000.0, g_dbgDrawUs / 3000.0, g_dbgPresentUs / 3000.0);
        GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);
        TapFrame f;
        g_tap.Read(f);
        auto ft = [](FILETIME x) { return ((ULONGLONG)x.dwHighDateTime << 32) | x.dwLowDateTime; };
        double cpuMs = (ft(k1) - ft(k0) + ft(u1) - ft(u0)) / 10000.0, wall = (double)(Tick() - t0);
        fwprintf(stderr, L"  CPU while animating: %.2f%% of one core, %.0f frames/s\n", 100 * cpuMs / wall,
                 (f.seq - seq0) * 1000.0 / wall);
    }
    stop = true;
    render.join();
    Pump(2500);
    TapFrame f;
    g_tap.Read(f);
    bool quiet = !f.lastSound || Tick() - f.lastSound > 1500;
    fwprintf(stderr, L"  %s tone stopped: no sound for %lld ms\n", quiet ? L"ok  " : L"FAIL",
             f.lastSound ? (long long)(Tick() - f.lastSound) : -1LL);
    if (!quiet) g_failures++;
    UiShutdown();
    g_tap.Shutdown();
    DestroyWindow(tb);
    fwprintf(stderr, g_failures ? L"\n%d FAILURE(S)\n" : L"\nall tests passed\n", g_failures);
}

// Reads (does not change) the volume of the system and of some apps.
static void VolumeMode(const std::vector<std::wstring>& apps) {
    auto sys = NudgeSystemVolume(0);
    fwprintf(stderr, L"system: ok=%d level=%d muted=%d\n", sys.ok, sys.level, sys.muted);
    for (auto& a : apps) {
        auto id = ResolveAppAudio(a);
        std::wstring names;
        auto r = NudgeAppVolume(id, 0, &names);
        fwprintf(stderr, L"%-40s exe='%s' package='%s' root=%u -> ok=%d level=%d muted=%d sessions: %s\n", a.c_str(),
                 id.exe.c_str(), id.package.c_str(), id.rootPid, r.ok, r.level, r.muted, names.c_str());
    }
}

static void LiveMode(const std::wstring& out, int seconds) {
    HWND tb = CreateFakeTaskbar();
    Wh_ModInit();
    Pump(seconds * 1000);
    auto snap = LatestSnapshot();
    if (snap) {
        fwprintf(stderr, L"snapshot: visible=%d src=%d app='%s' title='%s' artist='%s' playing=%d pos=%lld dur=%lld art=%p\n",
                 snap->visible, (int)snap->src, snap->appName.c_str(), snap->title.c_str(), snap->artist.c_str(),
                 snap->playing, (long long)snap->PosNow(), (long long)snap->durMs, snap->art.get());
        fwprintf(stderr, L"controls: pp=%d next=%d prev=%d seek=%d shuffle=%d repeat=%d app=%p icon=%p\n",
                 snap->canPlayPause, snap->canNext, snap->canPrev, snap->canSeek, snap->canShuffle,
                 snap->canRepeat, snap->app.get(), snap->app ? snap->app->icon.get() : nullptr);
    } else {
        fwprintf(stderr, L"no snapshot\n");
    }
    if (g_widget && snap && snap->visible) {
        g_widget->DebugRender(-1);
        SavePng(g_widget->DebugSurface(), out + L"\\live_widget.png", 0x1B1B1F);
        if (g_popup && !snap->connectPrompt) {
            g_popup->DebugRender(g_widget->ScreenRect(), -1, false, L"");
            SavePng(g_popup->DebugSurface(), out + L"\\live_popup.png", 0x2E3440);
        }
    }
    Wh_ModUninit();
    DestroyWindow(tb);
    fwprintf(stderr, L"clean shutdown\n");
}

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) fwprintf(stderr, L"  ok   %S\n", #cond);                \
        else {                                                            \
            fwprintf(stderr, L"  FAIL %S (line %d)\n", #cond, __LINE__); \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

// A tiny local HTTP server with canned answers (the next one per request; the
// last one repeats), standing in for api.spotify.com.
struct FakeServer {
    SOCKET ls = INVALID_SOCKET;
    int port = 0;
    std::thread t;
    std::mutex m;
    std::vector<std::string> responses;
    std::atomic<int> hits{0};
    std::atomic<bool> stop{false};

    void Set(std::vector<std::string> v) {
        std::lock_guard<std::mutex> lock(m);
        responses = std::move(v);
        next = 0;
    }
    bool Start() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (ls == INVALID_SOCKET || bind(ls, (sockaddr*)&a, sizeof(a)) || listen(ls, 4)) return false;
        int len = sizeof(a);
        getsockname(ls, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        t = std::thread([this] {
            while (!stop) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(ls, &fds);
                timeval tv{0, 100000};
                if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
                SOCKET c = accept(ls, nullptr, nullptr);
                if (c == INVALID_SOCKET) continue;
                std::string req;
                char buf[4096];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    int n = recv(c, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    req.append(buf, n);
                }
                std::string resp;
                {
                    std::lock_guard<std::mutex> lock(m);
                    if (!responses.empty()) resp = responses[std::min(next++, responses.size() - 1)];
                }
                hits++;
                send(c, resp.data(), (int)resp.size(), 0);
                shutdown(c, SD_SEND);
                closesocket(c);
            }
        });
        return true;
    }
    void Stop() {
        stop = true;
        if (t.joinable()) t.join();
        if (ls != INVALID_SOCKET) closesocket(ls);
        WSACleanup();
    }

   private:
    size_t next = 0;
};

static std::string HttpResp(int status, const std::string& headers, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n" + headers + "\r\n" + body;
}

static void SelfTest() {
    fwprintf(stderr, L"[pkce]\n");
    {
        unsigned char h[32];
        Sha256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", h);
        CHECK(Base64Url(h, 32) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
        std::string t = RandomToken(64);
        CHECK(t.size() == 64);
    }
    fwprintf(stderr, L"[encoding]\n");
    {
        CHECK(UrlEncode("spotify:track:7a3 b") == "spotify%3Atrack%3A7a3%20b");
        CHECK(UrlDecode("a%3Ab+c") == "a:b c");
        auto q = ParseQuery("code=AQB%2Fx&state=xyz&empty");
        CHECK(q["code"] == "AQB/x" && q["state"] == "xyz" && q.count("empty"));
        std::string secret = "refresh-token-123";
        CHECK(UnprotectSecret(ProtectSecret(secret)) == secret);
        CHECK(JsonEscape("a\"b\\c") == "a\\\"b\\\\c");
    }
    fwprintf(stderr, L"[json]\n");
    {
        std::string body = R"({
          "device": {"id": "abc", "is_active": true, "is_private_session": false, "is_restricted": false,
                     "name": "Kitchen", "type": "Speaker", "volume_percent": 42, "supports_volume": true},
          "shuffle_state": true, "repeat_state": "track", "timestamp": 1700000000000,
          "context": null, "progress_ms": 61234, "is_playing": true,
          "item": {"album": {"name": "Album é", "images": [
                      {"url": "https://i.scdn.co/640", "width": 640, "height": 640},
                      {"url": "https://i.scdn.co/300", "width": 300, "height": 300},
                      {"url": "https://i.scdn.co/64", "width": 64, "height": 64}]},
                   "artists": [{"name": "A"}, {"name": "B 🎵"}],
                   "duration_ms": 200000, "id": "tid", "name": "Song", "type": "track", "uri": "spotify:track:tid"},
          "currently_playing_type": "track",
          "actions": {"disallows": {"resuming": true, "skipping_prev": true}}
        })";
        Json j;
        CHECK(Json::Parse(body, j));
        SpotifyState st = ParsePlayer(j);
        CHECK(st.ok && st.hasDevice && st.playing);
        CHECK(st.device.name == L"Kitchen" && st.device.volume == 42 && st.device.supportsVolume);
        CHECK(st.title == L"Song" && st.artist == L"A, B \U0001F3B5" && st.album == L"Album é");
        CHECK(st.imageUrl == L"https://i.scdn.co/640");
        CHECK(st.progressMs == 61234 && st.durationMs == 200000);
        CHECK(st.shuffle && st.repeat == RepeatMode::Track);
        CHECK(st.disResume && st.disPrev && !st.disNext);

        std::string ep = R"({"device":{"id":"d","name":"Phone","type":"Smartphone","volume_percent":null},
          "is_playing":false,"progress_ms":5,"currently_playing_type":"episode",
          "item":{"type":"episode","name":"Ep 1","id":"e1","uri":"spotify:episode:e1","duration_ms":9000,
                  "images":[],"show":{"name":"The Show","images":[{"url":"https://i/s","width":300}]}}})";
        Json je;
        CHECK(Json::Parse(ep, je));
        SpotifyState se = ParsePlayer(je);
        CHECK(se.type == "episode" && se.artist == L"The Show" && se.imageUrl == L"https://i/s");
        CHECK(!se.device.supportsVolume && se.device.volume == -1);

        Json ad;
        CHECK(Json::Parse(R"({"device":{"id":"x","name":"PC"},"currently_playing_type":"ad","item":null,"is_playing":true})", ad));
        SpotifyState sa = ParsePlayer(ad);
        CHECK(sa.type == "ad" && sa.title == L"Advertisement");

        Json bad;
        CHECK(!Json::Parse("{\"a\":", bad));
        CHECK(!Json::Parse("[1,2", bad));
        Json arr;
        CHECK(Json::Parse("[true,false]", arr) && arr.size() == 2 && arr[0].Bool() && !arr[1].Bool(true));
    }
    fwprintf(stderr, L"[poll pacing]\n");
    {
        PollPacer p;
        ULONGLONG t = 1000000;
        CHECK(p.percent == 100 && p.Scale(5000) == 5000);
        p.OnPoll(false, t);
        CHECK(p.percent == 100);                   // normal answers keep the pace
        p.OnPoll(true, t += 5000);
        CHECK(p.percent == 150 && p.Scale(5000) == 7500);  // a rate limit slows down
        p.OnPoll(false, t += 5000);
        CHECK(p.percent == 150);                   // waiting out Retry-After isn't a new limit
        for (int i = 0; i < 10; i++) p.OnPoll(true, t += 5000);
        CHECK(p.percent == 300);                   // capped at 3x
        p.OnPoll(false, t += 200000);
        CHECK(p.percent == 300);                   // not calm long enough yet
        p.OnPoll(false, t += 101000);
        CHECK(p.percent == 275);                   // 5 min without 429: a bit faster
        for (int i = 0; i < 30; i++) p.OnPoll(false, t += 300000);
        CHECK(p.percent == 100);                   // back to normal, never faster
    }
    fwprintf(stderr, L"[poll schedule]\n");
    {
        PollPacer pace;
        PollSituation s;
        s.hasDevice = s.playing = true;
        CHECK(PollDelay(s, pace) == 5000);          // playing: every 5 s
        s.popupOpen = true;
        CHECK(PollDelay(s, pace) == 2000);          // faster while the popup is open
        s.popupOpen = false;
        s.localSpotifyPlaying = true;
        CHECK(PollDelay(s, pace) == 15000);         // the local app reports changes itself
        s.localSpotifyPlaying = false;
        s.remainingMs = 2500;
        CHECK(PollDelay(s, pace) == 3300);          // right after the track should end
        s.remainingMs = 50;
        CHECK(PollDelay(s, pace) == 1000);          // never faster than 1 s
        s.remainingMs = 0;
        CHECK(PollDelay(s, pace) == 1500);          // ended, next track not reported yet
        s.endPolls = 3;
        CHECK(PollDelay(s, pace) == 5000);          // ...but only a few times
        PollSituation paused;
        paused.hasDevice = true;
        CHECK(PollDelay(paused, pace) == 15000);
        paused.pausedMs = 700000;
        CHECK(PollDelay(paused, pace) == 30000);
        paused.popupOpen = true;
        CHECK(PollDelay(paused, pace) == 3000);
        PollSituation idle;
        CHECK(PollDelay(idle, pace) == 60000);      // never had a device
        idle.sinceDeviceMs = 4000;
        CHECK(PollDelay(idle, pace) == 3000);       // a device was there a moment ago
        idle.sinceDeviceMs = 60000;
        CHECK(PollDelay(idle, pace) == 30000);
        PollSituation bad;
        bad.ok = false;
        bad.failures = 1;
        CHECK(PollDelay(bad, pace) == 3000);
        bad.failures = 20;
        CHECK(PollDelay(bad, pace) == 60000);
        pace.percent = 200;
        CHECK(PollDelay(s, pace) == 10000);         // slowed down after rate limits

        // One hour of music (3:33 tracks) on a Connect device, popup closed:
        // count the polls. The old code polled every second (3600 an hour).
        auto simulate = [](bool local) {
            PollPacer pacer;
            const int64_t dur = 213000;
            int64_t t = 0;
            int polls = 0;
            int64_t shownLateMs = 0, seenTrack = -1;
            while (t < 3600000) {
                polls++;
                int64_t track = t / dur, elapsed = t % dur;
                if (track != seenTrack) {
                    if (seenTrack >= 0) shownLateMs = std::max<int64_t>(shownLateMs, elapsed);
                    seenTrack = track;
                }
                PollSituation s;
                s.hasDevice = s.playing = true;
                s.localSpotifyPlaying = local;
                s.sinceDeviceMs = 0;
                s.remainingMs = dur - elapsed;
                t += PollDelay(s, pacer);
            }
            fwprintf(stderr, L"  %s: %d polls in an hour, track changes shown at most %lld ms late\n",
                     local ? L"local app playing" : L"Connect device", polls, (long long)shownLateMs);
            return std::make_pair(polls, shownLateMs);
        };
        auto remote = simulate(false);
        CHECK(remote.first < 800);                  // ~5x fewer requests than before
        CHECK(remote.second <= 1000);               // track changes still show within a second
        auto local = simulate(true);
        CHECK(local.first < 300);
    }
    fwprintf(stderr, L"[rate limit waits]\n");
    {
        CHECK(ApiErrorReason(R"({"error":{"status":429,"message":"Too many requests","reason":"QUOTA_EXCEEDED"}})") ==
              "QUOTA_EXCEEDED");
        CHECK(ApiErrorReason("").empty() && ApiErrorReason("Too many requests").empty());
        CHECK(RateLimitWaitMs(1, 3, false) == 3000);       // a short limit: as Spotify asks
        CHECK(RateLimitWaitMs(1, 0, false) == 5000);       // no Retry-After: 5 s
        CHECK(RateLimitWaitMs(5, 3, false) == 3000);
        CHECK(RateLimitWaitMs(6, 3, false) == 120000);     // keeps coming back: 2 min
        CHECK(RateLimitWaitMs(20, 3, false) == 1800000);   // ...up to 30 min
        CHECK(RateLimitWaitMs(1, 0, true) == 120000);      // quota used up: 2 min right away
        CHECK(RateLimitWaitMs(2, 0, true) == 300000);
        CHECK(RateLimitWaitMs(3, 0, true) == 600000);
        CHECK(RateLimitWaitMs(9, 0, true) == 1800000);
        CHECK(RateLimitWaitMs(1, 600, true) == 600000);    // a longer Retry-After wins
        CHECK(RateLimitWaitMs(1, 70000, true) == 1800000); // but Spotify is asked again within 30 min
    }
    fwprintf(stderr, L"[rate limit families]\n");
    {
        CHECK(FamilyOf(L"/me/player?additional_types=episode") == ApiFamily::Player);
        CHECK(FamilyOf(L"/me/player/next") == ApiFamily::Player);
        CHECK(FamilyOf(L"/me/library/contains?uris=x") == ApiFamily::Library);
        CHECK(FamilyOf(L"/me/library?uris=x") == ApiFamily::Library);
        // A 429 on the heart check must not block player requests.
        g_retryUntil[(int)ApiFamily::Library] = Tick() + 5000;
        auto lib = SpotifyApi(L"GET", L"/me/library/contains?uris=x");
        auto player = SpotifyApi(L"GET", L"/me/player");
        CHECK(lib.status == 429 && lib.throttled);
        CHECK(!player.throttled);
        g_retryUntil[(int)ApiFamily::Library] = 0;
        // Commands wait out a short Retry-After instead of being dropped.
        g_retryUntil[(int)ApiFamily::Player] = Tick() + 400;
        ULONGLONG t0 = Tick();
        WaitForRetryWindow(L"/me/player/next", 6000);
        ULONGLONG waited = Tick() - t0;
        fwprintf(stderr, L"  waited %llu ms\n", waited);
        CHECK(waited >= 350 && waited < 1000);
        CHECK(Tick() >= g_retryUntil[(int)ApiFamily::Player]);
        g_retryUntil[(int)ApiFamily::Player] = 0;
    }
    fwprintf(stderr, L"[spectrum]\n");
    {
        const int N = 2048;
        const float rate = 48000;
        Spectrum fft;
        fft.Prepare(N);
        BandLayout L;
        BuildBands(L, 6, 50, 10000, rate, N);
        // The bin weights cover each band exactly.
        bool covered = true;
        float ratio = std::pow(10000.f / 50.f, 1.f / 6);
        for (int b = 0; b < 6; b++) {
            float lo = 50 * std::pow(ratio, (float)b), sum = 0;
            for (auto& t : L.bands[b]) sum += t.weight * rate / N;
            if (std::fabs(sum - lo * (ratio - 1)) > 1.f) covered = false;
        }
        CHECK(covered);
        auto tone = [&](float hz, float amp) {
            std::vector<float> s(N), p, db(6);
            for (int i = 0; i < N; i++) s[i] = amp * std::sin(6.2831853f * hz * i / rate);
            fft.Power(s.data(), p);
            BandsDb(p, L, db.data());
            return db;
        };
        auto loudest = [](const std::vector<float>& db) { return (int)(std::max_element(db.begin(), db.end()) - db.begin()); };
        auto t1k = tone(1000, 1.f);
        fwprintf(stderr, L"  1 kHz full scale: %.1f %.1f %.1f %.1f %.1f %.1f dB\n", t1k[0], t1k[1], t1k[2], t1k[3],
                 t1k[4], t1k[5]);
        CHECK(loudest(t1k) == 3);                    // 710-1718 Hz
        CHECK(t1k[3] > -1.f && t1k[3] < 4.f);        // ~0 dB
        CHECK(t1k[0] < -40 && t1k[5] < -40);         // far bands stay quiet
        auto t100 = tone(100, 0.1f);
        CHECK(loudest(t100) == 0);                   // 50-121 Hz
        auto t6k = tone(6000, 0.1f);
        CHECK(loudest(t6k) == 5);                    // 4.2-10 kHz
        auto silent = tone(1000, 0.f);
        CHECK(silent[3] < -100);

        LevelMapper fixed;
        float lv[6];
        fixed.Map(t1k.data(), 6, 0.016f, false, 0.f, lv);
        CHECK(lv[3] == 1.f && lv[0] == 0.f);
        fixed.Map(silent.data(), 6, 0.016f, false, 0.f, lv);
        CHECK(lv[3] == 0.f);
        // Auto level: a quiet tone (-30 dB) soon reaches full height...
        LevelMapper autoL;
        auto quiet = tone(1000, 0.03f);
        for (int i = 0; i < 60; i++) autoL.Map(quiet.data(), 6, 0.016f, true, 0.f, lv);
        CHECK(lv[3] > 0.95f && lv[0] < 0.05f);
        // ...but silence isn't blown up to full height.
        for (int i = 0; i < 600; i++) autoL.Map(silent.data(), 6, 0.016f, true, 0.f, lv);
        CHECK(lv[3] == 0.f);
        // Sensitivity raises the bars.
        LevelMapper a2, a3;
        auto mid = tone(1000, 0.003f);
        float l1[6], l2[6];
        a2.Map(mid.data(), 6, 0.016f, false, 0.f, l1);
        a3.Map(mid.data(), 6, 0.016f, false, 20.f * std::log10(2.f), l2);
        CHECK(l2[3] > l1[3]);
        // 48 narrow bands: none is empty.
        BandLayout L48;
        BuildBands(L48, 48, 50, 10000, rate, N);
        bool none = true;
        for (auto& b : L48.bands) none &= !b.empty();
        CHECK(L48.count == 48 && none);
        // Other sample rates.
        BandLayout L44;
        BuildBands(L44, 6, 50, 22000, 44100, N);
        CHECK(L44.count == 6 && !L44.bands[5].empty());
    }
    fwprintf(stderr, L"[scroll steps]\n");
    {
        float acc = 0;
        CHECK(WheelSteps(acc, 120, 5) == 5);         // one notch
        CHECK(WheelSteps(acc, -120, 5) == -5);
        acc = 0;
        int total = 0;
        for (int i = 0; i < 3; i++) total += WheelSteps(acc, 40, 5);
        CHECK(total == 5);                           // touchpad: three thirds of a notch
        acc = 0;
        total = 0;
        for (int i = 0; i < 11; i++) total += WheelSteps(acc, 10, 1);
        CHECK(total == 0);                           // not a full notch yet at 1%
        total += WheelSteps(acc, 10, 1);
        CHECK(total == 1);
        acc = 0;
        CHECK(WheelSteps(acc, 60, 5) == 2);          // 2.5% -> 2, half a step is kept
        CHECK(WheelSteps(acc, 12, 5) == 1);
        CHECK(WheelSteps(acc, -60, 5) == -2);        // turning around drops the remainder
    }
    fwprintf(stderr, L"[app audio ids]\n");
    {
        CHECK(ResolveAppAudio(L"Spotify.exe").exe == L"spotify.exe");
        CHECK(ResolveAppAudio(L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify").exe == L"spotify.exe");
        auto store = ResolveAppAudio(L"AppleInc.AppleMusicWin_nzyj5cx40ttey!App");
        CHECK(store.package == L"AppleInc.AppleMusicWin_nzyj5cx40ttey" && store.exe.empty());
        CHECK(ResolveAppAudio(L"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe").exe == L"vlc.exe");
        auto guess = ResolveAppAudio(L"NoSuchPlayerXyz");
        CHECK(guess.exe == L"nosuchplayerxyz.exe" && guess.rootPid == 0);
        auto self = ResolveAppAudio(L"harness.exe");
        CHECK(self.rootPid == GetCurrentProcessId());
    }
    fwprintf(stderr, L"[new settings]\n");
    {
        LoadSettings();
        auto s = GetSettings();
        CHECK(s->scroll.enabled && !s->scroll.system && s->scroll.step == 5 && s->scroll.indicator);
        CHECK(s->viz.enabled && s->viz.style == Settings::Viz::Style::Bars && s->viz.width == 32 && s->viz.bars == 6);
        CHECK(s->viz.colorMode == Settings::Viz::Color::Accent && s->viz.color == 0xFF1ED760);
        CHECK(s->viz.whenSilent == Settings::Viz::Silent::Shrink && s->viz.silenceDelayMs == 2000);
        auto& h = HarnessSettings();
        h[L"scrollVolume.target"] = L"system";
        h[L"scrollVolume.step"] = L"90";
        h[L"visualizer.style"] = L"wave";
        h[L"visualizer.colorMode"] = L"rainbow";
        h[L"visualizer.color"] = L"#336699";
        h[L"visualizer.source"] = L"app";
        h[L"visualizer.whenSilent"] = L"rest";
        h[L"visualizer.bars"] = L"500";
        h[L"visualizer.minFrequency"] = L"5000";
        h[L"visualizer.maxFrequency"] = L"6000";
        LoadSettings();
        s = GetSettings();
        CHECK(s->scroll.system && s->scroll.step == 25);
        CHECK(s->viz.style == Settings::Viz::Style::Wave && s->viz.colorMode == Settings::Viz::Color::Rainbow);
        CHECK(s->viz.color == 0xFF336699 && s->viz.appOnly && s->viz.whenSilent == Settings::Viz::Silent::Rest);
        CHECK(s->viz.bars == 48 && s->viz.maxHz >= s->viz.minHz * 2);
        DefaultSettings();
        LoadSettings();
    }
    fwprintf(stderr, L"[network]\n");
    Http::Init();
    {
        auto r = Http::Request(L"GET", L"https://api.spotify.com/v1/me/player");
        fwprintf(stderr, L"  GET /me/player without a token -> %d\n", r.status);
        CHECK(r.status == 401);
    }
    fwprintf(stderr, L"[quota block (fake Spotify server)]\n");
    {
        FakeServer srv;
        CHECK(srv.Start());
        g_testApiBase = L"http://127.0.0.1:" + std::to_wstring(srv.port) + L"/v1";
        g_auth.Load(L"harness-quota-client");
        g_auth.Store("fake-access", 3600, "fake-refresh");
        const std::string quota = R"({"error":{"status":429,"message":"Too many requests","reason":"QUOTA_EXCEEDED"}})";
        const int P = (int)ApiFamily::Player;

        // What Spotify sent all day: 429 QUOTA_EXCEEDED without a Retry-After.
        srv.Set({HttpResp(429, "", quota)});
        auto r1 = SpotifyApi(L"GET", L"/me/player?additional_types=episode");
        CHECK(r1.status == 429 && !r1.throttled && srv.hits == 1);
        ULONGLONG left = RetryWaitLeftMs(ApiFamily::Player);
        fwprintf(stderr, L"  next request in %llu ms\n", left);
        CHECK(left > 110000 && left <= 120000);    // 2 min (the old code: 5 s, then every 30 s)
        auto r2 = SpotifyApi(L"GET", L"/me/player?additional_types=episode");
        CHECK(r2.throttled && srv.hits == 1);       // nothing is sent while blocked
        ULONGLONG t0 = Tick();
        auto cmd = SpotifyCommand(L"POST", L"/me/player/next");
        CHECK(cmd.throttled && Tick() - t0 < 500);  // commands don't hang during a long block
        // Each refusal in a row waits longer.
        g_retryUntil[P] = 0;
        SpotifyApi(L"GET", L"/me/player");
        left = RetryWaitLeftMs(ApiFamily::Player);
        CHECK(srv.hits == 2 && left > 290000 && left <= 300000);
        // The heart check (another family) still works.
        srv.Set({HttpResp(200, "", "[true]")});
        auto lib = SpotifyApi(L"GET", L"/me/library/contains?uris=spotify%3Atrack%3Ax");
        CHECK(lib.status == 200 && srv.hits == 3);
        // A short rate limit is waited out as asked; the header is kept as text.
        srv.Set({HttpResp(429, "Retry-After: 2\r\n", "")});
        g_retryUntil[P] = 0;
        g_limitStreak[P] = 0;
        auto p = SpotifyApi(L"GET", L"/me/player");
        CHECK(p.status == 429 && p.retryAfter == 2 && p.retryAfterText == L"2");
        left = RetryWaitLeftMs(ApiFamily::Player);
        CHECK(left > 1500 && left <= 2000);
        // A huge Retry-After: Spotify is still asked again within 30 min.
        srv.Set({HttpResp(429, "Retry-After: 68167\r\n", quota)});
        g_retryUntil[P] = 0;
        g_limitStreak[P] = 0;
        p = SpotifyApi(L"GET", L"/me/player");
        left = RetryWaitLeftMs(ApiFamily::Player);
        CHECK(p.retryAfter == 68167 && left > 1790000 && left <= 1800000);
        // Spotify answers again: back to normal.
        srv.Set({HttpResp(200, "", "{}")});
        g_retryUntil[P] = 0;
        p = SpotifyApi(L"GET", L"/me/player");
        CHECK(p.status == 200 && g_limitStreak[P] == 0 && RetryWaitLeftMs(ApiFamily::Player) == 0);

        g_testApiBase.clear();
        g_auth.Clear();
        for (int i = 0; i < 3; i++) {
            g_retryUntil[i] = 0;
            g_limitStreak[i] = 0;
        }
        srv.Stop();
    }
    fwprintf(stderr, L"[sign-in listener]\n");
    {
        std::mutex m;
        std::condition_variable cv;
        std::string openedUrl;
        g_testOpenUrl = [&](const std::string& url) {
            std::lock_guard<std::mutex> lock(m);
            openedUrl = url;
            cv.notify_all();
        };
        g_auth.Load(L"harness-test-client");
        g_authCancel = false;
        std::thread t(RunAuthFlow, std::wstring(L"harness-test-client"), 8974);
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, std::chrono::seconds(5), [&] { return !openedUrl.empty(); });
        }
        CHECK(openedUrl.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A8974%2Fcallback") != std::string::npos);
        CHECK(openedUrl.find("code_challenge_method=S256") != std::string::npos);
        size_t sp = openedUrl.find("&state=");
        std::string state = sp == std::string::npos ? "" : openedUrl.substr(sp + 7);
        auto wrong = Http::Request(L"GET", L"http://127.0.0.1:8974/nope");
        CHECK(wrong.status == 404);
        auto cb = Http::Request(L"GET", ToWide("http://127.0.0.1:8974/callback?code=fake-code&state=" + state));
        CHECK(cb.status == 200 && cb.body.find("You're connected") != std::string::npos);
        t.join();
        CHECK(!g_auth.HasToken());
        g_testOpenUrl = nullptr;
    }
    fwprintf(stderr, L"[remote feed]\n");
    {
        g_feed.Start();
        Snapshot s;
        s.visible = true;
        s.src = Src::Spotify;
        s.appName = L"Spotify";
        s.title = L"Song";
        s.artist = L"Artist";
        s.deviceName = L"Kitchen";
        s.artUrl = L"https://i.scdn.co/image/abc";
        s.playing = true;
        s.posMs = 1000;
        s.posTick = Tick();
        s.durMs = 200000;
        s.canNext = true;
        s.canPlayPause = true;
        g_feed.Write(&s);

        // Read it the way another mod does.
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kFeedMappingName);
        CHECK(mapping != nullptr);
        FeedRecord* r = mapping ? (FeedRecord*)MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                                             sizeof(FeedRecord))
                                : nullptr;
        CHECK(r && r->magic == kFeedMagic && (r->seq & 1) == 0);
        CHECK(r && r->source == 1 && r->playing == 1 && r->controls == 6);
        CHECK(r && std::wstring(r->title) == L"Song" && std::wstring(r->artist) == L"Artist");
        CHECK(r && std::wstring(r->device) == L"Kitchen" && std::wstring(r->app) == L"Spotify");
        CHECK(r && std::wstring(r->artUrl) == L"https://i.scdn.co/image/abc");
        CHECK(r && r->durMs == 200000 && r->posMs >= 1000 && r->posMs < 3000);
        CHECK(r && r->stampMs > 1700000000000ULL && r->posStampMs == r->stampMs);

        // A long title is cut, not overflowed.
        s.title = std::wstring(400, L'x');
        g_feed.Write(&s);
        CHECK(r && wcslen(r->title) == 255 && std::wstring(r->artist) == L"Artist");

        // A command is consumed by the feed thread.
        if (r) {
            InterlockedExchange(&r->command, kFeedNext);
            HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kFeedEventName);
            CHECK(ev != nullptr);
            if (ev) {
                SetEvent(ev);
                CloseHandle(ev);
            }
            for (int i = 0; i < 100 && r->command != kFeedNone; i++) Sleep(10);
            CHECK(r->command == kFeedNone);
        }

        // The connect prompt and a hidden widget are not playback.
        s.connectPrompt = true;
        g_feed.Write(&s);
        CHECK(r && r->source == 0 && r->title[0] == 0);
        g_feed.Stop();
        CHECK(r && r->source == 0);
        if (r) UnmapViewOfFile(r);
        if (mapping) CloseHandle(mapping);
    }
    Http::Cleanup();
    fwprintf(stderr, g_failures ? L"\n%d FAILURE(S)\n" : L"\nall tests passed\n", g_failures);
}

// Publishes a media session from this process and changes it the way a browser
// does while scrolling YouTube, then checks what the widget would have shown.
static void ChurnTest() {
    namespace wm = winrt::Windows::Media;
    SetCurrentProcessExplicitAppUserModelID(L"TaskbarPlayer.Harness");
    HWND tb = CreateFakeTaskbar();
    HWND w = CreateWindowExW(0, L"TP_FakeTaskbar", L"Harness Test", WS_OVERLAPPED, 0, 0, 10, 10, nullptr,
                             nullptr, GetModuleHandleW(nullptr), nullptr);
    struct Ev {
        ULONGLONG t;
        bool visible;
        std::wstring title;
    };
    std::mutex m;
    std::vector<Ev> events;
    g_testOnlyAumid = L"harness";
    g_testSnapHook = [&](const Snapshot& s) {
        std::lock_guard<std::mutex> lock(m);
        events.push_back({Tick(), s.visible, s.title});
    };
    Wh_ModInit();
    Pump(500);

    auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
    wm::SystemMediaTransportControls smtc{nullptr};
    winrt::check_hresult(interop->GetForWindow(w, winrt::guid_of<wm::SystemMediaTransportControls>(), winrt::put_abi(smtc)));
    smtc.IsEnabled(true);
    smtc.IsPlayEnabled(true);
    smtc.IsPauseEnabled(true);
    smtc.IsNextEnabled(true);
    auto setTitle = [&](const wchar_t* t) {
        auto du = smtc.DisplayUpdater();
        du.Type(wm::MediaPlaybackType::Music);
        du.MusicProperties().Title(t);
        du.MusicProperties().Artist(L"Test Channel");
        du.Update();
    };
    // Returns the titles that were shown (visible) between two moments.
    auto shownBetween = [&](ULONGLONG a, ULONGLONG b) {
        std::lock_guard<std::mutex> lock(m);
        std::set<std::wstring> titles;
        bool hidden = false;
        for (auto& e : events)
            if (e.t >= a && e.t <= b) {
                if (e.visible) titles.insert(e.title);
                else hidden = true;
            }
        if (hidden) titles.insert(L"<hidden>");
        return titles;
    };
    auto lastShown = [&] {
        std::lock_guard<std::mutex> lock(m);
        return events.empty() ? std::wstring(L"<none>") : (events.back().visible ? events.back().title : L"<hidden>");
    };
    auto print = [](const std::set<std::wstring>& s) {
        std::wstring out;
        for (auto& t : s) out += L"'" + t + L"' ";
        return out;
    };

    fwprintf(stderr, L"[new session]\n");
    setTitle(L"Video A");
    smtc.PlaybackStatus(wm::MediaPlaybackStatus::Playing);
    ULONGLONG t0 = Tick();
    Pump(1000);
    auto early = shownBetween(t0, Tick());
    fwprintf(stderr, L"  first second: %s\n", print(early).c_str());
    CHECK(!early.count(L"Video A"));  // not yet: must play for a moment first
    Pump(1500);
    CHECK(lastShown() == L"Video A");

    fwprintf(stderr, L"[hover previews while scrolling]\n");
    ULONGLONG t1 = Tick();
    const wchar_t* previews[] = {L"Preview 1", L"Preview 2", L"Preview 3", L"Preview 4", L"Preview 5", L"Preview 6"};
    for (auto p : previews) {
        setTitle(p);
        Pump(180);
    }
    setTitle(L"Video A");
    Pump(2000);
    auto churn = shownBetween(t1, Tick());
    fwprintf(stderr, L"  shown during churn: %s\n", print(churn).c_str());
    CHECK(churn.size() == 1 && churn.count(L"Video A"));

    fwprintf(stderr, L"[real change]\n");
    ULONGLONG t2 = Tick();
    setTitle(L"Video B");
    Pump(2500);
    ULONGLONG firstB = 0;
    {
        std::lock_guard<std::mutex> lock(m);
        for (auto& e : events)
            if (e.t >= t2 && e.visible && e.title == L"Video B") {
                firstB = e.t;
                break;
            }
    }
    fwprintf(stderr, L"  'Video B' shown after %lld ms\n", firstB ? (long long)(firstB - t2) : -1LL);
    CHECK(firstB && firstB - t2 >= 900 && firstB - t2 < 2000);

    fwprintf(stderr, L"[brief stop]\n");
    ULONGLONG t3 = Tick();
    smtc.PlaybackStatus(wm::MediaPlaybackStatus::Stopped);
    Pump(400);
    smtc.PlaybackStatus(wm::MediaPlaybackStatus::Playing);
    Pump(2500);
    auto blip = shownBetween(t3, Tick());
    fwprintf(stderr, L"  shown around the stop: %s\n", print(blip).c_str());
    {
        std::lock_guard<std::mutex> lock(m);
        for (auto& e : events)
            if (e.t >= t3) fwprintf(stderr, L"    +%lld visible=%d %s\n", (long long)(e.t - t3), e.visible, e.title.c_str());
    }
    CHECK(!blip.count(L"<hidden>"));

    fwprintf(stderr, L"[real stop]\n");
    smtc.PlaybackStatus(wm::MediaPlaybackStatus::Stopped);
    Pump(2500);
    CHECK(lastShown() == L"<hidden>");

    smtc.IsEnabled(false);
    smtc = nullptr;
    g_testSnapHook = nullptr;
    Wh_ModUninit();
    DestroyWindow(w);
    DestroyWindow(tb);
    fwprintf(stderr, g_failures ? L"\n%d FAILURE(S)\n" : L"\nall tests passed\n", g_failures);
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 3) {
        fwprintf(stderr, L"usage: harness render|live <outDir> ...\n");
        return 1;
    }
    std::wstring mode = argv[1], out = argv[2];
    CreateDirectoryW(out.c_str(), nullptr);
    HarnessStorePath() = out + L"\\store.ini";
    DefaultSettings();
    if (mode == L"render") {
        RenderMode(out, argc > 3 ? argv[3] : L"", argc > 6 ? _wtoi(argv[4]) : 0, argc > 6 ? _wtoi(argv[5]) : 0,
                   argc > 6 ? _wtoi(argv[6]) : 0);
    } else if (mode == L"live") {
        LiveMode(out, argc > 3 ? _wtoi(argv[3]) : 6);
    } else if (mode == L"selftest") {
        SelfTest();
    } else if (mode == L"audio") {
        AudioMode(out, argc > 3 ? _wtoi(argv[3]) : 8, argc > 4 ? argv[4] : L"", argc > 5 ? argv[5] : L"");
    } else if (mode == L"tonetest") {
        ToneTest(out);
    } else if (mode == L"volume") {
        std::vector<std::wstring> apps;
        for (int i = 3; i < argc; i++) apps.push_back(argv[i]);
        if (apps.empty()) apps = {L"Spotify.exe", L"Brave", L"Chrome", L"MSEdge"};
        VolumeMode(apps);
    } else if (mode == L"churn") {
        ChurnTest();
    } else if (mode == L"blips") {
        // Spotify answers that come and go must not make the widget flicker.
        HarnessSettings()[L"spotifyClientId"] = L"harness-client";
        LoadSettings();
        Wh_SetStringValue(L"spotifyClient", L"harness-client");
        Wh_SetStringValue(L"spotifyToken", ToWide(ProtectSecret("fake-refresh-token")).c_str());
        g_auth.Load(L"harness-client");
        bool visible = false;
        Src src = Src::None;
        g_testSnapHook = [&](const Snapshot& s) {
            visible = s.visible;
            src = s.src;
        };
        auto state = [](bool ok, bool device, bool item) {
            SpotifyState st;
            st.ok = ok;
            st.hasDevice = device;
            if (device) st.device.name = L"Luca's Echo Show";
            if (item) {
                st.title = L"See You Next Year";
                st.artist = L"Arimasen";
                st.uri = L"spotify:track:x";
                st.type = "track";
                st.durationMs = 195000;
            }
            st.playing = item;
            st.tick = Tick();
            return st;
        };
        auto check = [&](const wchar_t* what, bool wantVisible) {
            bool ok = visible == wantVisible && (!wantVisible || src == Src::Spotify);
            fwprintf(stderr, L"  %s %-55s visible=%d\n", ok ? L"ok  " : L"FAIL", what, visible);
            if (!ok) g_failures++;
        };
        g_controller.OnSpotify(state(true, true, true));
        check(L"playing on the Echo Show", true);
        g_controller.OnSpotify(state(false, false, false));
        Sleep(2500);  // longer than the 1.5 s grace: the old code hid the widget here
        g_controller.Publish();
        check(L"a failed request, 2.5 s later", true);
        g_controller.OnSpotify(state(true, true, true));
        g_controller.OnSpotify(state(true, false, false));
        Sleep(2500);
        g_controller.Publish();
        check(L"a 'no device' answer, 2.5 s later", true);
        g_controller.OnSpotify(state(true, true, false));
        check(L"brief 'nothing playing' answer", true);
        g_controller.OnSpotify(state(true, true, true));
        check(L"playing again", true);
        Sleep(6300);
        g_controller.OnSpotify(state(true, false, false));
        g_controller.Publish();
        Sleep(1700);  // past the short grace period
        g_controller.Publish();
        check(L"'no device' that persists for 6+ s hides the widget", false);
        g_testSnapHook = nullptr;
        fwprintf(stderr, g_failures ? L"\n%d FAILURE(S)\n" : L"\nall tests passed\n", g_failures);
    } else if (mode == L"flip") {
        // Spotify paused in the background + a browser session that is
        // re-created, paused and closed: the widget must not jump around.
        namespace wm = winrt::Windows::Media;
        SetCurrentProcessExplicitAppUserModelID(L"TaskbarPlayer.Harness");
        HarnessSettings()[L"spotifyClientId"] = L"harness-client";
        LoadSettings();
        Wh_SetStringValue(L"spotifyClient", L"harness-client");
        Wh_SetStringValue(L"spotifyToken", ToWide(ProtectSecret("fake-refresh-token")).c_str());
        g_testOnlyAumid = L"harness";
        std::mutex m;
        std::vector<std::pair<ULONGLONG, Src>> events;
        g_testSnapHook = [&](const Snapshot& s) {
            std::lock_guard<std::mutex> lock(m);
            events.push_back({Tick(), s.visible ? s.src : Src::None});
        };
        auto srcsSince = [&](ULONGLONG t) {
            std::lock_guard<std::mutex> lock(m);
            std::set<int> out;
            for (auto& e : events)
                if (e.first >= t) out.insert((int)e.second);
            return out;
        };
        auto last = [&] {
            std::lock_guard<std::mutex> lock(m);
            return events.empty() ? Src::None : events.back().second;
        };
        auto name = [](Src s) { return s == Src::Spotify ? L"Spotify" : s == Src::Session ? L"browser" : L"none"; };
        g_core.Start();  // only the core: no real Spotify requests
        g_core.Sync([] { g_controller.Init(); });
        SpotifyState paused;
        paused.ok = true;
        paused.hasDevice = true;
        paused.device.name = L"TX-NR636";
        paused.title = L"Paused song";
        paused.artist = L"Artist";
        paused.uri = L"spotify:track:p";
        paused.type = "track";
        paused.tick = Tick();
        g_core.Sync([paused] { g_controller.OnSpotify(paused); });
        Pump(300);
        auto check = [&](const wchar_t* what, bool ok) {
            fwprintf(stderr, L"  %s %s\n", ok ? L"ok  " : L"FAIL", what);
            if (!ok) g_failures++;
        };
        check(L"Spotify (paused) is shown", last() == Src::Spotify);

        HWND w = CreateWindowExW(0, L"STATIC", L"Harness Test", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        wm::SystemMediaTransportControls smtc{nullptr};
        winrt::check_hresult(interop->GetForWindow(w, winrt::guid_of<wm::SystemMediaTransportControls>(), winrt::put_abi(smtc)));
        auto enable = [&](bool on) {
            smtc.IsEnabled(on);
            if (!on) return;
            smtc.IsPlayEnabled(true);
            smtc.IsPauseEnabled(true);
            auto du = smtc.DisplayUpdater();
            du.Type(wm::MediaPlaybackType::Music);
            du.MusicProperties().Title(L"A video");
            du.MusicProperties().Artist(L"Channel");
            du.Update();
        };
        enable(true);
        smtc.PlaybackStatus(wm::MediaPlaybackStatus::Playing);
        Pump(2500);
        check(L"a playing browser video takes over", last() == Src::Session);

        ULONGLONG t1 = Tick();
        enable(false);  // the browser drops its session...
        Pump(300);
        enable(true);   // ...and re-creates it
        smtc.PlaybackStatus(wm::MediaPlaybackStatus::Playing);
        Pump(2500);
        auto during = srcsSince(t1);
        std::wstring seen;
        for (int s : during) seen += std::wstring(name((Src)s)) + L" ";
        fwprintf(stderr, L"  shown while the session was re-created: %s\n", seen.c_str());
        check(L"no jump to Spotify while the browser re-creates its session", !during.count((int)Src::Spotify));

        smtc.PlaybackStatus(wm::MediaPlaybackStatus::Paused);
        Pump(2000);
        check(L"pausing the video keeps the video shown", last() == Src::Session);

        enable(false);  // closed for good
        Pump(3500);
        check(L"closing it for good falls back to Spotify", last() == Src::Spotify);

        smtc = nullptr;
        g_testSnapHook = nullptr;
        g_core.Sync([] { g_controller.Shutdown(); });
        g_core.Stop();
        DestroyWindow(w);
        fwprintf(stderr, g_failures ? L"\n%d FAILURE(S)\n" : L"\nall tests passed\n", g_failures);
    } else if (mode == L"exittest") {
        // Like a helper explorer.exe: the mod starts, never finds a taskbar
        // (the class doesn't exist here) and the process exits without
        // Wh_ModUninit. Static destructors run in exit(); this must not abort.
        Wh_ModInit();
        Sleep(1500);
        fwprintf(stderr, L"exiting without uninit\n");
        fflush(stderr);
        exit(0);
    }
    {
        std::lock_guard<std::mutex> lock(g_wicMutex);
        g_wic = nullptr;
    }
    CoUninitialize();
    return 0;
}
