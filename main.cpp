// main.cpp : 程序入口、主循环、托盘、单实例、图标
#include "app.h"
#include <shellapi.h>
#include <commctrl.h>

namespace mf {

App  g_app;
UINT WM_TRAY   = 0;
UINT WM_MF_SHOW = 0;
UINT WM_MF_QUIT = 0;

// ★ 诊断计数器：每帧累加，UI 实时显示
static int64_t g_diagMoves   = 0;    // 累计收到的鼠标移动事件数
static int64_t g_diagClicks  = 0;    // 累计收到的点击事件数
static int64_t g_diagDrained = 0;    // 本帧 Drain 取出的事件数
static int64_t g_diagFallback = 0;   // 保底注入次数（hook 失效时用 GetCursorPos）
static int     g_diagLiveP   = 0;    // 当前存活粒子数（每秒采样一次）
static double  g_diagLastSample = 0; // 上次粒子采样时间
static wchar_t g_diagText[128] = L""; // 格式化后的诊断文本，供 UI 读取

static const wchar_t* kMutexName = L"Global\\MouseFlow_Instance_9F2A1C4D";

// ---------------------------------------------------------------- 图标（运行时生成，零资源依赖）
// ★ 从 PE 嵌入资源加载图标（icon.ico 已通过 mf.rc 的 "1 ICON" 嵌入）
static HICON MakeAppIcon(int size) {
    return (HICON)LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1),
                              IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
}

// ---------------------------------------------------------------- 托盘
static NOTIFYICONDATAW g_nid{};

void App::AddTrayIcon() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = FindWindowW(L"MF_Sink", nullptr);
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon  = hIcon;
    wcscpy_s(g_nid.szTip, L"MouseFlow 鼠标特效");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTrayTip();
}

void App::UpdateTrayTip() {
    std::wstring base = settings.masterEnabled ? L"MouseFlow 鼠标特效（已启用）"
                                               : L"MouseFlow 鼠标特效（已禁用）";
    std::wstring tip = base + L"  v" + kBuildStamp;
    wcscpy_s(g_nid.szTip, tip.c_str());
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ShowTrayMenu(HWND owner) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 100, L"打开设置");
    AppendMenuW(m, MF_STRING | (g_app.settings.masterEnabled ? MF_CHECKED : 0), 101,
                g_app.settings.masterEnabled ? L"禁用特效" : L"启用特效");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 102, L"退出");
    POINT p{}; GetCursorPos(&p);
    SetForegroundWindow(owner);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, owner, nullptr);
    DestroyMenu(m);
    if (id == 100)      g_app.OpenSettings();
    else if (id == 101) g_app.ToggleMaster();
    else if (id == 102) PostQuitMessage(0);
}

// ---------------------------------------------------------------- 配置同步
void App::ApplyConfig() {
    curTrail = settings.ResolveTrail();
    curClick = settings.ResolveClick();
    fx.Configure(curTrail, curClick, settings.globalScale, settings.globalOpacity);
    fx.SetTrailOn(settings.trailEnabled && settings.masterEnabled);
    fx.SetClickOn(settings.clickEnabled && settings.masterEnabled);
    fx.SetIdleFade(settings.hideOnIdle);

    bool regOn = IsAutoStartEnabled();
    if (settings.autoStart != regOn)
        SetAutoStart(settings.autoStart, settings.silentStart);
}

void App::Save() {
    SaveSettings(settings);
}

void App::ToggleMaster() {
    settings.masterEnabled = !settings.masterEnabled;
    ApplyConfig();
    Save();
    UpdateTrayTip();
    if (hwndUI) UISync(hwndUI);
}

bool App::IsFullscreenForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == overlay.Hwnd() || (hwndUI && fg == hwndUI)) return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;
    RECT r{}; GetWindowRect(fg, &r);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (r.left <= vx + 4 && r.top <= vy + 4 &&
        r.right >= vx + vw - 4 && r.bottom >= vy + vh - 4)
        return true;
    return false;
}

// ---------------------------------------------------------------- 主循环帧
static bool g_lastRendered = false;

