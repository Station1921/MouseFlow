// config.h : 设置数据与持久化（扁平键值，UTF-8，零依赖）
#pragma once
#include "common.h"
#include "effects.h"
#include <map>

namespace mf {

// 单个特效的用户自定义状态（切换特效后再切回来，参数依然保留）
struct EffectTweak {
    float    p[P_NUMERIC];
    Color4f  colorA;
    Color4f  colorB;
    bool     hasColor = false;   // 是否覆盖了默认配色
    bool     hasParam = false;   // 是否覆盖了默认参数
};

enum ThemeMode : int { Theme_System = 0, Theme_Light = 1, Theme_Dark = 2 };

struct Settings {
    // --- 总开关 ---
    bool  masterEnabled  = true;    // 特效总开关（托盘可快速切换）
    bool  trailEnabled   = true;
    bool  clickEnabled   = true;

    // --- 当前选中的特效 ---
    std::wstring trailId = L"neon-comet";
    std::wstring clickId = L"ripple";

    // --- 全局表现 ---
    float globalOpacity  = 1.0f;    // 0.1 ~ 1.0
    float globalScale    = 1.0f;    // 0.5 ~ 2.0
    int   fpsLimit       = 0;       // 0 = 跟随显示器刷新率，否则 30/60/120/144

    // --- 行为 ---
    bool  autoStart      = false;   // 开机自启
    bool  silentStart    = true;    // 自启时静默（不弹主界面）
    bool  minimizeToTray = true;    // 关闭窗口最小化到托盘
    bool  pauseFullscreen= false;   // 全屏应用时自动暂停
    bool  hideOnIdle     = true;    // 鼠标静止时淡出拖尾

    // --- 外观 ---
    int   theme          = Theme_System;

    // --- 每个特效的自定义参数 ---
    std::map<std::wstring, EffectTweak> tweaks;

    // 窗口位置（-1 表示居中）
    int   winX = -1, winY = -1, winW = 0, winH = 0;

    // 解析出的有效特效定义（应用 tweak 后）
    EffectDef ResolveTrail() const;
    EffectDef ResolveClick() const;

    // 取某特效的当前参数值（不存在 tweak 则返回默认）
    float GetParam(const EffectDef& def, bool isTrail, int pid) const;
    void  SetParam(const EffectDef& def, bool isTrail, int pid, float v);
    Color4f GetColorA(const EffectDef& def, bool isTrail) const;
    Color4f GetColorB(const EffectDef& def, bool isTrail) const;
    void  SetColorA(const EffectDef& def, bool isTrail, Color4f c);
    void  SetColorB(const EffectDef& def, bool isTrail, Color4f c);
    void  ResetEffect(const EffectDef& def, bool isTrail);
};

// 配置文件路径 %LOCALAPPDATA%\MouseFlow\settings.cfg
std::wstring ConfigDir();
std::wstring ConfigPath();

bool LoadSettings(Settings& s);
bool SaveSettings(const Settings& s);

// 开机自启（HKCU\...\Run），静默模式追加 --silent
bool IsAutoStartEnabled();
bool SetAutoStart(bool on, bool silent);

} // namespace mf
