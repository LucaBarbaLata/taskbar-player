
namespace tp {

// ============================================================================
// Audio: spectrum analysis, app volume, loopback capture and the visualizer
// ============================================================================

constexpr UINT WM_TP_VIZFRAME = WM_APP + 0x354;  // the audio tap has a new frame
constexpr UINT WM_TP_VOLUME = WM_APP + 0x355;    // a volume change finished (wParam: level | flags | before << 16)
constexpr WPARAM kVolMuted = 0x100, kVolSystem = 0x200, kVolFailed = 0x400, kVolHasBefore = 0x800;

#ifdef TP_HARNESS
// Where the time goes (test harness only), in microseconds.
static std::atomic<uint64_t> g_dbgAnalysisUs{0}, g_dbgDrawUs{0}, g_dbgPresentUs{0};
static uint64_t DbgNowUs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 1000000 / f.QuadPart);
}
#endif

// ---------------------------------------------------------------------------
// Spectrum analysis (pure functions, covered by the harness self-test)
// ---------------------------------------------------------------------------

// Radix-2 FFT of a Hann-windowed block of samples.
class Spectrum {
   public:
    // `n` must be a power of two.
    void Prepare(int n) {
        if (n == n_) return;
        n_ = n;
        window_.resize(n);
        re_.resize(n);
        im_.resize(n);
        cos_.resize(n / 2);
        sin_.resize(n / 2);
        rev_.resize(n);
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < n; i++) window_[i] = (float)(0.5 - 0.5 * std::cos(2 * pi * i / n));
        for (int i = 0; i < n / 2; i++) {
            cos_[i] = (float)std::cos(2 * pi * i / n);
            sin_[i] = (float)-std::sin(2 * pi * i / n);
        }
        int bits = 0;
        while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++) {
            int r = 0;
            for (int b = 0; b < bits; b++)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev_[i] = r;
        }
    }
    int Size() const { return n_; }

    // Power of bins 0..n/2, scaled so that a full-scale sine gives ~1 in its bin.
    void Power(const float* samples, std::vector<float>& out) {
        int n = n_;
        for (int i = 0; i < n; i++) {
            re_[rev_[i]] = samples[i] * window_[i];
            im_[rev_[i]] = 0;
        }
        for (int len = 2; len <= n; len <<= 1) {
            int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int j = 0; j < half; j++) {
                    float wr = cos_[j * step], wi = sin_[j * step];
                    int a = i + j, b = a + half;
                    float tr = re_[b] * wr - im_[b] * wi;
                    float ti = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - tr;
                    im_[b] = im_[a] - ti;
                    re_[a] += tr;
                    im_[a] += ti;
                }
            }
        }
        out.resize(n / 2 + 1);
        float norm = 4.f / n;  // a full-scale sine with a Hann window peaks at n/4
        for (int k = 0; k <= n / 2; k++) {
            float r = re_[k] * norm, i = im_[k] * norm;
            out[k] = r * r + i * i;
        }
    }

   private:
    int n_ = 0;
    std::vector<float> window_, re_, im_, cos_, sin_;
    std::vector<int> rev_;
};

// How much each FFT bin contributes to each (logarithmically spaced) band.
struct BandLayout {
    struct Tap {
        int bin;
        float weight;
    };
    std::vector<std::vector<Tap>> bands;
    std::vector<float> tilt;  // power weight per band (brightens the treble)
    int count = 0, fftN = 0, minHz = 0, maxHz = 0;
    float rate = 0;
};

// Music has less energy in the treble; this evens the bars out.
constexpr float kTiltDbPerOctave = 2.5f;

static void BuildBands(BandLayout& L, int count, int minHz, int maxHz, float rate, int fftN) {
    L = BandLayout();
    L.count = count;
    L.fftN = fftN;
    L.minHz = minHz;
    L.maxHz = maxHz;
    L.rate = rate;
    float df = rate / fftN;
    int maxBin = fftN / 2;
    float lo0 = (float)minHz, hiN = std::min((float)maxHz, rate / 2 * 0.98f);
    if (hiN <= lo0 * 1.1f) hiN = lo0 * 2;
    float ratio = std::pow(hiN / lo0, 1.f / count);
    L.bands.resize(count);
    L.tilt.resize(count);
    for (int b = 0; b < count; b++) {
        float lo = lo0 * std::pow(ratio, (float)b), hi = lo * ratio;
        int k0 = std::max(1, (int)std::floor(lo / df - 0.5f));
        int k1 = std::min(maxBin, (int)std::ceil(hi / df + 0.5f));
        for (int k = k0; k <= k1; k++) {
            float binLo = (k - 0.5f) * df, binHi = (k + 0.5f) * df;
            float overlap = std::min(hi, binHi) - std::max(lo, binLo);
            if (overlap > 0) L.bands[b].push_back({k, overlap / df});
        }
        float fc = std::sqrt(lo * hi);
        L.tilt[b] = std::pow(fc / 1000.f, kTiltDbPerOctave / 3.0103f);
    }
}

// Band power in dB (0 dB = a full-scale sine).
static void BandsDb(const std::vector<float>& power, const BandLayout& L, float* outDb) {
    for (int b = 0; b < L.count; b++) {
        double p = 0;
        for (auto& t : L.bands[b])
            if (t.bin < (int)power.size()) p += power[t.bin] * t.weight;
        outDb[b] = 10.f * std::log10((float)(p * L.tilt[b]) + 1e-12f);
    }
}

// Turns band dB values into bar heights (0..1), optionally following the
// loudness of the music ("auto level").
struct LevelMapper {
    static constexpr float kRangeDb = 36.f;     // a bar spans this many dB
    static constexpr float kFixedTopDb = -8.f;  // full height without auto level
    static constexpr float kAutoFloorDb = -52.f;
    float topDb = kAutoFloorDb;

    void Map(const float* db, int n, float dtSec, bool autoLevel, float gainDb, float* out) {
        float top = kFixedTopDb;
        if (autoLevel) {
            float frameMax = -200;
            for (int i = 0; i < n; i++) frameMax = std::max(frameMax, db[i]);
            if (frameMax > topDb) topDb += (frameMax - topDb) * std::min(1.f, dtSec / 0.06f);
            else topDb = std::max(frameMax, topDb - 5.f * dtSec);  // recover 5 dB per second
            topDb = std::max(topDb, kAutoFloorDb);
            top = topDb;
        }
        for (int i = 0; i < n; i++) out[i] = Clamp01((db[i] + gainDb - (top - kRangeDb)) / kRangeDb);
    }
};

// Wheel notches (120 per notch; touchpads send smaller steps) into whole
// percent steps. Returns the change; the remainder stays in `acc`.
static int WheelSteps(float& acc, int delta, int stepPercent) {
    if (delta == 0 || stepPercent <= 0) return 0;
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;  // direction changed
    acc += (float)delta;
    float units = acc * stepPercent / 120.f;
    int change = (int)units;  // toward zero
    acc -= change * 120.f / stepPercent;
    return change;
}

// ---------------------------------------------------------------------------
// Which processes belong to an app (for its volume and for capturing it)
// ---------------------------------------------------------------------------

struct ProcEntry {
    DWORD pid = 0, parent = 0;
    std::wstring exe;  // lowercase file name
};

static std::vector<ProcEntry> ListProcesses() {
    std::vector<ProcEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        out.push_back(ProcEntry{pe.th32ProcessID, pe.th32ParentProcessID, Lower(pe.szExeFile)});
    CloseHandle(snap);
    return out;
}

static std::wstring PackageFamilyOf(DWORD pid) {
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t name[128];  // PACKAGE_FAMILY_NAME_MAX_LENGTH is 64 + 1 + 13
    UINT32 len = ARRAYSIZE(name);
    std::wstring out;
    if (GetPackageFamilyName(proc, &len, name) == ERROR_SUCCESS) out = name;
    CloseHandle(proc);
    return out;
}

