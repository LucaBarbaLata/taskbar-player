
namespace tp {

// ============================================================================
// Taskbar integration
// ============================================================================

static UINT g_msgAttach = 0, g_msgDetach = 0, g_msgSettings = 0;
static Widget* g_widget = nullptr;
static std::atomic<bool> g_attached{false};
static bool g_controllerStarted = false;  // core thread
static int g_attachAttempts = 0;          // core thread

#ifdef TP_HARNESS
static const wchar_t* kTaskbarClass = L"TP_FakeTaskbar";
#else
static const wchar_t* kTaskbarClass = L"Shell_TrayWnd";
#endif

static void RegisterClasses() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = ModuleInstance();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = Widget::Proc;
    wc.lpszClassName = Widget::kClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = Popup::Proc;
    wc.lpszClassName = Popup::kClass;
    RegisterClassExW(&wc);
    // The GPU popup gets the system's rounded drop shadow.
    wc.style = CS_DROPSHADOW;
    wc.lpszClassName = Popup::kClassComposited;
    RegisterClassExW(&wc);
}

static void UnregisterClasses() {
    UnregisterClassW(Widget::kClass, ModuleInstance());
    UnregisterClassW(Popup::kClass, ModuleInstance());
    UnregisterClassW(Popup::kClassComposited, ModuleInstance());
}

static void CreateWidget(HWND taskbar) {
    g_widget = new Widget();
    if (!g_widget->Create(taskbar)) {
        delete g_widget;
        g_widget = nullptr;
        return;
    }
    g_uiHwnd = g_widget->Hwnd();
    g_ui.SetSnapshot(LatestSnapshot());
    g_widget->OnSnapshot();
}

static void DestroyWidget() {
    g_uiHwnd = nullptr;
    if (g_widget) {
        g_widget->Destroy();
        delete g_widget;
        g_widget = nullptr;
    }
    g_widgetVisible = false;
}

// Runs on the taskbar thread.
static void UiInit(HWND taskbar) {
    if (g_gfx) return;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    g_gfx = new Gfx();
    if (!g_gfx->Init()) {
        Wh_Log(L"Direct2D initialization failed");
        delete g_gfx;
        g_gfx = nullptr;
        return;
    }
    RegisterClasses();
    g_popup = new Popup();
    if (!g_popup->Create(taskbar)) {
        delete g_popup;
        g_popup = nullptr;
    }
    CreateWidget(taskbar);
    Wh_Log(L"UI created");
}

// Runs on the taskbar thread.
static void UiShutdown() {
    if (g_popup) {
        g_popup->Destroy();
        delete g_popup;
        g_popup = nullptr;
    }
    DestroyWidget();
    g_ui.SetSnapshot(nullptr);
    if (g_gfx) {
        UnregisterClasses();
        delete g_gfx;
        g_gfx = nullptr;
    }
}

// Runs on the taskbar thread.
static void UiApplySettings(HWND taskbar) {
    if (!g_gfx) return;
    if (g_widget && g_widget->Overlay() != GetSettings()->overlayMode) {
        DestroyWidget();
        CreateWidget(taskbar);
    } else if (g_widget) {
        g_widget->OnSettingsChanged();
    }
    if (g_popup && g_popup->CreatedClassic() != GetSettings()->popupClassic) {
        // The popup engine changed: rebuild the popup window.
        g_popup->Destroy();
        delete g_popup;
        g_popup = new Popup();
        if (!g_popup->Create(taskbar)) {
            delete g_popup;
            g_popup = nullptr;
        }
    }
    PopupApplySettings();
}

static void TryAttach();

static LRESULT CALLBACK TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, DWORD_PTR) {
    if (msg == g_msgAttach && msg) {
        UiInit(hwnd);
        return 0;
    }
    if (msg == g_msgDetach && msg) {
        UiShutdown();
        return 0;
    }
    if (msg == g_msgSettings && msg) {
        UiApplySettings(hwnd);
        return 0;
    }
    switch (msg) {
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (g_widget) PostMessageW(g_widget->Hwnd(), WM_TP_SETTINGS, 0, 0);
            break;
        case WM_WINDOWPOSCHANGED:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (g_widget) PostMessageW(g_widget->Hwnd(), WM_TP_RELAYOUT, 0, 0);
            break;
        case WM_NCDESTROY:
            // The taskbar is going away (e.g. explorer restarting it).
            UiShutdown();
            g_taskbarHwnd = nullptr;
            g_attached = false;
            g_core.Post([] { TryAttach(); }, 2000);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static HWND FindOwnTaskbar() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, kTaskbarClass, nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return h;
    }
    return nullptr;
}

// Runs on the core thread until the taskbar of this explorer process is found.
static void TryAttach() {
    if (g_shuttingDown || g_attached) return;
    HWND tb = FindOwnTaskbar();
    if (tb && WindhawkUtils::SetWindowSubclassFromAnyThread(tb, TaskbarSubclassProc, 0)) {
        g_taskbarHwnd = tb;
        g_attached = true;
        g_attachAttempts = 0;
        Trace(L"Attached to the taskbar");
        // Only the explorer process that owns the taskbar runs the rest of the
        // mod. Helper explorer.exe processes stay at a single idle thread.
        Http::Init();
        g_spot.Start();
        g_cmd.Start();
        g_img.Start();
        if (!g_controllerStarted) {
            g_controllerStarted = true;
            g_controller.Init();
        }
        SendMessageW(tb, g_msgAttach, 0, 0);
        g_controller.Publish();
        return;
    }
    g_attachAttempts++;
    g_core.Post([] { TryAttach(); }, g_attachAttempts < 60 ? 1000 : 5000);
}

static void ModInit() {
    LoadSettings();
    InitTrace();
    Trace(L"Mod init (pid %u, clientIdSet=%d)", GetCurrentProcessId(), !GetSettings()->clientId.empty());
    g_shuttingDown = false;
    g_authCancel = false;
    g_msgAttach = RegisterWindowMessageW(L"TaskbarPlayer_Attach");
    g_msgDetach = RegisterWindowMessageW(L"TaskbarPlayer_Detach");
    g_msgSettings = RegisterWindowMessageW(L"TaskbarPlayer_Settings");
    g_core.Start();
    g_core.Post([] { TryAttach(); });
}

static void ModUninit() {
    g_shuttingDown = true;
    g_authCancel = true;
    HWND tb = g_taskbarHwnd;
    if (tb && g_attached) {
        SendMessageW(tb, g_msgDetach, 0, 0);
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(tb, TaskbarSubclassProc);
    }
    g_attached = false;
    g_taskbarHwnd = nullptr;
    Http::CancelAll();
    g_core.Sync([] { g_controller.Shutdown(); });
    g_img.Sync([] { ReleaseImgThreadObjects(); });
    g_spot.Stop();
    g_cmd.Stop();
    g_img.Stop();
    g_core.Stop();
    if (g_authThread.joinable()) g_authThread.join();
    Http::Cleanup();
    std::lock_guard<std::mutex> lock(g_wicMutex);
    g_wic = nullptr;
}

static void ModSettingsChanged() {
    LoadSettings();
    g_core.Post([] { g_controller.OnSettingsChanged(); });
    HWND tb = g_taskbarHwnd;
    if (tb && g_attached) SendMessageW(tb, g_msgSettings, 0, 0);
}

}  // namespace tp

BOOL Wh_ModInit() {
    Wh_Log(L"Taskbar Player starting");
    tp::ModInit();
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"Taskbar Player stopping");
    tp::ModUninit();
}

void Wh_ModSettingsChanged() {
    tp::ModSettingsChanged();
}
