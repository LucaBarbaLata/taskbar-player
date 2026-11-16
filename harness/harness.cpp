// Test harness: runs the mod's UI outside explorer and saves renders as PNG files.
//   harness.exe render <outDir> <image> [cropX cropY cropSize]
//   harness.exe live <outDir> <seconds>
#include "shim.h"
#include <shellscalingapi.h>

#include "../taskbar-player.wh.cpp"

#include <systemmediatransportcontrolsinterop.h>

using namespace tp;

static bool SavePng(const Surface& s, const std::wstring& path, uint32_t bg) {
    int w = s.w, h = s.h;
    const BYTE* src = (const BYTE*)s.Bits();
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
    auto art = LoadArt(image, cx, cy, cs);
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

    UiShutdown();
    DestroyWindow(tb);
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

static int g_failures = 0;
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