struct AppAudioId {
    std::wstring exe;      // lowercase exe file name, e.g. "brave.exe"
    std::wstring package;  // package family name of Store apps
    DWORD rootPid = 0;     // the app's top process (its sound includes its children)

    bool Valid() const { return !exe.empty() || !package.empty(); }
    bool Matches(const ProcEntry& p) const {
        if (!exe.empty() && p.exe == exe) return true;
        return !package.empty() && IEquals(PackageFamilyOf(p.pid), package);
    }
};

// Maps a media session's AppUserModelID to the app's processes.
static AppAudioId ResolveAppAudio(const std::wstring& aumid) {
    AppAudioId id;
    if (aumid.empty()) return id;
    auto procs = ListProcesses();
    auto exeOf = [&](DWORD pid) -> std::wstring {
        for (auto& p : procs)
            if (p.pid == pid) return p.exe;
        return {};
    };
    size_t bang = aumid.find(L'!');
    if (IContains(aumid, L"spotify")) {
        id.exe = L"spotify.exe";
    } else if (bang != std::wstring::npos) {
        id.package = aumid.substr(0, bang);
    } else if (IContains(aumid, L".exe")) {
        id.exe = Lower(aumid.substr(aumid.find_last_of(L"\\/") + 1));
    } else {
        // Browsers use their own IDs ("Chrome", "MSEdge", "Brave", a hash
        // for Firefox): find the window that carries it.
        FindWindowCtx ctx;
        ctx.aumid = aumid;
        EnumWindows(FindAppWindowProc, (LPARAM)&ctx);
        if (ctx.found) {
            DWORD pid = 0;
            GetWindowThreadProcessId(ctx.found, &pid);
            id.exe = exeOf(pid);
            id.rootPid = pid;
        }
        if (id.exe.empty()) id.exe = Lower(aumid) + L".exe";
    }
    if (!id.rootPid) {
        // The top process of the app: its parent isn't part of the app.
        for (auto& p : procs) {
            if (!id.Matches(p)) continue;
            bool parentMatches = false;
            for (auto& q : procs)
                if (q.pid == p.parent) parentMatches = id.Matches(q);
            if (!parentMatches) {
                id.rootPid = p.pid;
                break;
            }
        }
    }
    return id;
}

// ---------------------------------------------------------------------------
// Volume: the Windows master volume or one app's Volume Mixer slider
// ---------------------------------------------------------------------------

static const CLSID kCLSID_MMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};

static com_ptr<IMMDevice> DefaultRenderDevice() {
    com_ptr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                en.put_void())))
        return nullptr;
    com_ptr<IMMDevice> dev;
    en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put());
    return dev;
}

static std::wstring DeviceId(IMMDevice* dev) {
    if (!dev) return {};
    LPWSTR id = nullptr;
    std::wstring out;
    if (SUCCEEDED(dev->GetId(&id)) && id) out = id;
    CoTaskMemFree(id);
    return out;
}

struct VolumeResult {
    bool ok = false;
    int level = -1;   // percent
    int before = -1;  // percent before the change
    bool muted = false;
};

static VolumeResult NudgeSystemVolume(int delta) {
    VolumeResult r;
    auto dev = DefaultRenderDevice();
    com_ptr<IAudioEndpointVolume> vol;
    if (!dev || FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, vol.put_void())))
        return r;
    float cur = 0;
    BOOL mute = FALSE;
    if (FAILED(vol->GetMasterVolumeLevelScalar(&cur))) return r;
    vol->GetMute(&mute);
    int now = (int)std::lround(cur * 100);
    int next = std::clamp(now + delta, 0, 100);
    if (next != now) vol->SetMasterVolumeLevelScalar(next / 100.f, nullptr);
    if (mute && delta > 0) {
        vol->SetMute(FALSE, nullptr);
        mute = FALSE;
    }
    r.ok = true;
    r.level = next;
    r.before = now;
    r.muted = mute;
    return r;
}

// Changes every audio session of the app on the default output device.
// Fails (ok = false) when the app has no session there.
static VolumeResult NudgeAppVolume(const AppAudioId& id, int delta, std::wstring* names = nullptr) {
    VolumeResult r;
    if (!id.Valid()) return r;
    auto dev = DefaultRenderDevice();
    com_ptr<IAudioSessionManager2> mgr;
    if (!dev || FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, mgr.put_void())))
        return r;
    com_ptr<IAudioSessionEnumerator> sessions;
    int count = 0;
    if (FAILED(mgr->GetSessionEnumerator(sessions.put())) || FAILED(sessions->GetCount(&count))) return r;
    auto procs = ListProcesses();
    std::vector<com_ptr<ISimpleAudioVolume>> matched;
    float cur = -1;
    BOOL mute = FALSE;
    bool fromActive = false;
    for (int i = 0; i < count; i++) {
        com_ptr<IAudioSessionControl> ctl;
        if (FAILED(sessions->GetSession(i, ctl.put())) || !ctl) continue;
        auto ctl2 = ctl.try_as<IAudioSessionControl2>();
        if (!ctl2 || ctl2->IsSystemSoundsSession() == S_OK) continue;
        DWORD pid = 0;
        ctl2->GetProcessId(&pid);
        if (!pid) continue;
        const ProcEntry* proc = nullptr;
        for (auto& p : procs)
            if (p.pid == pid) proc = &p;
        if (!proc || !id.Matches(*proc)) continue;
        auto vol = ctl.try_as<ISimpleAudioVolume>();
        if (!vol) continue;
        AudioSessionState state = AudioSessionStateInactive;
        ctl->GetState(&state);
        float level = 0;
        BOOL m = FALSE;
        vol->GetMasterVolume(&level);
        vol->GetMute(&m);
        bool active = state == AudioSessionStateActive;
        if (cur < 0 || (active && !fromActive)) {
            cur = level;
            mute = m;
            fromActive = active;
        }
        if (names) *names += proc->exe + L"(" + std::to_wstring(pid) + L") ";
        matched.push_back(vol);
    }
    if (matched.empty()) return r;
    int now = (int)std::lround(cur * 100);
    int next = std::clamp(now + delta, 0, 100);
    for (auto& v : matched) {
        if (delta != 0) v->SetMasterVolume(next / 100.f, nullptr);
        if (mute && delta > 0) v->SetMute(FALSE, nullptr);
    }
    r.ok = true;
    r.level = next;
    r.before = now;
    r.muted = mute && delta <= 0;
    return r;
}

// Volume changes run on their own thread (Spotify commands on g_cmd can
// wait out rate limits) and are merged while one is in progress.
static Worker& g_vol = *new Worker();  // never destroyed, see g_core

class MixerVolume {
   public:
    // UI thread. The result is posted to `notify` as WM_TP_VOLUME.
    void Nudge(bool system, const std::wstring& aumid, int delta, HWND notify) {
        std::lock_guard<std::mutex> lock(m_);
        if (system != system_ || aumid != aumid_) pending_ = 0;
        system_ = system;
        aumid_ = aumid;
        notify_ = notify;
        pending_ += delta;
        if (queued_) return;
        queued_ = true;
        g_vol.Start();
        if (!g_vol.Post([this] { Run(); })) queued_ = false;
    }

