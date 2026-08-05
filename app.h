// app.h : 全局应用状态与跨模块接口
#pragma once
#include "config.h"
#include "particles.h"
#include "renderer.h"
#include "hook.h"

namespace mf {

struct App {
    Settings  settings;
    Overlay   overlay;
    MouseHook hook;
    FxEngine  fx;
    EffectDef curTrail{};
    EffectDef curClick{};
    bool      silent   = false;
    HWND      hwndUI   = nullptr;
    HICON     hIcon    = nullptr;
    bool      uiOpen   = false;

    // main.cpp 实现
    void ApplyConfig();            // 把 settings 同步到引擎/钩子/注册表
    void Save();                   // 持久化到磁盘
    bool IsFullscreenForeground(); // 全屏检测
    void ToggleMaster();           // 托盘快捷开关
    void AddTrayIcon();
    void UpdateTrayTip();

    // ui.cpp 实现
    void OpenSettings();
    void HideSettingsToTray();
};

extern App g_app;

// 托盘回调 / 单实例激活消息
extern UINT WM_TRAY;
extern UINT WM_MF_SHOW;
extern UINT WM_MF_QUIT;   // 请求已有实例退出，让新实例接管

// UI -> 主状态同步（切换特效、改参数后刷新控件）
void UISync(HWND hwnd);

// 诊断：返回当前事件/粒子计数文本（UI 标题栏显示）
const wchar_t* GetDiagText();

} // namespace mf
