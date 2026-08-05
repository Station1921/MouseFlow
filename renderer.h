// renderer.h : 全屏透明覆盖层（分层窗口 + 软件 ARGB 渲染）
//
// 架构说明（2026-08-04 彻底重写）：
//   旧实现用 WS_EX_NOREDIRECTIONBITMAP + DirectComposition，DComp 合成层会覆盖 Win32
//   命中测试，导致"点击穿透"在 MinGW 下无解（SetHitTestable 不被支持）。
//
//   新实现改用**分层窗口（WS_EX_LAYERED）+ 软件逐像素渲染**：
//   - 每帧把粒子软件绘制到一张全屏 ARGB 离屏缓冲；
//   - 像素 alpha=0 的区域：DWM 按 alpha 命中测试，鼠标点击自动穿透到下层窗口；
//   - 像素 alpha>0 的区域：正常显示特效。
//   点击穿透因此"开箱即用"，无需任何 DComp hack；且覆盖层里没有任何原生控件，
//   白底/错位问题从源头消失。
#pragma once
#include "common.h"
#include "particles.h"

namespace mf {

class Overlay {
public:
    ~Overlay();

    bool Init(HINSTANCE hInst);
    void Shutdown();

    // 虚拟桌面尺寸变化时重建离屏缓冲
    void OnDisplayChange();

    // 绘制一帧。内部自动管理可见性：无存活粒子时隐藏窗口（零开销）。
    bool Render(const FxEngine& fx, const EffectDef& trailDef, float globalOpacity);

    void SetVisible(bool v);
    bool Visible() const { return visible_; }
    HWND Hwnd() const { return hwnd_; }

    // 屏幕坐标 -> 覆盖层局部坐标
    Vec2 ToLocal(Vec2 screen) const { return { screen.x - (float)vx_, screen.y - (float)vy_ }; }

    int  Width()  const { return vw_; }
    int  Height() const { return vh_; }

    // 供设置界面：把某个特效渲染到调用方提供的 ARGB(BGRA) 缓冲里。
    //   buf      : 大小为 W*H 的 uint32_t 数组（每像素 0xAARRGGBB，小端即 B,G,R,A）。
    //   darkBackground : true=用深色填充背景（不透明预览）；false=保持透明（调用方自行合成）。
    static void RenderFxToBuffer(uint32_t* buf, int W, int H,
                                 const FxEngine& fx, const EffectDef& def,
                                 float opacity, bool darkBackground);

private:
    bool AllocBuffer();
    void FreeBuffer();
    void Present();          // 把 bits_ 通过 UpdateLayeredWindow 提交到屏幕
    void Hide();

    HWND  hwnd_ = nullptr;
    int   vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;
    bool  visible_ = false;
    bool  ready_ = false;

    HBITMAP hbmp_  = nullptr;   // 离屏 DIB（全虚拟屏幕分辨率）
    uint32_t* bits_ = nullptr;  // DIB 像素（预乘 alpha 的 BGRA）
    HDC     hdcMem_ = nullptr;
    int     bufW_ = 0, bufH_ = 0;
};

} // namespace mf