   private:
    void Run() {
        bool system;
        std::wstring aumid;
        int delta;
        HWND notify;
        {
            std::lock_guard<std::mutex> lock(m_);
            system = system_;
            aumid = aumid_;
            delta = pending_;
            notify = notify_;
            pending_ = 0;
            queued_ = false;
        }
        VolumeResult r;
        bool fellBack = false;
        if (!system) {
            if (aumid != idAumid_ || Tick() - idTick_ > 15000) {
                id_ = ResolveAppAudio(aumid);
                idAumid_ = aumid;
                idTick_ = Tick();
            }
            std::wstring names;
            r = NudgeAppVolume(id_, delta, &names);
            if (r.ok) {
                if (aumid != loggedAumid_) Trace(L"Volume: '%s' -> sessions %s", aumid.c_str(), names.c_str());
                loggedAumid_ = aumid;
            } else {
                fellBack = true;
                if (aumid != loggedAumid_)
                    Trace(L"Volume: no mixer session for '%s' (exe '%s', package '%s'), using the system volume",
                          aumid.c_str(), id_.exe.c_str(), id_.package.c_str());
                loggedAumid_ = aumid;
            }
        }
        if (system || !r.ok) r = NudgeSystemVolume(delta);
        WPARAM wp = r.ok ? ((WPARAM)std::clamp(r.level, 0, 100) | (r.muted ? kVolMuted : 0) |
                            (system || fellBack ? kVolSystem : 0) |
                            (r.before >= 0 ? kVolHasBefore | (WPARAM)std::clamp(r.before, 0, 100) << 16 : 0))
                         : kVolFailed;
        if (notify) PostMessageW(notify, WM_TP_VOLUME, wp, 0);
    }

    std::mutex m_;
    bool system_ = false, queued_ = false;
    std::wstring aumid_;
    int pending_ = 0;
    HWND notify_ = nullptr;
    // g_vol thread only:
    AppAudioId id_;
    std::wstring idAumid_, loggedAumid_;
    ULONGLONG idTick_ = 0;
};

static MixerVolume& g_mixer = *new MixerVolume();  // never destroyed, see g_core

// ---------------------------------------------------------------------------
// Loopback capture: what the PC (or one app) plays, as mono samples
// ---------------------------------------------------------------------------

// Process loopback (Windows 10 21H2+), declared here because older SDK
// headers lack it.
struct TpProcessLoopbackParams {
    DWORD targetProcessId;
    int mode;  // 0 = include the process tree
};
struct TpAudioClientActivationParams {
    int activationType;  // 1 = process loopback
    TpProcessLoopbackParams loopback;
};
using ActivateAudioInterfaceAsyncFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*,
                                                        IActivateAudioInterfaceCompletionHandler*,
                                                        IActivateAudioInterfaceAsyncOperation**);

class ActivateHandler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
   public:
    ActivateHandler() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    virtual ~ActivateHandler() {
        if (done) CloseHandle(done);
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == __uuidof(IAgileObject))
            *ppv = static_cast<IAgileObject*>(this);
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hr = E_FAIL;
        com_ptr<IUnknown> unk;
        if (SUCCEEDED(op->GetActivateResult(&hr, unk.put())) && SUCCEEDED(hr) && unk)
            unk->QueryInterface(__uuidof(IAudioClient), client.put_void());
        result = hr;
        SetEvent(done);
        return S_OK;
    }

    HANDLE done = nullptr;
    HRESULT result = E_PENDING;
    com_ptr<IAudioClient> client;

   private:
    std::atomic<ULONG> refs_{1};
};

class CaptureStream {
   public:
    ~CaptureStream() { Close(); }

    // Everything played on the default output device.
    bool OpenSystem(std::wstring& deviceId) {
        Close();
        auto dev = DefaultRenderDevice();
        if (!dev) return false;
        deviceId = DeviceId(dev.get());
        com_ptr<IAudioClient> client;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()))) return false;
        WAVEFORMATEX* fmt = nullptr;
        if (FAILED(client->GetMixFormat(&fmt)) || !fmt) return false;
        bool ok = Start(client.get(), fmt, false);
        CoTaskMemFree(fmt);
        return ok;
    }

    // Only what one process (and its children) plays.
    bool OpenProcess(DWORD pid) {
        Close();
        static auto activate = (ActivateAudioInterfaceAsyncFn)GetProcAddress(
            LoadLibraryW(L"mmdevapi.dll"), "ActivateAudioInterfaceAsync");
        if (!activate || !pid) return false;
        TpAudioClientActivationParams params{1, {pid, 0}};
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof(params);
        pv.blob.pBlobData = (BYTE*)&params;
        com_ptr<ActivateHandler> handler;
        handler.attach(new ActivateHandler());
        com_ptr<IActivateAudioInterfaceAsyncOperation> op;
        HRESULT hr = activate(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &pv, handler.get(), op.put());
        if (FAILED(hr) || WaitForSingleObject(handler->done, 3000) != WAIT_OBJECT_0 || FAILED(handler->result) ||
            !handler->client) {
            Trace(L"Process loopback activation failed: 0x%08X / 0x%08X", (unsigned)hr, (unsigned)handler->result);
            return false;
        }
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = 48000;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = 48000 * 4;
        return Start(handler->client.get(), &fmt, true);
    }

    void Close() {
        if (client_) client_->Stop();
        capture_ = nullptr;
        client_ = nullptr;
        if (event_) {
            CloseHandle(event_);
            event_ = nullptr;
        }
    }

    bool IsOpen() const { return capture_ != nullptr; }
    int Rate() const { return rate_; }
    int BufferMs() const { return bufferMs_; }
    HANDLE DataEvent() const { return capture_ ? event_ : nullptr; }  // set when data is ready (process loopback)

    // Appends all available audio as mono samples. Returns false if the
    // stream broke (device removed or changed format).
    bool Read(std::vector<float>& mono) {
        if (!capture_) return false;
        UINT32 packet = 0;
        HRESULT hr;
        while (SUCCEEDED(hr = capture_->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;
            size_t base = mono.size();
            mono.resize(base + frames);
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                std::fill(mono.begin() + base, mono.end(), 0.f);
            } else {
                for (UINT32 f = 0; f < frames; f++) {
                    const BYTE* frame = data + (size_t)f * blockAlign_;
                    float sum = 0;
                    for (int c = 0; c < channels_; c++) sum += SampleAt(frame, c);
                    mono[base + f] = sum / channels_;
                }
            }
            capture_->ReleaseBuffer(frames);
        }
        return SUCCEEDED(hr);
    }

   private:
    bool Start(IAudioClient* client, const WAVEFORMATEX* fmt, bool process) {
        rate_ = (int)fmt->nSamplesPerSec;
        channels_ = std::max<int>(1, fmt->nChannels);
        bits_ = fmt->wBitsPerSample;
        blockAlign_ = fmt->nBlockAlign;
        float_ = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22)
            float_ = ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat.Data1 == 3;  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        if (!(float_ ? bits_ == 32 : (bits_ == 16 || bits_ == 24 || bits_ == 32))) {
            Trace(L"Unsupported capture format: %d bits, tag %d", bits_, fmt->wFormatTag);
            return false;
        }
        // Process loopback ignores the buffer size (it gives 10 ms), so it
        // signals an event whenever data is ready and the tap drains it then.
        DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        if (process) flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 3000000 /* 300 ms */, 0, fmt, nullptr);
        if (FAILED(hr)) {
            Trace(L"Capture Initialize failed: 0x%08X", (unsigned)hr);
            return false;
        }
        if (process) {
            event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event_ || FAILED(client->SetEventHandle(event_))) return false;
        }
        com_ptr<IAudioCaptureClient> capture;
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), capture.put_void()))) return false;
        if (FAILED(client->Start())) return false;
        UINT32 frames = 0;
        client->GetBufferSize(&frames);
        bufferMs_ = rate_ > 0 ? (int)(frames * 1000LL / rate_) : 0;
        Trace(L"Capture buffer: %u frames (%d ms)", frames, bufferMs_);
        client_.copy_from(client);
        capture_ = capture;
        return true;
    }

    float SampleAt(const BYTE* frame, int c) const {
        int bytes = bits_ / 8;
        const BYTE* p = frame + c * bytes;
        if (float_) {
            float v;
            memcpy(&v, p, 4);
            return v;
        }
        switch (bits_) {
            case 16: {
                int16_t v;
                memcpy(&v, p, 2);
                return v / 32768.f;
            }
            case 24: {
                int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24);
                return v / 2147483648.f;
            }
            default: {
                int32_t v;
                memcpy(&v, p, 4);
                return v / 2147483648.f;
            }
        }
    }

    com_ptr<IAudioClient> client_;
    com_ptr<IAudioCaptureClient> capture_;
    HANDLE event_ = nullptr;
    int rate_ = 48000, channels_ = 2, bits_ = 32, blockAlign_ = 8, bufferMs_ = 0;
    bool float_ = true;
};

