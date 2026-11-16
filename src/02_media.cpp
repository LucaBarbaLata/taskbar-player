
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

// Finding an app's main window by its AppUserModelID (or exe name).
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