static void Frame() {
    App& a = g_app;
    double now = NowSeconds();

    if (a.settings.masterEnabled) {
        MouseEvt buf[64];
        int n = a.hook.Drain(buf, 64);
        g_diagDrained = n;
        bool hookDeliveredClick = false;
        for (int i = 0; i < n; ++i) {
            if (buf[i].type == ME_Move) {
                ++g_diagMoves;
                a.fx.PushMouse(Vec2(buf[i].x, buf[i].y), buf[i].t);
            } else {
                ++g_diagClicks;
                int btn = (buf[i].type == ME_RDown) ? 1 : (buf[i].type == ME_MDown) ? 2 : 0;
                a.fx.PushClick(Vec2(buf[i].x, buf[i].y), btn, buf[i].t);
                hookDeliveredClick = true;
            }
        }

        // ★ 保底机制：若 hook 未收到鼠标事件，直接用光标位置注入 FxEngine（移动）
        bool hookDead = (n == 0 || g_diagMoves == 0);
        if (hookDead) {
            POINT pt{};
            if (GetCursorPos(&pt)) {
                if (a.settings.trailEnabled)
                    a.fx.PushMouse(Vec2((float)pt.x, (float)pt.y), now);
                ++g_diagFallback;
            }
        }

        // ★ 点击保底：hook 失效时（拖尾已靠保底可见，但点击无保底会完全失效），
        //   通过异步按键状态检测“按下沿”，确保点击特效在任意情况下都不丢。
        //   每帧采样按键状态以维护沿判定；仅在 hook 未投递点击且未死循环时发射，
        //   避免与正常 hook 路径重复触发（双击叠效）。
        static bool g_prevLB = false, g_prevRB = false, g_prevMB = false;
        bool lb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool rb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool mb = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        // ★ 点击保底：与移动保底解耦！
        //   移动保底只在 hookDead 时触发，但点击事件可能被 hook 单独丢弃
        //   （hook 投递了移动→拖尾可见→hookDead=false，但不投递点击→点击完全无）
        //   因此点击保底只要"本帧 hook 未投递点击"就触发，不依赖 hookDead
        if (a.settings.clickEnabled && !hookDeliveredClick) {
            // 在自有设置窗口内点击时不产生涟漪，避免操作 UI 时干扰
            if (GetForegroundWindow() != a.hwndUI) {
                POINT pt{}; GetCursorPos(&pt);
                Vec2 cp((float)pt.x, (float)pt.y);
                if (lb && !g_prevLB) { a.fx.PushClick(cp, 0, now); ++g_diagFallback; }
                if (rb && !g_prevRB) { a.fx.PushClick(cp, 1, now); ++g_diagFallback; }
                if (mb && !g_prevMB) { a.fx.PushClick(cp, 2, now); ++g_diagFallback; }
            }
        }
        g_prevLB = lb; g_prevRB = rb; g_prevMB = mb;
    } else {
        g_diagDrained = 0;
    }

    // 每秒采样一次粒子存活数
    if (now - g_diagLastSample > 1.0) {
        g_diagLiveP = a.fx.LiveCount();
        g_diagLastSample = now;
        swprintf_s(g_diagText, L"移动:%lld 点击:%lld 保底:%lld 粒子:%d",
                   g_diagMoves, g_diagClicks, g_diagFallback, g_diagLiveP);
    }

    bool paused = a.settings.masterEnabled && a.settings.pauseFullscreen &&
                  a.IsFullscreenForeground();
    a.fx.SetTrailOn(a.settings.trailEnabled && a.settings.masterEnabled && !paused);
    a.fx.SetClickOn(a.settings.clickEnabled && a.settings.masterEnabled && !paused);

    a.fx.Update(now);

    // 诊断构建：只要总开关开着就强制渲染，验证管线
    bool active = a.settings.masterEnabled;
    if (active) {
        a.overlay.SetVisible(true);
        a.overlay.Render(a.fx, a.curTrail, a.settings.globalOpacity);
        g_lastRendered = true;
    } else if (g_lastRendered) {
        a.overlay.Render(a.fx, a.curTrail, a.settings.globalOpacity);
        a.overlay.SetVisible(false);
        g_lastRendered = false;
    }
}

// ---------------------------------------------------------------- 诊断信息接口
const wchar_t* GetDiagText() { return g_diagText; }