// ---------------------------------------------------------------------------
// The audio tap: a thread that captures, analyzes and hands frames to the
// visualizer. It only runs in the explorer process that owns the taskbar
// (the visualizer starts it) and is joined when the mod unloads.
// ---------------------------------------------------------------------------

struct TapConfig {
    bool active = false;   // capture at all
    bool fast = false;     // frames at `fps` (otherwise ~10 per second)
    bool appOnly = false;  // only the app `aumid`
    std::wstring aumid;
    int bands = 6, minHz = 50, maxHz = 10000, fps = 60, wavePoints = 48;
    bool autoLevel = true;
    int sensitivity = 100;
    HWND notify = nullptr;

    bool operator==(const TapConfig& o) const {
        return active == o.active && fast == o.fast && appOnly == o.appOnly && aumid == o.aumid &&
               bands == o.bands && minHz == o.minHz && maxHz == o.maxHz && fps == o.fps &&
               wavePoints == o.wavePoints && autoLevel == o.autoLevel && sensitivity == o.sensitivity &&
               notify == o.notify;
    }
};

struct TapFrame {
    std::vector<float> bands;  // 0..1
    std::vector<float> wave;   // -1..1
    ULONGLONG lastSound = 0;   // when the sound was last above the silence threshold
    ULONGLONG soundSince = 0;  // start of the current stretch of sound
    float rmsDb = -120;        // loudness of the last captured audio
    bool capturing = false;
    uint64_t seq = 0;
};

class AudioTap {
   public:
    // UI thread.
    void Configure(const TapConfig& c) {
        {
            std::lock_guard<std::mutex> lock(m_);
            if (cfg_ == c && running_) return;
            // A new window: a frame message posted to the old one never arrives.
            if (c.notify != cfg_.notify) framePosted = false;
            cfg_ = c;
            gen_++;
            if (!running_ && c.active && !g_shuttingDown) {
                if (!wake_) wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                stop_ = false;
                running_ = true;
                thread_ = std::thread([this] { Run(); });
            }
        }
        if (wake_) SetEvent(wake_);
    }

    void Read(TapFrame& out) {
        std::lock_guard<std::mutex> lock(m_);
        out = frame_;
    }

    // Stops the thread (mod unload).
    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        if (wake_) SetEvent(wake_);
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lock(m_);
        running_ = false;
        cfg_ = TapConfig();
        framePosted = false;
        frame_ = TapFrame();
    }

    std::atomic<bool> framePosted{false};
    std::atomic<uint64_t> samplesRead{0};  // diagnostics

   private:
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        CaptureStream stream;
        Spectrum fft;
        BandLayout layout;
        LevelMapper mapper;
        std::vector<float> ring(1 << 15), mono, block, power, db, levels, wave;
        uint64_t written = 0;  // samples written to the ring in total
        TapConfig cfg;
        uint64_t cfgGen = ~0ull;
        std::wstring deviceId, openedFor;
        DWORD targetPid = 0;
        ULONGLONG retryAt = 0, lastDeviceCheck = 0, lastPacket = 0, lastFill = 0, lastAnalysis = Tick();
        ULONGLONG lastSound = 0, soundSince = 0, lastResolve = 0;
        float wavePeak = 0.05f, lastRmsDb = -120;
        bool loggedFail = false;

        auto push = [&](const float* s, size_t n) {
            size_t cap = ring.size();
            for (size_t i = 0; i < n; i++) ring[(written + i) % cap] = s[i];
            written += n;
        };
        auto last = [&](size_t n, std::vector<float>& out) {
            size_t cap = ring.size();
            out.resize(n);
            for (size_t i = 0; i < n; i++) {
                uint64_t idx = written >= n ? written - n + i : 0;
                out[i] = written >= n ? ring[idx % cap] : 0.f;
            }
        };

        while (true) {
            {
                std::lock_guard<std::mutex> lock(m_);
                if (stop_) break;
                if (gen_ != cfgGen) {
                    cfg = cfg_;
                    cfgGen = gen_;
                }
            }
            ULONGLONG now = Tick();
            if (!cfg.active) {
                if (stream.IsOpen()) {
                    stream.Close();
                    openedFor.clear();
                    Trace(L"Visualizer capture stopped");
                }
                {
                    std::lock_guard<std::mutex> lock(m_);
                    frame_.capturing = false;
                    std::fill(frame_.bands.begin(), frame_.bands.end(), 0.f);
                    std::fill(frame_.wave.begin(), frame_.wave.end(), 0.f);
                }
                WaitForSingleObject(wake_, INFINITE);
                continue;
            }

            std::wstring want = cfg.appOnly ? L"app:" + cfg.aumid : L"system";
            if (stream.IsOpen() && want != openedFor) {
                stream.Close();
                retryAt = 0;
            }
            if (!stream.IsOpen() && now >= retryAt) {
                bool ok = false;
                if (cfg.appOnly) {
                    targetPid = ResolveAppAudio(cfg.aumid).rootPid;
                    lastResolve = now;
                    ok = targetPid && stream.OpenProcess(targetPid);
                } else {
                    ok = stream.OpenSystem(deviceId);
                }
                openedFor = want;
                if (ok) {
                    Trace(L"Visualizer capturing %s (pid %u) at %d Hz", cfg.appOnly ? cfg.aumid.c_str() : L"all sound",
                          cfg.appOnly ? targetPid : 0, stream.Rate());
                    loggedFail = false;
                    lastPacket = lastFill = now;
                    lastDeviceCheck = now;
                } else {
                    if (!loggedFail)
                        Trace(L"Visualizer capture failed (%s, pid %u), retrying", want.c_str(), targetPid);
                    loggedFail = true;
                    retryAt = now + 3000;
                }
            }

            float rate = (float)(stream.IsOpen() ? stream.Rate() : 48000);
            int fftN = 2048;
            while (fftN < rate * 0.04f && fftN < 16384) fftN <<= 1;  // ~40 ms of sound
            fft.Prepare(fftN);
            if (layout.count != cfg.bands || layout.fftN != fftN || layout.rate != rate ||
                layout.minHz != cfg.minHz || layout.maxHz != cfg.maxHz)
                BuildBands(layout, cfg.bands, cfg.minHz, cfg.maxHz, rate, fftN);

            bool sound = false;
            mono.clear();
            if (stream.IsOpen() && !stream.Read(mono)) {
                Trace(L"Visualizer capture stream broke, reopening");
                stream.Close();
                retryAt = now + 500;
            }
            if (!mono.empty()) {
                push(mono.data(), mono.size());
                samplesRead += mono.size();
                lastPacket = lastFill = now;
                double sum = 0;
                for (float v : mono) sum += (double)v * v;
                float rmsDb = 10.f * std::log10((float)(sum / mono.size()) + 1e-12f);
                sound = rmsDb > -58.f;
                lastRmsDb = rmsDb;
            } else if (now - lastPacket > 60) {
                // Nothing is playing (the device then delivers no data) or
                // there is no stream: feed silence so the bars fall to zero.
                size_t n = (size_t)((now - lastFill) * rate / 1000);
                if (n) {
                    mono.assign(std::min<size_t>(n, ring.size()), 0.f);
                    push(mono.data(), mono.size());
                }
                lastFill = now;
            }
            if (stream.IsOpen()) {
                if (!cfg.appOnly && now - lastDeviceCheck > 2000) {
                    lastDeviceCheck = now;
                    if (DeviceId(DefaultRenderDevice().get()) != deviceId) {
                        Trace(L"Default output device changed, reopening the capture");
                        stream.Close();
                        retryAt = 0;
                    }
                }
                if (cfg.appOnly && now - lastPacket > 4000 && now - lastResolve > 4000) {
                    // The app may have restarted.
                    lastResolve = now;
                    DWORD pid = ResolveAppAudio(cfg.aumid).rootPid;
                    if (pid != targetPid) {
                        stream.Close();
                        retryAt = 0;
                    }
                }
            }
            if (sound) {
                if (!lastSound || now - lastSound > 400) soundSince = now;
                lastSound = now;
            }

            // Analysis, at the frame rate (data may arrive more often).
            DWORD interval = cfg.fast ? std::max(8, 1000 / std::max(1, cfg.fps)) : 100;
            if (now < lastAnalysis + interval - 2) {
                WaitForWork(timer, stream.DataEvent(), (DWORD)(lastAnalysis + interval - now));
                continue;
            }
            float dt = std::clamp((now - lastAnalysis) / 1000.f, 0.001f, 0.5f);
            lastAnalysis = now;
#ifdef TP_HARNESS
            uint64_t dbgT0 = DbgNowUs();
#endif
            last((size_t)fftN, block);
            fft.Power(block.data(), power);
            db.resize(cfg.bands);
            levels.resize(cfg.bands);
            BandsDb(power, layout, db.data());
            float gainDb = 20.f * std::log10(cfg.sensitivity / 100.f);
            mapper.Map(db.data(), cfg.bands, dt, cfg.autoLevel, gainDb, levels.data());

            // Wave: ~25 ms of sound starting at a rising zero crossing, so the
            // shape stands still instead of jittering.
            size_t span = (size_t)(rate * 0.025f), search = (size_t)(rate * 0.02f);
            last(span + search, block);
            size_t start = 0;
            for (size_t i = search; i > 1; i--) {
                if (block[i - 1] < 0 && block[i] >= 0) {
                    start = i;
                    break;
                }
            }
            float peak = 0;
            for (size_t i = start; i < start + span; i++) peak = std::max(peak, std::fabs(block[i]));
            wavePeak = std::max({peak, wavePeak * std::exp(-dt / 1.5f), 0.01f});
            float scale = (cfg.autoLevel ? 0.9f / wavePeak : 1.f) * cfg.sensitivity / 100.f;
            int P = std::max(4, cfg.wavePoints);

            wave.resize(P);
            for (int i = 0; i < P; i++) {
                float pos = start + (float)i * (span - 1) / (P - 1);
                size_t k = std::min((size_t)pos, block.size() - 2);
                float t = pos - k;
                float v = block[k] * (1 - t) + block[k + 1] * t;
                wave[i] = std::clamp(v * scale, -1.f, 1.f);
            }
            {
                std::lock_guard<std::mutex> lock(m_);
                frame_.bands = levels;
                frame_.wave = wave;
                frame_.lastSound = lastSound;
                frame_.soundSince = soundSince;
                frame_.rmsDb = lastRmsDb;
                frame_.capturing = stream.IsOpen();
                frame_.seq++;
            }
#ifdef TP_HARNESS
            g_dbgAnalysisUs += DbgNowUs() - dbgT0;
#endif
            if (cfg.notify && !framePosted.exchange(true))
                if (!PostMessageW(cfg.notify, WM_TP_VIZFRAME, 0, 0)) framePosted = false;

            WaitForWork(timer, stream.DataEvent(), interval);
        }
        stream.Close();
        if (timer) CloseHandle(timer);
        CoUninitialize();
    }

    // Sleeps until the next frame is due, new audio data is ready or the
    // configuration changed.
    void WaitForWork(HANDLE timer, HANDLE data, DWORD ms) {
        HANDLE handles[3] = {wake_, timer, data};
        DWORD n = 1;
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)ms * 10000;
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) n = 2;
        if (data) {
            if (n == 1) handles[1] = data;
            n++;
        }
        WaitForMultipleObjects(n, handles, FALSE, ms + 50);
    }

    std::mutex m_;
    std::thread thread_;
    HANDLE wake_ = nullptr;
    bool running_ = false, stop_ = false;
    TapConfig cfg_;
    uint64_t gen_ = 0;
    TapFrame frame_;
};

