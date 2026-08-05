// hook.h : 全局低级鼠标钩子（独立线程 + 无锁 SPSC 环形缓冲）
#pragma once
#include "common.h"
#include <atomic>

namespace mf {

enum MouseEvtType : uint8_t {
    ME_Move = 0,
    ME_LDown,
    ME_RDown,
    ME_MDown,
};

struct MouseEvt {
    float        x, y;
    double       t;
    MouseEvtType type;
};

inline constexpr int kEvtCap = 512;   // 2 的幂

class MouseHook {
public:
    bool Start();
    void Stop();
    bool Running() const { return running_.load(std::memory_order_relaxed); }

    // 主线程消费；返回取出的事件数
    int Drain(MouseEvt* out, int maxOut);

    // 钩子线程内部使用
    void PushEvent(const MouseEvt& e);

private:
    static DWORD WINAPI ThreadMain(LPVOID param);

    HANDLE            thread_ = nullptr;
    DWORD             tid_    = 0;
    std::atomic<bool> running_{ false };

    MouseEvt              ring_[kEvtCap];
    std::atomic<uint32_t> head_{ 0 };   // 写入位置（钩子线程）
    std::atomic<uint32_t> tail_{ 0 };   // 读取位置（主线程）
};

} // namespace mf
