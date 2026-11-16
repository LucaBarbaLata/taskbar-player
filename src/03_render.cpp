
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
        boundW_ = boundH_ = -1;
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
        boundW_ = boundH_ = -1;  // bind the render target to the new bitmap
        bits_ = bits;
        w = nw;
        h = nh;
        return true;
    }

    ID2D1RenderTarget* Begin() {
        if (!dc_) return nullptr;
        if (!rt_) {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                software ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.f,
                96.f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
            if (FAILED(g_gfx->D2D()->CreateDCRenderTarget(&props, rt_.put()))) return nullptr;
            gen++;
        }
        // Binding is only needed when the bitmap changed (it is costly per frame).
        if (boundW_ != w || boundH_ != h || boundGen_ != gen) {
            RECT rc{0, 0, w, h};
            if (FAILED(rt_->BindDC(dc_, &rc))) return nullptr;
            boundW_ = w;
            boundH_ = h;
            boundGen_ = gen;
        }
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

    // Draw on the CPU: cheaper for small surfaces redrawn every frame (no
    // copy back from the GPU). Set before the first Begin().
    bool software = false;

   private:
    int boundW_ = -1, boundH_ = -1;
    uint32_t boundGen_ = ~0u;
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