static AudioTap& g_tap = *new AudioTap();  // never destroyed, see g_core

// ---------------------------------------------------------------------------
// The visualizer: a small click-through window on top of the widget, so the
// animation only redraws this strip and not the whole widget.
// ---------------------------------------------------------------------------

class Visualizer {
   public:
    static constexpr const wchar_t* kClass = L"TaskbarPlayer_Visualizer";

    struct Context {
        bool on = false;        // the feature is enabled and the widget shows a track
        bool gateOpen = false;  // allowed to react to sound right now
        std::wstring aumid;     // app shown on the widget (for "only this app")
        bool hasAccent = false;
        D2D1_COLOR_F accent{1, 1, 1, 1};
        bool light = false;
        float scale = 1.f;
    };

    bool Create(HWND parent, bool overlay, std::function<void()> onSlotChange) {
        overlay_ = overlay;
        onSlotChange_ = std::move(onSlotChange);
        DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        if (overlay) ex |= WS_EX_TOOLWINDOW;
        hwnd_ = CreateWindowExW(ex, kClass, L"", overlay ? WS_POPUP : (WS_CHILD | WS_CLIPSIBLINGS), 0, 0, 1, 1,
                                parent, nullptr, ModuleInstance(), this);
        if (!hwnd_) {
            Wh_Log(L"Visualizer window creation failed: %u", GetLastError());
            return false;
        }
        surf_.software = true;
        return true;
    }

