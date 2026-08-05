#include "hook.h"

namespace mf {

static MouseHook* g_hook = nullptr;
static HHOOK      g_hhook = nullptr;

static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_hook) {
        const MSLLHOOKSTRUCT* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        MouseEvt e{};
        e.x = (float)ms->pt.x;
        e.y = (float)ms->pt.y;
        e.t = NowSeconds();
        bool want = true;
        switch (wParam) {
            case WM_MOUSEMOVE:   e.type = ME_Move;  break;
            case WM_LBUTTONDOWN: e.type = ME_LDown; break;
            case WM_RBUTTONDOWN: e.type = ME_RDown; break;
            case WM_MBUTTONDOWN: e.type = ME_MDown; break;
            default: want = false; break;
        }
        // 忽略程序注入的合成事件，避免自激
        if (want && !(ms->flags & LLMHF_INJECTED))
            g_hook->PushEvent(e);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void MouseHook::PushEvent(const MouseEvt& e) {
    uint32_t h = head_.load(std::memory_order_relaxed);
    uint32_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= (uint32_t)kEvtCap) {
        // 缓冲满：丢弃最旧的移动事件，保证按键不丢
        if (e.type == ME_Move) return;
        tail_.store(t + 1, std::memory_order_release);
    }
    ring_[h & (kEvtCap - 1)] = e;
    head_.store(h + 1, std::memory_order_release);
}

int MouseHook::Drain(MouseEvt* out, int maxOut) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    uint32_t h = head_.load(std::memory_order_acquire);
    int n = 0;
    while (t != h && n < maxOut) {
        out[n++] = ring_[t & (kEvtCap - 1)];
        ++t;
    }
    tail_.store(t, std::memory_order_release);
    return n;
}

DWORD WINAPI MouseHook::ThreadMain(LPVOID param) {
    MouseHook* self = static_cast<MouseHook*>(param);
    g_hook = self;
    g_hhook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    if (!g_hhook) {
        self->running_.store(false, std::memory_order_release);
        return 1;
    }
    self->running_.store(true, std::memory_order_release);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hhook);
    g_hhook = nullptr;
    g_hook = nullptr;
    self->running_.store(false, std::memory_order_release);
    return 0;
}

bool MouseHook::Start() {
    if (thread_) return true;
    head_.store(0);
    tail_.store(0);
    thread_ = CreateThread(nullptr, 64 * 1024, ThreadMain, this, 0, &tid_);
    if (!thread_) return false;
    // 等待钩子安装完成（最多 1 秒）
    for (int i = 0; i < 200 && !running_.load(std::memory_order_acquire); ++i)
        Sleep(5);
    return running_.load(std::memory_order_acquire);
}

void MouseHook::Stop() {
    if (!thread_) return;
    PostThreadMessageW(tid_, WM_QUIT, 0, 0);
    WaitForSingleObject(thread_, 2000);
    CloseHandle(thread_);
    thread_ = nullptr;
    tid_ = 0;
    running_.store(false, std::memory_order_release);
}

} // namespace mf