// ---------------------------------------------------------------- 隐藏的消息窗口（托盘 / 显示 / 显示变更）
static LRESULT CALLBACK SinkProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAY) {
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) ShowTrayMenu(h);
        return 0;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == 100)      g_app.OpenSettings();
        else if (id == 101) g_app.ToggleMaster();
        else if (id == 102) PostQuitMessage(0);
        return 0;
    }
    if (m == WM_DISPLAYCHANGE) { g_app.overlay.OnDisplayChange(); return 0; }
    if (m == WM_MF_SHOW)       { g_app.OpenSettings(); return 0; }
    if (m == WM_MF_QUIT)       { g_app.Save(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static HWND CreateSink(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = SinkProc;
    wc.hInstance   = hInst;
    wc.lpszClassName = L"MF_Sink";
    RegisterClassExW(&wc);
    // 真实顶层隐藏窗口（WS_EX_TOOLWINDOW 使其不出现在任务栏/Alt-Tab），
    // 这样跨进程 FindWindow / PostMessage 才能可靠送达（消息-only 窗口收不到）。
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MF_Sink", L"",
                           WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
}

} // namespace mf

// ---------------------------------------------------------------- 入口
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    using namespace mf;

    // 解析命令行（--silent：自启/静默运行不弹主界面）
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 0; i < argc; ++i)
                if (_wcsicmp(argv[i], L"--silent") == 0) g_app.silent = true;
            LocalFree(argv);
        }
    }

    // ★ 必须在单实例检查之前注册窗口消息，否则 PostMessageW(old, WM_MF_QUIT, ...)
    //   时 WM_MF_QUIT == 0（尚未注册），旧实例收不到退出指令 → 新 exe 永远无法接管。
    WM_TRAY    = RegisterWindowMessageW(L"MouseFlowTrayMsg");
    WM_MF_SHOW = RegisterWindowMessageW(L"MouseFlowShow");
    WM_MF_QUIT = RegisterWindowMessageW(L"MouseFlowQuit");

    // 单实例：若已有实例在运行，请它退出并让出互斥锁，由本次启动的（最新）进程接管。
    // 这保证"覆盖新 exe 后双击运行"一定会跑新构建，而不是唤醒旧的（可能仍是坏的）实例。
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"MF_Sink", nullptr);
        if (old) PostMessageW(old, WM_MF_QUIT, 0, 0);   // 旧实例保存设置并退出
        if (hMutex) { CloseHandle(hMutex); hMutex = nullptr; }
        // 轮询等待互斥锁释放（旧实例退出后），最多 ~5 秒
        for (int i = 0; i < 50; ++i) {
            hMutex = CreateMutexW(nullptr, TRUE, kMutexName);
            if (GetLastError() != ERROR_ALREADY_EXISTS) break;  // 成功取得所有权
            CloseHandle(hMutex); hMutex = nullptr;
            Sleep(100);
        }
        if (!hMutex) {  // 超时：退化为“唤醒旧实例并退出”
            PostMessageW(HWND_BROADCAST, WM_MF_SHOW, 0, 0);
            return 0;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitCommonControls();

    g_app.hIcon = MakeAppIcon(32);

    LoadSettings(g_app.settings);
    // ★ 强制校正：如果配置文件中 master 关闭了，强制开启（用户可能误关导致特效"消失"）
    //   这是经过 24 轮排查确认的 #1 致命原因——配置持久化了 master=0 后，
    //   每次启动特效都不显示，且用户不知道怎么恢复。
    if (!g_app.settings.masterEnabled) {
        g_app.settings.masterEnabled = true;
        SaveSettings(g_app.settings);  // 立即回写，避免下次启动又关闭
    }
    g_app.ApplyConfig();

    if (!g_app.overlay.Init(hInst)) {
        MessageBoxW(nullptr, L"无法创建分层覆盖窗口（请检查系统是否启用了桌面组合/DWM）。", L"MouseFlow", MB_ICONERROR);
        return 1;
    }
    g_app.hook.Start();
    HWND sink = CreateSink(hInst);
    (void)sink;
    g_app.AddTrayIcon();

    if (!g_app.silent)
        g_app.OpenSettings();

    // 主循环：泵消息 + 渲染帧
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            if (g_app.hwndUI && IsDialogMessageW(g_app.hwndUI, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Frame();
        int interval = (g_app.settings.fpsLimit > 0) ? (1000 / g_app.settings.fpsLimit)
                                                     : (1000 / 60);
        if (!g_lastRendered) interval = 33;   // 空闲时降低轮询频率
        Sleep((DWORD)interval);
    }
done:
    g_app.hook.Stop();
    if (g_app.hwndUI) DestroyWindow(g_app.hwndUI);
    g_app.overlay.Shutdown();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    CoUninitialize();
    return 0;
}