    void Destroy() {
        g_tap.Configure(TapConfig());
        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    HWND Hwnd() const { return hwnd_; }
    // Shown, regardless of whether the parent is (IsWindowVisible checks parents too).
    bool Shown() const { return hwnd_ && (GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_VISIBLE); }

    void SetContext(const Context& c) {
        bool accentChanged = c.hasAccent != ctx_.hasAccent || c.accent.r != ctx_.accent.r ||
                             c.accent.g != ctx_.accent.g || c.accent.b != ctx_.accent.b;
        ctx_ = c;
        if (accentChanged) dirty_ = true;
        Evaluate();
    }

    void OnSettingsChanged() {
        dirty_ = true;
        Evaluate();
        Render();
    }

    // Layout presence of the slot (0..1, eased), animated by the widget.
    float SlotValue() const { return EaseInOutCubic(slotT_.value); }
    float SlotTarget() const { return slotT_.to; }
    bool StepSlot() { return slotT_.Step(); }

    // Where the window goes (parent client or screen coordinates), its size,
    // how much of the slot is open and the widget's own opacity.
    void Place(int x, int y, int w, int h, float squeeze, float widgetAlpha) {
        if (!hwnd_) return;
        bool moved = x != x_ || y != y_;
        bool changed = w != w_ || h != h_ || std::fabs(squeeze - squeeze_) > 0.001f ||
                       std::fabs(widgetAlpha - widgetAlpha_) > 0.001f;
        x_ = x;
        y_ = y;
        w_ = w;
        h_ = h;
        squeeze_ = squeeze;
        widgetAlpha_ = widgetAlpha;
        if (moved && !overlay_)
            SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (changed || moved) Render();
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Visualizer* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = (Visualizer*)((CREATESTRUCTW*)lp)->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = hwnd;
        } else {
            self = (Visualizer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        return self->Handle(msg, wp, lp);
    }

#ifdef TP_HARNESS
    // Draws the given levels without any audio (test harness only).
    void DebugSet(const std::vector<float>& levels, const std::vector<float>& wave, float peakBoost,
                  float slot = 1.f) {
        cur_ = levels;
        peak_.assign(levels.size(), 0.f);
        for (size_t i = 0; i < levels.size(); i++) peak_[i] = std::min(1.f, levels[i] + peakBoost);
        wave_ = wave;
        alphaT_.Snap(slot);
        slotT_.Snap(slot);
        accentNow_ = TargetColor();
        accentSet_ = true;
        dirty_ = true;
        Render();
    }
    const Surface& DebugSurface() const { return surf_; }
    int DebugX() const { return x_; }
#endif

   private:
    using V = Settings::Viz;
    enum { kTimerDecay = 1 };

    float S(float v) const { return v * ctx_.scale; }

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_NCHITTEST:
                return HTTRANSPARENT;
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_TP_VIZFRAME:
                g_tap.framePosted = false;
                OnFrame(true);
                return 0;
            case WM_TIMER:
                if (wp == kTimerDecay) OnFrame(false);
                return 0;
            case WM_NCDESTROY: {
                HWND h = hwnd_;
                KillTimer(h, kTimerDecay);
                surf_.Release();
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                return DefWindowProcW(h, msg, wp, lp);
            }
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }

    bool TapActive() const { return ctx_.on && ctx_.gateOpen && GetSettings()->viz.enabled; }

    // Decides what should be visible and tells the audio tap what it needs.
    void Evaluate() {
        const V& v = GetSettings()->viz;
        float slot = 0, alpha = 0;
        if (ctx_.on && v.enabled) {
            switch (v.whenSilent) {
                case V::Silent::Shrink:
                    slot = alpha = sounding_ ? 1.f : 0.f;
                    break;
                case V::Silent::Keep:
                    slot = 1;
                    alpha = sounding_ ? 1.f : 0.f;
                    break;
                case V::Silent::Rest:
                    slot = alpha = 1;
                    break;
            }
        }
        bool anim = GetSettings()->popupAnimations;
        if (slot != slotT_.to) {
            slotT_.To(slot, anim ? 280.f : 0.f);
            if (onSlotChange_) onSlotChange_();
        }
        if (alpha != alphaT_.to) {
            alphaT_.To(alpha, anim ? (alpha > 0 ? 260.f : 320.f) : 0.f);
            dirty_ = true;
        }
        if (!TapActive()) sounding_ = false;

        TapConfig t;
        t.active = TapActive();
        t.fast = t.active && (sounding_ || alphaT_.dur > 0 || Moving());
        t.appOnly = v.appOnly;
        t.aumid = v.appOnly ? ctx_.aumid : L"";
        t.bands = v.bars;
        t.minHz = v.minHz;
        t.maxHz = v.maxHz;
        t.fps = v.frameRate;
        t.wavePoints = std::clamp((int)(S((float)v.width) / 1.5f), 16, 160);
        t.autoLevel = v.autoLevel;
        t.sensitivity = v.sensitivity;
        t.notify = hwnd_;
        g_tap.Configure(t);

        // Without the tap there are no frames: animate the fall-off locally.
        if (hwnd_) {
            if (!t.active && (Moving() || alphaT_.dur > 0)) SetTimer(hwnd_, kTimerDecay, 16, nullptr);
            else KillTimer(hwnd_, kTimerDecay);
        }
    }

    bool Moving() const {
        for (float c : cur_)
            if (c > 0.002f) return true;
        for (float p : peak_)
            if (p > 0.002f) return true;
        for (float w : wave_)
            if (std::fabs(w) > 0.002f) return true;
        return false;
    }

    static float TimeConstant(int speed, float slowMs, float fastMs) {
        float t = (std::clamp(speed, 1, 100) - 1) / 99.f;
        return slowMs * std::pow(fastMs / slowMs, t);
    }

    void OnFrame(bool fromTap) {
        const V& v = GetSettings()->viz;
        ULONGLONG now = Tick();
        float dt = std::clamp((now - lastFrame_) / 1000.f, 0.001f, 0.25f);
        lastFrame_ = now;

        TapFrame f;
        if (fromTap && TapActive()) g_tap.Read(f);
        size_t n = (size_t)v.bars;
        if (f.bands.size() != n) f.bands.assign(n, 0.f);
        if (cur_.size() != n) cur_.assign(n, 0.f);
        if (peak_.size() != n) peak_.assign(n, 0.f);
        if (hold_.size() != n) hold_.assign(n, 0);

        // Sound state.
        bool was = sounding_;
        ULONGLONG delay = std::max(150, v.silenceDelayMs);
        sounding_ = TapActive() && f.capturing && f.lastSound && now - f.lastSound < delay &&
                    (was || now - f.soundSince >= 350);
        if (sounding_ != was) Trace(L"Visualizer: sound %s", sounding_ ? L"started" : L"stopped");

        // Smoothing: fast rise, slower fall.
        float kRise = 1 - std::exp(-dt * 1000 / TimeConstant(v.riseSpeed, 120.f, 4.f));
        float kFall = 1 - std::exp(-dt * 1000 / TimeConstant(v.fallSpeed, 1200.f, 25.f));
        bool changed = false;
        for (size_t i = 0; i < n; i++) {
            float target = sounding_ || v.whenSilent == V::Silent::Rest ? f.bands[i] : 0.f;
            if (!fromTap) target = 0;
            float old = cur_[i];
            cur_[i] += (target - cur_[i]) * (target > cur_[i] ? kRise : kFall);
            if (cur_[i] < 0.001f) cur_[i] = 0;
            if (cur_[i] >= peak_[i]) {
                peak_[i] = cur_[i];
                hold_[i] = now + 450;
            } else if (now > hold_[i]) {
                peak_[i] = std::max(cur_[i], peak_[i] - 0.9f * dt);
            }
            changed |= std::fabs(cur_[i] - old) > 0.0005f;
        }
        size_t P = f.wave.size();
        if (!fromTap || !sounding_) {
            for (auto& w : wave_) w *= std::exp(-dt / 0.08f);
        } else {
            if (wave_.size() != P) wave_.assign(P, 0.f);
            for (size_t i = 0; i < P; i++) wave_[i] += (f.wave[i] - wave_[i]) * 0.65f;
        }
        if (v.style == V::Style::Wave) changed = true;

        Evaluate();
        bool alphaMoving = alphaT_.Step();
        changed |= alphaMoving;

        // The accent color follows the cover smoothly.
        D2D1_COLOR_F target = TargetColor();
        if (!accentSet_) {
            accentNow_ = target;
            accentSet_ = true;
        } else {
            float k = 1 - std::exp(-dt / 0.3f);
            D2D1_COLOR_F a = accentNow_;
            a.r += (target.r - a.r) * k;
            a.g += (target.g - a.g) * k;
            a.b += (target.b - a.b) * k;
            a.a += (target.a - a.a) * k;
            if (std::fabs(a.r - accentNow_.r) + std::fabs(a.g - accentNow_.g) + std::fabs(a.b - accentNow_.b) +
                    std::fabs(a.a - accentNow_.a) >
                0.001f)
                changed = true;
            accentNow_ = a;
        }
        if (changed || dirty_) Render();
    }

    D2D1_COLOR_F TextColor() const { return ctx_.light ? Rgba(0x000000, 0.9f) : Rgba(0xFFFFFF, 1.f); }

    static D2D1_COLOR_F Argb(uint32_t c) { return Rgba(c & 0xFFFFFF, ((c >> 24) & 255) / 255.f); }

    D2D1_COLOR_F TargetColor() const {
        const V& v = GetSettings()->viz;
        switch (v.colorMode) {
            case V::Color::Accent:
                return ctx_.hasAccent ? WithAlpha(ctx_.accent, 1.f) : TextColor();
            case V::Color::Custom:
            case V::Color::Gradient:
                return Argb(v.color);
            default:
                return TextColor();
        }
    }

    D2D1_COLOR_F Rainbow(float t) const {
        D2D1_COLOR_F c = HslToColor(Clamp01(t) * 0.8f, 0.9f, ctx_.light ? 0.45f : 0.62f);
        return c;
    }

    // A brush for an element spanning `a` (base) to `b` (tip), or a
    // horizontal one from x0 to x1 for rainbow lines.
    ID2D1Brush* Paint(ID2D1RenderTarget* rt, D2D1_POINT_2F base, D2D1_POINT_2F tip, float alpha,
                      float rainbowT = -1) {
        const V& v = GetSettings()->viz;
        if (v.colorMode == V::Color::Rainbow && rainbowT >= 0) {
            D2D1_COLOR_F c = Rainbow(rainbowT);
            return surf_.Brush(WithAlpha(c, c.a * alpha));
        }
        if (v.colorMode == V::Color::Gradient) {
            D2D1_COLOR_F c1 = Argb(v.color), c2 = Argb(v.color2);
            auto* g = Gradient(rt, {{0.f, c1}, {1.f, c2}});
            if (g) {
                g->SetStartPoint(base);
                g->SetEndPoint(tip);
                g->SetOpacity(alpha);
                return g;
            }
        }
        D2D1_COLOR_F c = accentNow_;
        return surf_.Brush(WithAlpha(c, c.a * alpha));
    }

    ID2D1LinearGradientBrush* Gradient(ID2D1RenderTarget* rt, std::vector<D2D1_GRADIENT_STOP> stops) {
        std::wstring key;
        for (auto& s : stops) {
            wchar_t buf[96];
            swprintf(buf, 96, L"%.3f:%.3f,%.3f,%.3f,%.3f;", s.position, s.color.r, s.color.g, s.color.b, s.color.a);
            key += buf;
        }
        if (surf_.gen != gradGen_) {
            grads_.clear();
            gradGen_ = surf_.gen;
        }
        auto it = grads_.find(key);
        if (it != grads_.end()) return it->second.get();
        com_ptr<ID2D1GradientStopCollection> coll;
        if (FAILED(rt->CreateGradientStopCollection(stops.data(), (UINT32)stops.size(), coll.put()))) return nullptr;
        com_ptr<ID2D1LinearGradientBrush> brush;
        if (FAILED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), coll.get(),
                                                 brush.put())))
            return nullptr;
        if (grads_.size() > 8) grads_.clear();
        grads_[key] = brush;
        return brush.get();
    }

    ID2D1LinearGradientBrush* RainbowLine(ID2D1RenderTarget* rt, float x0, float x1, float alpha) {
        std::vector<D2D1_GRADIENT_STOP> stops;
        for (int i = 0; i <= 6; i++) stops.push_back({i / 6.f, Rainbow(i / 6.f)});
        auto* g = Gradient(rt, stops);
        if (g) {
            g->SetStartPoint({x0, 0});
            g->SetEndPoint({x1, 0});
            g->SetOpacity(alpha);
        }
        return g;
    }

    void Render() {
        dirty_ = false;
        if (!hwnd_ || !g_gfx) return;
        float alpha = alphaT_.value * widgetAlpha_ * GetSettings()->viz.opacity / 100.f;
        bool show = alpha > 0.003f && squeeze_ > 0.01f && w_ > 1;
        if (!show) {
            if (Shown()) ShowWindow(hwnd_, SW_HIDE);
            return;
        }
#ifdef TP_HARNESS
        uint64_t t0 = DbgNowUs();
#endif
        if (!surf_.Resize(std::max(1, w_), std::max(1, h_))) return;
        ID2D1RenderTarget* rt = surf_.Begin();
        if (!rt) return;
        float W = (float)w_, H = (float)h_;
        // Squeeze horizontally around the center while the slot opens/closes.
        rt->SetTransform(D2D1::Matrix3x2F::Scale(squeeze_, 1.f, D2D1::Point2F(W / 2, H / 2)));
        Draw(rt, W, H, alpha);
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (!surf_.End()) return;
#ifdef TP_HARNESS
        uint64_t t1 = DbgNowUs();
        g_dbgDrawUs += t1 - t0;
#endif
        POINT pos{x_, y_};
        surf_.Present(hwnd_, overlay_ ? &pos : nullptr, 255);
#ifdef TP_HARNESS
        g_dbgPresentUs += DbgNowUs() - t1;
#endif
        if (!Shown()) {
            ShowWindow(hwnd_, SW_SHOWNA);
            if (!overlay_) SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    void Draw(ID2D1RenderTarget* rt, float W, float H, float alpha) {
        const V& v = GetSettings()->viz;
        float bw = std::min(S((float)v.width), W), bh = std::min(S((float)v.height), H - S(4));
        D2D1_RECT_F box = RectXYWH(std::round((W - bw) / 2), std::round((H - bh) / 2), bw, bh);
        int n = (int)cur_.size();
        if (n <= 0) return;
        auto level = [&](int i) { return cur_[v.mirror ? n - 1 - i : i]; };
        auto peakOf = [&](int i) { return peak_[v.mirror ? n - 1 - i : i]; };
        float gap = S((float)v.gap);
        float barW = (bw - gap * (n - 1)) / n;
        if (barW < 1) {
            gap = std::max(0.f, (bw - n) / std::max(1, n - 1));
            barW = std::max(0.5f, (bw - gap * (n - 1)) / n);
        }
        float round = v.roundness / 100.f;
        float minH = std::max(S(2), std::min(barW, bh * 0.2f));
        bool peaks = v.peaks;
        float capH = std::max(S(1.5f), std::min(barW * 0.45f, S(3)));
        auto rr = [&](D2D1_RECT_F r) {
            float rad = std::min(Width(r), Height(r)) / 2 * round;
            return D2D1::RoundedRect(r, rad, rad);
        };

        switch (v.style) {
            case V::Style::Bars:
            case V::Style::Mirrored: {
                bool mir = v.style == V::Style::Mirrored;
                float mid = (box.top + box.bottom) / 2;
                for (int i = 0; i < n; i++) {
                    float x = box.left + i * (barW + gap);
                    float h = minH + level(i) * (bh - minH);
                    float t = n > 1 ? (float)i / (n - 1) : 0.f;
                    D2D1_RECT_F r = mir ? RectF(x, mid - h / 2, x + barW, mid + h / 2)
                                        : RectF(x, box.bottom - h, x + barW, box.bottom);
                    D2D1_POINT_2F base = mir ? D2D1::Point2F(x, mid) : D2D1::Point2F(x, box.bottom);
                    D2D1_POINT_2F tip = D2D1::Point2F(x, box.top);
                    if (mir) {
                        // Gradient from the center outwards: draw both halves.
                        D2D1_RECT_F top = RectF(r.left, r.top, r.right, mid + 0.5f);
                        D2D1_RECT_F bottom = RectF(r.left, mid - 0.5f, r.right, r.bottom);
                        if (GetSettings()->viz.colorMode == V::Color::Gradient) {
                            rt->PushAxisAlignedClip(top, D2D1_ANTIALIAS_MODE_ALIASED);
                            rt->FillRoundedRectangle(rr(r), Paint(rt, base, D2D1::Point2F(x, box.top), alpha, t));
                            rt->PopAxisAlignedClip();
                            rt->PushAxisAlignedClip(bottom, D2D1_ANTIALIAS_MODE_ALIASED);
                            rt->FillRoundedRectangle(rr(r), Paint(rt, base, D2D1::Point2F(x, box.bottom), alpha, t));
                            rt->PopAxisAlignedClip();
                        } else {
                            rt->FillRoundedRectangle(rr(r), Paint(rt, base, tip, alpha, t));
                        }
                    } else {
                        rt->FillRoundedRectangle(rr(r), Paint(rt, base, tip, alpha, t));
                    }
                    if (peaks) {
                        float ph = minH + peakOf(i) * (bh - minH);
                        if (ph > h + capH * 0.5f) {
                            if (mir) {
                                float y = std::max(box.top, mid - ph / 2 - capH - S(1));
                                rt->FillRoundedRectangle(rr(RectF(x, y, x + barW, y + capH)),
                                                         Paint(rt, base, tip, alpha * 0.85f, t));
                                float y2 = std::min(box.bottom - capH, mid + ph / 2 + S(1));
                                rt->FillRoundedRectangle(rr(RectF(x, y2, x + barW, y2 + capH)),
                                                         Paint(rt, base, tip, alpha * 0.85f, t));
                            } else {
                                float y = std::max(box.top, box.bottom - ph - capH - S(1));
                                rt->FillRoundedRectangle(rr(RectF(x, y, x + barW, y + capH)),
                                                         Paint(rt, base, tip, alpha * 0.85f, t));
                            }
                        }
                    }
                }
                break;
            }
            case V::Style::Dots: {
                float d = barW;
                int rows = std::max(1, (int)std::floor((bh + gap) / (d + gap)));
                float used = rows * d + (rows - 1) * gap;
                float top = box.top + (bh - used) / 2;
                for (int i = 0; i < n; i++) {
                    float x = box.left + i * (barW + gap);
                    float lit = level(i) * rows;
                    int peakRow = (int)std::ceil(peakOf(i) * rows) - 1;
                    float t = n > 1 ? (float)i / (n - 1) : 0.f;
                    for (int r = 0; r < rows; r++) {
                        float y = top + used - (r + 1) * d - r * gap;
                        D2D1_RECT_F dot = RectXYWH(x, y, d, d);
                        float on = Clamp01(lit - r);
                        if (r == 0) on = std::max(on, 1.f);  // the bottom dot is always lit
                        if (peaks && r == peakRow && r > 0) on = std::max(on, 0.85f);
                        float a = alpha * (0.14f + 0.86f * on);
                        rt->FillRoundedRectangle(rr(dot), Paint(rt, D2D1::Point2F(x, box.bottom),
                                                                D2D1::Point2F(x, box.top), a, t));
                    }
                }
                break;
            }
            case V::Style::Line: {
                std::vector<D2D1_POINT_2F> pts(n);
                float stroke = std::max(S(1.5f), 1.f);
                float top = box.top + stroke / 2, bottom = box.bottom - stroke / 2;
                for (int i = 0; i < n; i++) {
                    float x = n > 1 ? box.left + bw * i / (n - 1) : box.left + bw / 2;
                    pts[i] = D2D1::Point2F(x, bottom - level(i) * (bottom - top));
                }
                auto clampY = [&](float y) { return std::clamp(y, top, bottom); };
                auto build = [&](bool closed) {
                    com_ptr<ID2D1PathGeometry> geo;
                    if (FAILED(g_gfx->D2D()->CreatePathGeometry(geo.put()))) return geo;
                    com_ptr<ID2D1GeometrySink> sink;
                    geo->Open(sink.put());
                    sink->BeginFigure(closed ? D2D1::Point2F(pts[0].x, box.bottom) : pts[0],
                                      closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
                    if (closed) sink->AddLine(pts[0]);
                    for (int i = 0; i + 1 < n; i++) {
                        D2D1_POINT_2F p0 = pts[std::max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1],
                                      p3 = pts[std::min(n - 1, i + 2)];
                        D2D1_POINT_2F c1{p1.x + (p2.x - p0.x) / 6, clampY(p1.y + (p2.y - p0.y) / 6)};
                        D2D1_POINT_2F c2{p2.x - (p3.x - p1.x) / 6, clampY(p2.y - (p3.y - p1.y) / 6)};
                        sink->AddBezier(D2D1::BezierSegment(c1, c2, p2));
                    }
                    if (closed) sink->AddLine(D2D1::Point2F(pts[n - 1].x, box.bottom));
                    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    return geo;
                };
                auto fill = build(true), line = build(false);
                bool rainbow = v.colorMode == V::Color::Rainbow;
                if (fill) {
                    ID2D1Brush* b = nullptr;
                    if (rainbow) {
                        b = RainbowLine(rt, box.left, box.right, alpha * 0.3f);
                    } else if (v.colorMode == V::Color::Gradient) {
                        b = Paint(rt, D2D1::Point2F(0, box.bottom), D2D1::Point2F(0, box.top), alpha * 0.32f);
                    } else {
                        // A solid color fades out towards the bottom.
                        D2D1_COLOR_F c = accentNow_;
                        auto* g = Gradient(rt, {{0.f, WithAlpha(c, c.a * 0.5f)}, {1.f, WithAlpha(c, c.a * 0.04f)}});
                        if (g) {
                            g->SetStartPoint(D2D1::Point2F(0, box.top));
                            g->SetEndPoint(D2D1::Point2F(0, box.bottom));
                            g->SetOpacity(alpha);
                        }
                        b = g;
                    }
                    if (b) rt->FillGeometry(fill.get(), b);
                }
                if (line) {
                    ID2D1Brush* b = rainbow ? (ID2D1Brush*)RainbowLine(rt, box.left, box.right, alpha)
                                            : Paint(rt, D2D1::Point2F(0, box.bottom), D2D1::Point2F(0, box.top), alpha);
                    if (b) rt->DrawGeometry(line.get(), b, stroke, g_gfx->RoundStroke());
                }
                break;
            }
            case V::Style::Wave: {
                int P = (int)wave_.size();
                float stroke = std::max(S(1.5f), 1.f);
                float mid = (box.top + box.bottom) / 2, amp = bh / 2 - stroke / 2;
                com_ptr<ID2D1PathGeometry> geo;
                if (FAILED(g_gfx->D2D()->CreatePathGeometry(geo.put()))) break;
                com_ptr<ID2D1GeometrySink> sink;
                geo->Open(sink.put());
                for (int i = 0; i < std::max(P, 2); i++) {
                    float x = box.left + bw * i / std::max(1, std::max(P, 2) - 1);
                    float w = P ? wave_[v.mirror ? P - 1 - i : i] : 0.f;
                    D2D1_POINT_2F p = D2D1::Point2F(x, mid - w * amp);
                    if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
                    else sink->AddLine(p);
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                ID2D1Brush* b = v.colorMode == V::Color::Rainbow
                                    ? (ID2D1Brush*)RainbowLine(rt, box.left, box.right, alpha)
                                : v.colorMode == V::Color::Gradient
                                    ? Paint(rt, D2D1::Point2F(box.left, 0), D2D1::Point2F(box.right, 0), alpha)
                                    : Paint(rt, {}, {}, alpha);
                if (b) rt->DrawGeometry(geo.get(), b, stroke, g_gfx->RoundStroke());
                break;
            }
        }
    }

    HWND hwnd_ = nullptr;
    bool overlay_ = false;
    std::function<void()> onSlotChange_;
    Context ctx_;
    Surface surf_;
    int x_ = INT_MIN, y_ = INT_MIN, w_ = 0, h_ = 0;
    float squeeze_ = 0, widgetAlpha_ = 0;
    Tween slotT_, alphaT_;
    bool sounding_ = false, dirty_ = true;
    ULONGLONG lastFrame_ = 0;
    std::vector<float> cur_, peak_, wave_;
    std::vector<ULONGLONG> hold_;
    D2D1_COLOR_F accentNow_{1, 1, 1, 1};
    bool accentSet_ = false;
    std::map<std::wstring, com_ptr<ID2D1LinearGradientBrush>> grads_;
    uint32_t gradGen_ = ~0u;
};

}  // namespace tp
