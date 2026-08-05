// ui.cpp : 全自绘设置界面（2026-08-04 第四版 —— 特效页布局架构重做）
//
// 本版针对用户第36轮截图反馈，彻底重做 拖尾效果(页1) / 点击特效(页2) 的布局：
//   ① 参数面板标签不可见 → 行高加大(52px)，标签区18px+滑块区28px清晰分层
//   ② 模块盖住文字 → 严格垂直流布局：列表 | 预览→配色→重置→参数，无重叠
//   ③ 设置不能滚动 → 重写滚轮命中检测 + 边界计算
//   ④ 进入特效页后点不了其他设置 → 确保无 SetCapture 泄漏 + 命中检测顺序正确
//   ⑤ 列表文字截断 → item 宽度自适应 + DT_END_ELLIPSIS
//
// 架构：单一 WS_POPUP 窗口，WM_PAINT 全自绘，零原生控件。
// 绘制管线：WM_PAINT → BeginPaint → EnsureBackBuffer → 全部绘制到内存 DC → BitBlt → EndPaint
#include "app.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <vector>
#include <string>

namespace mf {

// ================================================================ 常量 / 布局
static const int kWinW = 1020, kWinH = 780;
static const int kSideW = 200;
static const int kHeaderH = 52;
static const int kPreviewTimer = 1;

static inline int ContentX0() { return kSideW + 24; }
static inline int ContentX1() { return kWinW - 24; }
static inline int ContentW()  { return ContentX1() - ContentX0(); }

enum {
    IDC_LIST_TRAIL = 100, IDC_LIST_CLICK = 101,
    IDC_COL_A = 300, IDC_COL_B = 301, IDC_BTN_RESET = 430,
};

struct T {
    COLORREF bg, panel, card, accent, text, sub, border, hover;
    bool dark;
    Gdiplus::Color gpBg, gpPanel, gpCard, gpAccent, gpText, gpSub, gpBorder, gpHover;
};

static bool SystemPrefersDark() {
    HKEY hk{};
    DWORD v = 1, cb = sizeof(v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&v, &cb);
        RegCloseKey(hk);
    }
    return v == 0;
}

static inline Gdiplus::Color ToGP(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static T MakeTheme(bool dark) {
    COLORREF accent = RGB(32, 140, 240);
    T t;
    t.dark = dark;
    if (dark) {
        t.bg = RGB(24,24,28); t.panel = RGB(34,34,40); t.card = RGB(44,44,52);
        t.accent = accent; t.text = RGB(232,232,240); t.sub = RGB(140,140,156);
        t.border = RGB(56,56,64); t.hover = RGB(52,52,62);
    } else {
        t.bg = RGB(245,245,248); t.panel = RGB(238,238,242); t.card = RGB(255,255,255);
        t.accent = accent; t.text = RGB(32,32,38); t.sub = RGB(100,100,108);
        t.border = RGB(220,220,228); t.hover = RGB(240,240,245);
    }
    t.gpBg     = ToGP(t.bg);     t.gpPanel  = ToGP(t.panel);
    t.gpCard   = ToGP(t.card);   t.gpAccent = ToGP(t.accent);
    t.gpText   = ToGP(t.text);   t.gpSub    = ToGP(t.sub);
    t.gpBorder = ToGP(t.border); t.gpHover  = ToGP(t.hover);
    return t;
}
static T Theme() {
    bool dark = false;
    if (g_app.settings.theme == Theme_Dark) dark = true;
    else if (g_app.settings.theme == Theme_System) dark = SystemPrefersDark();
    return MakeTheme(dark);
}

// ================================================================ GDI+ 全局状态
static ULONG_PTR g_gdiplusToken = 0;
static bool GdiplusInit() {
    if (g_gdiplusToken) return true;
    Gdiplus::GdiplusStartupInput si{};
    si.GdiplusVersion = 2;
    si.DebugEventCallback = nullptr;
    si.SuppressBackgroundThread = FALSE;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
    return st == Gdiplus::Ok;
}
static void GdiplusDone() {
    if (g_gdiplusToken) { Gdiplus::GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }
}

// ==================================================== 双缓冲后端
static HBITMAP g_backBmp = nullptr;
static uint32_t* g_backBits = nullptr;
static HDC g_backDC = nullptr;
static int g_backW = 0, g_backH = 0;

static void EnsureBackBuffer(int W, int H) {
    if (g_backDC && W == g_backW && H == g_backH) return;
    if (g_backDC) { DeleteDC(g_backDC); g_backDC = nullptr; }
    if (g_backBmp) { DeleteObject(g_backBmp); g_backBmp = nullptr; g_backBits = nullptr; }
    g_backW = W; g_backH = H;
    HDC hdcScreen = GetDC(nullptr);
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi); bi.biWidth = W; bi.biHeight = -H;
    bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
    g_backBmp = CreateDIBSection(hdcScreen, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&g_backBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!g_backBmp) return;
    g_backDC = CreateCompatibleDC(nullptr);
    SelectObject(g_backDC, g_backBmp);
}

static void FreeBackBuffer() {
    if (g_backDC) { DeleteDC(g_backDC); g_backDC = nullptr; }
    if (g_backBmp) { DeleteObject(g_backBmp); g_backBmp = nullptr; g_backBits = nullptr; }
    g_backW = g_backH = 0;
}

// ==================================================== 三级字体系统
static HFONT g_uiFont     = nullptr;   // 正文
static HFONT g_uiFontBold = nullptr;   // 标题 bold
static HFONT g_uiFontSmall = nullptr;  // 小字

static HFONT CreateUIFont(int size, bool bold) {
    LOGFONTW lf{};
    lf.lfHeight = -size;
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    lf.lfPitchAndFamily = FF_SWISS | VARIABLE_PITCH;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}
static void InitFonts() {
    if (!g_uiFont)       g_uiFont       = CreateUIFont(13, false);
    if (!g_uiFontBold)   g_uiFontBold   = CreateUIFont(16, true);
    if (!g_uiFontSmall)  g_uiFontSmall  = CreateUIFont(11, false);
}
static void FreeFonts() {
    if (g_uiFont)      { DeleteObject(g_uiFont);      g_uiFont = nullptr; }
    if (g_uiFontBold)  { DeleteObject(g_uiFontBold);  g_uiFontBold = nullptr; }
    if (g_uiFontSmall) { DeleteObject(g_uiFontSmall); g_uiFontSmall = nullptr; }
}

// ==================================================== 状态
static HWND  g_hwnd = nullptr;
static int   g_curPage = 0;
static int   g_paramScroll = 0;
static int   g_listScroll  = 0;

struct PRow { int pid; float lo, hi; int dec; bool isTrail; int y; };
static std::vector<PRow> g_rows;

static FxEngine  g_previewFx;
static bool      g_previewIsTrail = true;
static EffectDef g_previewDef{};
static int       g_selTrail = 0, g_selClick = 0;

// FPS 下拉框状态
static bool      g_fpsDropdownOpen = false;
static RECT      g_fpsDropdownRc{};   // 按钮区域（用于绘制弹出框定位）
static const int g_pvW = 360, g_pvH = 150;
static std::vector<uint32_t> g_previewBuf;
static DWORD g_customColors[16] = { 0 };

// ====================================================
//  ★★★ 特效页面布局（绝对不重叠版本）★★★
//
//  窗口 1020×780，侧边栏 200，标题栏 52
//  内容区 x=200, w=820
//
//  垂直流（从上到下，严格递增，无重叠）：
//    [62] 页面标题          h=28  ("拖尾效果" / "点击特效")
//    [96] 区域标签          h=16  ("预设列表" / "实时预览")
//   [118] ★ 内容起始线 ★
//
//   左侧列表区：x=200, w=258, y=118, h=650
//   右侧内容区：x=478, w=526
//     [118] 预览            h=140
//     [266] gap             =8
//     [274] 配色            h=34
//     [316] gap             =8
//     [324] 重置            h=28
//     [362] gap             =10
//     [372] 参数面板        h=剩余(396)
//
//   参数每行 50px：标签行18 + 间距4 + 滑块行28
// ====================================================

static int EffListX()  { return ContentX0(); }
static int EffListW()  { return 258; }
static int EffListY()  { return kHeaderH + 66; }   // 标题+标签之后
static int EffListH()  { return kWinH - EffListY() - 12; }

static int EffRightX() { return ContentX0() + EffListW() + 20; }
static int EffRightW() { return ContentX1() - EffRightX(); }

static int EffPreviewY() { return EffListY(); }       // 与列表顶部对齐
static int EffPreviewH() { return 140; }

static int EffColorY()  { return EffPreviewY() + EffPreviewH() + 8; }
static int EffColorH()  { return 34; }

static int EffResetY()  { return EffColorY() + EffColorH() + 8; }
static int EffResetH()  { return 28; }

static int EffParamY()  { return EffResetY() + EffResetH() + 10; }
static int EffParamH()  { return kWinH - EffParamY() - 12; }

// 参数行高
static const int kParamRowH = 50;

// ==================================================== 绘制工具：文字
static void DrawTextAA(HDC hdc, const wchar_t* txt, const RECT& rc, COLORREF col,
                       UINT fmt, HFONT fontOverride = nullptr) {
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);
    HFONT useFont = fontOverride ? fontOverride : g_uiFont;
    HFONT old = (HFONT)SelectObject(hdc, useFont);
    DrawTextW(hdc, txt, -1, (RECT*)&rc, fmt);
    SelectObject(hdc, old);
}

// ==================================================== GDI+ 抗锯齿形状
static void GP_FillRoundRect(Gdiplus::Graphics& g, int x, int y, int w, int h, int r, const Gdiplus::Color& c) {
    if (w <= 0 || h <= 0) return;
    r = (std::min)(r, (std::min)(w, h) / 2);
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush br(c);
    g.FillPath(&br, &path);
}
static void GP_StrokeRoundRect(Gdiplus::Graphics& g, int x, int y, int w, int h, int r, const Gdiplus::Color& c, float width = 1.0f) {
    if (w <= 0 || h <= 0) return;
    r = (std::min)(r, (std::min)(w, h) / 2);
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    Gdiplus::Pen pn(c, width);
    g.DrawPath(&pn, &path);
}
static void DrawRoundRect(Gdiplus::Graphics& g, const RECT& rc, const T& t, int radius,
                           const Gdiplus::Color& fill, bool hasBorder = true,
                           const Gdiplus::Color* borderCol = nullptr) {
    GP_FillRoundRect(g, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, radius, fill);
    if (hasBorder)
        GP_StrokeRoundRect(g, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, radius,
                           borderCol ? *borderCol : t.gpBorder);
}
static void DrawRoundRect(Gdiplus::Graphics& g, const RECT& rc, int radius,
                           const Gdiplus::Color& fill, bool hasBorder = true,
                           const Gdiplus::Color* borderCol = nullptr) {
    GP_FillRoundRect(g, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, radius, fill);
    if (hasBorder && borderCol)
        GP_StrokeRoundRect(g, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, radius, *borderCol);
}
static void GP_FillCircle(Gdiplus::Graphics& g, int cx, int cy, int r, const Gdiplus::Color& c) {
    if (r <= 0) return;
    Gdiplus::SolidBrush br(c);
    g.FillEllipse(&br, cx - r, cy - r, r * 2, r * 2);
}
static void GP_StrokeCircle(Gdiplus::Graphics& g, int cx, int cy, int r, const Gdiplus::Color& c, float w = 1.0f) {
    if (r <= 0) return;
    Gdiplus::Pen pn(c, w);
    g.DrawEllipse(&pn, cx - r, cy - r, r * 2, r * 2);
}

// ==================================================== 滑块
static void DrawSlider(Gdiplus::Graphics& g, HDC hdc, const RECT& rc, int pos,
                        const wchar_t* valText, const T& t) {
    int mid = rc.top + (rc.bottom - rc.top) / 2;
    int trkH = 3, margin = 4;
    {
        Gdiplus::SolidBrush trkBr(t.dark ? Gdiplus::Color(50, 50, 58) : Gdiplus::Color(210, 210, 218));
        g.FillRectangle(&trkBr, rc.left, mid - trkH / 2, rc.right - rc.left, trkH);
    }
    int fillW = (rc.right - rc.left - margin * 2) * pos / 1000;
    if (fillW > 0) {
        Gdiplus::SolidBrush fillBr(t.gpAccent);
        g.FillRectangle(&fillBr, rc.left + margin, mid - trkH / 2, (std::max)(fillW, 1), trkH);
    }
    int thumbW = 14, thumbH = 20;
    int thumbX = rc.left + margin + fillW - thumbW / 2;
    thumbX = Clampi(thumbX, rc.left + margin, rc.right - margin - thumbW);
    RECT thumbRc{ thumbX, mid - thumbH / 2, thumbX + thumbW, mid + thumbH / 2 };
    DrawRoundRect(g, thumbRc, t, 4,
                  t.dark ? Gdiplus::Color(235, 235, 242) : Gdiplus::Color(255, 255, 255));
    if (valText && valText[0]) {
        RECT vt{ rc.right - 56, mid - 9, rc.right, mid + 9 };
        DrawTextAA(hdc, valText, vt, t.sub, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, g_uiFontSmall);
    }
}

static int SliderPosFromX(const RECT& rc, int x) {
    if (rc.right <= rc.left) return 0;
    int margin = 4;
    int p = (x - rc.left - margin) * 1000 / (rc.right - rc.left - margin * 2);
    return Clampi(p, 0, 1000);
}

// ==================================================== Toggle
static void DrawToggle(Gdiplus::Graphics& g, HDC hdc, const RECT& rc, bool on, const T& t) {
    int h = rc.bottom - rc.top, w = rc.right - rc.left;
    int r = h / 2 - 2, cy = (rc.top + rc.bottom) / 2;
    int x0 = rc.left + 3, x1 = rc.right - 3;
    DrawRoundRect(g, { x0, cy - r, x1, cy + r }, t, r, on ? t.gpAccent : t.gpBorder, false);
    int knobR = r - 3, kx = on ? (x1 - r - 2) : (x0 + r + 2);
    GP_FillCircle(g, kx, cy, knobR, Gdiplus::Color(255, 255, 255));
}

// ==================================================== 侧边栏
static const wchar_t* kNav[] = { L"概览", L"拖尾效果", L"点击特效", L"通用设置", L"关于" };

static void DrawSidebar(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    Gdiplus::SolidBrush pnlBr(t.gpPanel);
    g.FillRectangle(&pnlBr, 0, 0, kSideW, kWinH);
    Gdiplus::Pen sepPen(t.gpBorder, 1.0f);
    g.DrawLine(&sepPen, kSideW, 0, kSideW, kWinH);

    int top = 20, rh = 40, gap = 4;
    for (int i = 0; i < 5; ++i) {
        int y = top + i * (rh + gap);
        RECT r{ 8, y, kSideW - 8, y + rh };
        if (g_curPage == i) {
            DrawRoundRect(g, r, t, 8, t.gpHover, false);
            GP_FillRoundRect(g, r.left + 10, r.top + 10, 4, r.bottom - r.top - 20, 2, t.gpAccent);
        }
        DrawTextAA(hdc, kNav[i], { r.left + 18, r.top, r.right, r.bottom },
                   g_curPage == i ? t.accent : t.text, DT_SINGLELINE | DT_VCENTER);
    }
}

// ==================================================== 标题栏
static void DrawHeader(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    Gdiplus::SolidBrush bgBr(t.gpBg);
    g.FillRectangle(&bgBr, kSideW, 0, kWinW - kSideW, kHeaderH);

    std::wstring title = std::wstring(L"MouseFlow") + L"   v" + kBuildStamp;
    DrawTextAA(hdc, title.c_str(), { kSideW + 24, 0, kWinW - 120, kHeaderH },
               t.text, DT_SINGLELINE | DT_VCENTER);

    int bw = 34, bh = 28, by = (kHeaderH - bh) / 2;
    RECT rmin{ kWinW - 20 - bw * 2 - 8, by, kWinW - 20 - bw - 8, by + bh };
    DrawRoundRect(g, rmin, t, 6, t.gpHover, false);
    DrawTextAA(hdc, L"\u2014", rmin, t.text, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    RECT rb{ kWinW - 20 - bw, by, kWinW - 20, by + bh };
    DrawRoundRect(g, rb, 6, Gdiplus::Color(224, 64, 64), false);
    DrawTextAA(hdc, L"\u00D7", rb, RGB(255, 255, 255), DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

// ====================================================
//  ★ 预设列表（自绘，确保文字不截断）
// ====================================================
static void DrawPresetList(Gdiplus::Graphics& g, HDC hdc, const T& t, bool isTrail) {
    int n = isTrail ? kTrailEffectCount : kClickEffectCount;
    int x = EffListX(), w = EffListW(), y0 = EffListY(), itemH = 70;

    // ★ 双重裁剪：GDI + GDI+（两者裁剪系统独立，必须分别设置）
    int saved = SaveDC(hdc);
    HRGN listClip = CreateRectRgn(x, y0, x + w, y0 + EffListH());
    ExtSelectClipRgn(hdc, listClip, RGN_COPY);
    DeleteObject(listClip);

    // GDI+ 独立裁剪（Graphics 对象不继承 GDI 裁剪区域）
    Gdiplus::Region oldGpClip;
    g.GetClip(&oldGpClip);
    Gdiplus::Rect gpClipR(x, y0, w, EffListH());
    g.SetClip(gpClipR);  // Rect 重载，等价于 Replace 模式

    for (int i = 0; i < n; ++i) {
        int yy = y0 + i * itemH - g_listScroll;
        RECT ir{ x, yy, x + w, yy + itemH - 4 };
        if (ir.bottom < y0 || ir.top > y0 + EffListH()) continue;
        InflateRect(&ir, -6, -2);
        const EffectDef& d = isTrail ? kTrailEffects[i] : kClickEffects[i];
        int cur = isTrail ? g_selTrail : g_selClick;
        bool sel = (i == cur);

        DrawRoundRect(g, ir, t, 8, t.gpCard);
        if (sel) {
            GP_FillRoundRect(g, ir.left + 2, ir.top + 2, 3, ir.bottom - ir.top - 4, 2, t.gpAccent);
        }

        // 名称 — 充足宽度，省略号截断
        RECT nameRc{ ir.left + 12, ir.top + 6, ir.right - 64, ir.top + 26 };
        DrawTextAA(hdc, d.name, nameRc, t.text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        // 描述
        RECT descRc{ ir.left + 12, ir.top + 26, ir.right - 64, ir.top + 46 };
        DrawTextAA(hdc, d.desc, descRc, t.sub, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, g_uiFontSmall);

        // 使用按钮
        int btnW = 50, btnH = 22;
        RECT brc{ ir.right - btnW - 6, ir.top + (ir.bottom - ir.top - btnH) / 2,
                  ir.right - 6, ir.top + (ir.bottom - ir.top - btnH) / 2 + btnH };
        if (sel) {
            DrawRoundRect(g, brc, t, 5, t.gpHover);
            DrawTextAA(hdc, L"使用中", brc, t.accent, DT_SINGLELINE | DT_CENTER | DT_VCENTER, g_uiFontSmall);
        } else {
            DrawRoundRect(g, brc, 5, t.gpAccent, false);
            DrawTextAA(hdc, L"使用", brc, RGB(255, 255, 255), DT_SINGLELINE | DT_CENTER | DT_VCENTER, g_uiFontSmall);
        }
    }

    // ★ 恢复双重裁剪区域（GDI + GDI+）
    g.SetClip(&oldGpClip);  // 恢复 GDI+ 裁剪
    RestoreDC(hdc, saved);   // 恢复 GDI 裁剪
}

// ====================================================
//  ★ 实时预览（裁剪+等比缩放）
// ====================================================
static void RenderPreviewToBuf() {
    if (g_previewBuf.empty()) g_previewBuf.resize((size_t)g_pvW * g_pvH);
    Overlay::RenderFxToBuffer(g_previewBuf.data(), g_pvW, g_pvH,
                              g_previewFx, g_previewDef,
                              g_app.settings.globalOpacity, true);
}
static void DrawPreview(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    int x = EffRightX(), y = EffPreviewY(), w = EffRightW(), h = EffPreviewH();
    DrawRoundRect(g, { x, y, x + w, y + h }, t, 10, t.gpCard);

    RenderPreviewToBuf();

    RECT inner{ x + 6, y + 6, x + w - 6, y + h - 6 };
    int iw = inner.right - inner.left, ih = inner.bottom - inner.top;

    // ★ 用 SaveDC/RestoreDC 安全地保存恢复裁剪区域（防止泄漏导致后续GDI文字被裁掉）
    int saved = SaveDC(hdc);
    HRGN clipRgn = CreateRectRgn(inner.left, inner.top, inner.right, inner.bottom);
    ExtSelectClipRgn(hdc, clipRgn, RGN_COPY);
    DeleteObject(clipRgn);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi); bi.biWidth = g_pvW; bi.biHeight = -g_pvH;
    bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
    float srcAspect = (float)g_pvW / (float)g_pvH;
    float dstAspect = (float)iw / (float)ih;
    int dw, dh, dx, dy;
    if (dstAspect > srcAspect) {
        dh = ih; dw = (int)(ih * srcAspect);
        dx = inner.left + (iw - dw) / 2; dy = inner.top;
    } else {
        dw = iw; dh = (int)(iw / srcAspect);
        dx = inner.left; dy = inner.top + (ih - dh) / 2;
    }
    StretchDIBits(hdc, dx, dy, (std::max)(dw, 1), (std::max)(dh, 1),
                  0, 0, g_pvW, g_pvH, g_previewBuf.data(), (BITMAPINFO*)&bi,
                  DIB_RGB_COLORS, SRCCOPY);

    // ★ 恢复裁剪区域（关键！不恢复则后续所有GDI文字被静默裁剪掉）
    RestoreDC(hdc, saved);
    // 注：外层 DrawRoundRect 已提供卡片+单边框，不再额外画内层边框
}

// ==================================================== 配色块
static void DrawColorSwatch(Gdiplus::Graphics& g, HDC hdc, const RECT& rc,
                             const Color4f& col, const wchar_t* label, const T& t) {
    COLORREF c = RGB((int)(col.r * 255), (int)(col.g * 255), (int)(col.b * 255));
    DrawRoundRect(g, rc, t, 8, ToGP(c));
    // ★ 始终白色文字 + 水平居中
    DrawTextAA(hdc, label, rc, RGB(255, 255, 255),
               DT_SINGLELINE | DT_CENTER | DT_VCENTER, g_uiFontSmall);
}

// ==================================================== 重置按钮
static void DrawResetButton(Gdiplus::Graphics& g, HDC hdc, const RECT& rc, const T& t) {
    DrawRoundRect(g, rc, t, 6, t.gpHover);
    DrawTextAA(hdc, L"重置当前特效", rc, t.text, DT_SINGLELINE | DT_CENTER | DT_VCENTER, g_uiFontSmall);
}

// ==================================================== 复选框
static void DrawSelfChk(Gdiplus::Graphics& g, HDC hdc, int x, int y,
                         const wchar_t* txt, bool checked, const T& t) {
    int box = 15;
    RECT boxR{ x, y, x + box, y + box };
    if (checked)
        DrawRoundRect(g, boxR, t, 3, t.gpAccent);
    else
        DrawRoundRect(g, boxR, t, 3, t.dark ? Gdiplus::Color(60, 60, 68) : Gdiplus::Color(200, 200, 208));
    if (checked)
        DrawTextAA(hdc, L"\u2713", { x - 1, y - 1, x + box + 1, y + box + 1 },
                    RGB(255, 255, 255), DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    DrawTextAA(hdc, txt, { x + box + 8, y, x + 300, y + box + 2 },
               t.text, DT_SINGLELINE | DT_VCENTER);
}

// ==================================================== 单选按钮
static void DrawSelfRadio(Gdiplus::Graphics& g, HDC hdc, int x, int y,
                           const wchar_t* txt, bool sel, const T& t, int tw) {
    int rd = 8;
    int cx = x + rd, cy = y + rd;  // 圆心
    if (sel) {
        GP_FillCircle(g, cx, cy, rd + 1, t.gpAccent);       // 外圈（accent色）
        GP_FillCircle(g, cx, cy, rd - 2, t.gpCard);          // 内圈（卡片色）
        GP_FillCircle(g, cx, cy, rd - 4, Gdiplus::Color(255, 255, 255)); // ★ 白点居中（圆心一致）
    } else {
        GP_FillCircle(g, cx, cy, rd + 1, t.gpCard);
        GP_StrokeCircle(g, cx, cy, rd + 1, t.gpSub, 1.0f);
    }
    DrawTextAA(hdc, txt, { x + rd * 2 + 8, y, x + tw, y + rd * 2 + 2 },
               sel ? t.accent : t.text, DT_SINGLELINE | DT_VCENTER);
}

// ====================================================
//  ★★★ 参数面板（完全重做 —— 标签必可见）★★★
//
//  每行 kParamRowH(50px) 分两层：
//    [y+0 .. y+18]  标签行：参数名(左) + 数值(右)
//    [y+22 .. y+48] 滑块行：滑块(全宽)
// ====================================================
static void BuildParamRows(bool isTrail) {
    g_rows.clear();
    const EffectDef& d = isTrail ? g_app.curTrail : g_app.curClick;
    int y = 0;
    for (int pid = 0; pid < P_NUMERIC; ++pid) {
        if (!(d.exposed & Bit(pid))) continue;
        const ParamMeta& m = kParamMeta[pid];
        g_rows.push_back({ pid, m.lo, m.hi, m.decimals, isTrail, y });
        y += kParamRowH;
    }
}
static void RebuildParamPanel() {
    BuildParamRows(g_previewIsTrail);
    g_paramScroll = 0;
}

// 参数名标签区域（上层）—— 使用 padL=20, padR=14
static RECT ParamLabelRect(const PRow& r) {
    int px = EffRightX(), py = EffParamY(), pw = EffRightW();
    int padL = 20, padT = 16;
    return { px + padL, py + padT + r.y - g_paramScroll,
             px + pw - 46, py + padT + r.y - g_paramScroll + 18 };
}

// 参数滑块区域（下层）
static RECT ParamSliderRect(const PRow& r) {
    int px = EffRightX(), py = EffParamY(), pw = EffRightW();
    int padL = 20, padT = 16;
    return { px + padL, py + padT + r.y + 22 - g_paramScroll,
             px + pw - 14, py + padT + r.y + 48 - g_paramScroll };
}

static void DrawParamPanel(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    int x = EffRightX(), y = EffParamY(), w = EffRightW(), h = EffParamH();

    // 面板背景卡片
    DrawRoundRect(g, { x, y, x + w, y + h }, t, 10, t.gpCard);

    // ★ 内边距：内容不贴边（左20 右14 上16 下16）
    int padL = 20, padR = 14, padT = 16;
    int contentH = h - padT - 16;  // 减去上下内边距

    for (auto& r : g_rows) {
        int rowTop = y + padT + r.y - g_paramScroll;

        // 整行可见性判断（相对于内容区）
        if (rowTop + kParamRowH < y + padT || rowTop > y + h - 16) continue;

        const EffectDef& d = r.isTrail ? g_app.curTrail : g_app.curClick;
        float cur = g_app.settings.GetParam(d, r.isTrail, r.pid);
        wchar_t buf[64];
        const ParamMeta& m = kParamMeta[r.pid];
        if (m.decimals == 0) swprintf_s(buf, L"%d%s", (int)(cur + 0.5f), m.suffix);
        else if (m.decimals == 1) swprintf_s(buf, L"%.1f%s", cur, m.suffix);
        else swprintf_s(buf, L"%.2f%s", cur, m.suffix);

        // ★ 第一层：参数名（左）+ 数值（右）—— 高18px，充足空间
        RECT labelRc = ParamLabelRect(r);
        // 确保标签在面板内才绘制
        if (labelRc.bottom > y + padT && labelRc.top < y + h - 16) {
            DrawTextAA(hdc, m.name, { x + padL, rowTop, x + w - padR, rowTop + 18 },
                       t.text, DT_SINGLELINE | DT_VCENTER);
            DrawTextAA(hdc, buf, { x + w - 46, rowTop, x + w - 10, rowTop + 18 },
                       t.sub, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, g_uiFontSmall);
        }

        // ★ 第二层：滑块 —— 高26px（从rowTop+22到rowTop+48）
        RECT sliderRc = ParamSliderRect(r);
        if (sliderRc.bottom > y + padT && sliderRc.top < y + h - 16) {
            int pos = (int)((cur - r.lo) / (r.hi - r.lo) * 1000.0f);
            DrawSlider(g, hdc, sliderRc, Clampi(pos, 0, 1000), nullptr, t);
        }
    }
}

// ==================================================== 页面绘制

// ---- 概览页 ----
static void DrawOverviewPage(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    int x0 = ContentX0(), w = ContentW(), y = kHeaderH + 20;
    DrawTextAA(hdc, L"概览", { x0, y, x0 + w, y + 26 }, t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);
    y += 36;

    auto card = [&](const wchar_t* name, const wchar_t* desc, bool on, int yp) {
        RECT rc{ x0, yp, x0 + w, yp + 72 };
        DrawRoundRect(g, rc, t, 10, t.gpCard);
        DrawTextAA(hdc, name, { rc.left + 16, rc.top + 12, rc.right - 80, rc.top + 32 },
                   t.text, DT_SINGLELINE | DT_VCENTER);
        DrawTextAA(hdc, desc, { rc.left + 16, rc.top + 34, rc.right - 80, rc.top + 54 },
                   t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);
        DrawToggle(g, hdc, { rc.right - 60, rc.top + 20, rc.right - 16, rc.top + 48 }, on, t);
    };
    card(L"鼠标拖尾", L"全局透明覆盖层，不影响桌面点击。", g_app.settings.trailEnabled, y); y += 82;
    card(L"点击特效", L"点击任意窗口或桌面时播放动画。", g_app.settings.clickEnabled, y); y += 82;

    RECT st{ x0, y, x0 + w, y + 120 };
    DrawRoundRect(g, st, t, 10, t.gpCard);
    DrawTextAA(hdc, L"当前状态", { st.left + 16, st.top + 12, st.right - 16, st.top + 32 },
               t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);
    struct KV { const wchar_t* k; std::wstring v; int o; };
    KV kvs[] = {
        { L"渲染架构", L"分层窗口 \u00B7 点击穿透", 40 },
        { L"当前拖尾", std::wstring(g_app.curTrail.name), 64 },
        { L"点击特效", std::wstring(L"已启用 \u00B7 ") + g_app.curClick.name, 88 },
    };
    for (auto& kv : kvs) {
        DrawTextAA(hdc, kv.k, { st.left + 16, st.top + kv.o, st.left + 110, st.top + kv.o + 18 },
                   t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);
        DrawTextAA(hdc, kv.v.c_str(), { st.left + 116, st.top + kv.o, st.right - 16, st.top + kv.o + 18 },
                   t.text, DT_SINGLELINE | DT_VCENTER);
    }
}

// ---- ★ 特效页（全新布局流） ----
static void DrawEffectPage(Gdiplus::Graphics& g, HDC hdc, const T& t, bool isTrail) {
    int x0 = ContentX0(), w = ContentW();

    // ★ 页面标题（在列表上方，绝对不重叠）
    int titleY = kHeaderH + 10;
    DrawTextAA(hdc, isTrail ? L"拖尾效果" : L"点击特效",
               { x0, titleY, x0 + w, titleY + 28 }, t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);

    // ★ 区域标签（标题下方、列表上方）
    int labelY = titleY + 32;
    DrawTextAA(hdc, L"预设列表", { EffListX(), labelY, EffListX() + 100, labelY + 16 },
               t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);
    DrawTextAA(hdc, L"实时预览", { EffRightX(), labelY, EffRightX() + 100, labelY + 16 },
               t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);

    // ① 预设列表（左侧）—— EffListY() 已在标题+标签下方
    DrawPresetList(g, hdc, t, isTrail);

    // ② 预览（右上）
    DrawPreview(g, hdc, t);

    // ③ 配色块（预览下方）
    int cw = (EffRightW() - 8) / 2;
    DrawColorSwatch(g, hdc, { EffRightX(), EffColorY(), EffRightX() + cw, EffColorY() + EffColorH() },
                  g_app.settings.GetColorA(g_previewIsTrail ? g_app.curTrail : g_app.curClick, g_previewIsTrail),
                  L"起始颜色", t);
    DrawColorSwatch(g, hdc, { EffRightX() + cw + 8, EffColorY(), EffRightX() + EffRightW(), EffColorY() + EffColorH() },
                  g_app.settings.GetColorB(g_previewIsTrail ? g_app.curTrail : g_app.curClick, g_previewIsTrail),
                  L"结束颜色", t);

    // ④ 重置按钮
    DrawResetButton(g, hdc, { EffRightX(), EffResetY(), EffRightX() + EffRightW(), EffResetY() + EffResetH() }, t);

    // ⑤ 参数面板（最下方，占满剩余空间）
    DrawParamPanel(g, hdc, t);
}

// ---- 通用设置页（加大卡片/修正单选对齐）----
static void DrawSettingsPage(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    int x0 = ContentX0(), w = ContentW(), y = kHeaderH + 20;
    DrawTextAA(hdc, L"通用设置", { x0, y, x0 + w, y + 24 }, t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);
    y += 32;

    // ====== 卡片1：外观基础（加大高度+下边距）=======
    RECT c1{ x0, y, x0 + w, y + 172 };
    DrawRoundRect(g, c1, t, 10, t.gpCard);
    DrawTextAA(hdc, L"外观基础", { x0 + 16, y + 12, x0 + w - 16, y + 32 },
               t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);

    int sx  = x0 + 28;
    int slx = x0 + 120;
    int slr = x0 + w - 20;
    int rowH = 42;  // 加大行高，更舒适

    int ry = y + 46;  // 标题下方留更多空间
    // 不透明度
    DrawTextAA(hdc, L"不透明度", { sx, ry, slx - 8, ry + rowH }, t.text, DT_SINGLELINE | DT_VCENTER);
    RECT opRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
    DrawSlider(g, hdc, opRc, (int)(g_app.settings.globalOpacity * 1000), nullptr, t);
    wchar_t ob[16]; swprintf_s(ob, L"%d%%", (int)(g_app.settings.globalOpacity * 100 + 0.5f));
    DrawTextAA(hdc, ob, { slr - 48, ry, slr, ry + rowH }, t.sub, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, g_uiFontSmall);

    // 缩放比例
    ry += rowH;
    DrawTextAA(hdc, L"缩放比例", { sx, ry, slx - 8, ry + rowH }, t.text, DT_SINGLELINE | DT_VCENTER);
    RECT scRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
    int scPos = (int)((g_app.settings.globalScale - 0.3f) / (3.0f - 0.3f) * 1000.0f);
    DrawSlider(g, hdc, scRc, scPos, nullptr, t);
    wchar_t sb[16]; swprintf_s(sb, L"%.1fx", g_app.settings.globalScale);
    DrawTextAA(hdc, sb, { slr - 48, ry, slr, ry + rowH }, t.sub, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, g_uiFontSmall);

    // 主题
    ry += rowH;
    DrawTextAA(hdc, L"主题", { sx, ry, slx - 8, ry + rowH }, t.text, DT_SINGLELINE | DT_VCENTER);
    int radioW = (slr - slx) / 3;
    int rby = ry + (rowH - 18) / 2;
    DrawSelfRadio(g, hdc, slx, rby, L"跟随系统", g_app.settings.theme == Theme_System, t, radioW);
    DrawSelfRadio(g, hdc, slx + radioW, rby, L"浅色", g_app.settings.theme == Theme_Light, t, radioW);
    DrawSelfRadio(g, hdc, slx + radioW * 2, rby, L"深色", g_app.settings.theme == Theme_Dark, t, radioW);

    // ====== 卡片2：系统与性能（加大高度+充足下边距）=======
    y += 184;  // 卡片间距
    RECT c2{ x0, y, x0 + w, y + 300 };  // 280→300，+20px下边距
    DrawRoundRect(g, c2, t, 10, t.gpCard);
    DrawTextAA(hdc, L"系统与性能", { x0 + 16, y + 10, x0 + w - 16, y + 30 },
               t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);

    struct ChkDef { const wchar_t* txt; bool* val; };
    ChkDef chks[] = {
        { L"开机自启动",           &g_app.settings.autoStart },
        { L"自启动时静默（不弹界面）", &g_app.settings.silentStart },
        { L"关闭窗口时最小化到托盘", &g_app.settings.minimizeToTray },
        { L"全屏应用时自动暂停",   &g_app.settings.pauseFullscreen },
        { L"鼠标静止时淡出拖尾",   &g_app.settings.hideOnIdle },
    };
    int cy = y + 36;
    for (auto& c : chks) {
        DrawSelfChk(g, hdc, sx, cy + (rowH - 15) / 2, c.txt, *c.val, t);
        cy += rowH;
    }

    DrawTextAA(hdc, L"帧率限制", { sx, cy, slx - 8, cy + rowH }, t.text, DT_SINGLELINE | DT_VCENTER);
    const wchar_t* fpsLabels[] = { L"跟随刷新率", L"30 FPS", L"60 FPS", L"120 FPS", L"144 FPS" };
    int fpsVals[] = { 0, 30, 60, 120, 144 };
    int sel = 0; for (int i = 0; i < 5; ++i) if (g_app.settings.fpsLimit == fpsVals[i]) sel = i;
    RECT fpsRc{ slx, cy + (rowH - 24) / 2, slx + 170, cy + (rowH - 24) / 2 + 24 };
    g_fpsDropdownRc = fpsRc;  // 记录区域供下拉框定位
    DrawRoundRect(g, fpsRc, t, 5, g_fpsDropdownOpen ? t.gpHover : t.gpBg);
    // ★ 小字体 + 居中
    DrawTextAA(hdc, fpsLabels[sel], { slx + 10, fpsRc.top, slx + 145, fpsRc.bottom },
               t.text, DT_SINGLELINE | DT_VCENTER | DT_LEFT, g_uiFontSmall);
    DrawTextAA(hdc, L"\u25BE", { fpsRc.right - 22, fpsRc.top, fpsRc.right - 4, fpsRc.bottom },
               t.sub, DT_SINGLELINE | DT_CENTER | DT_VCENTER, g_uiFontSmall);
}

// ---- 关于页 ----
static void DrawAboutPage(Gdiplus::Graphics& g, HDC hdc, const T& t) {
    int x0 = ContentX0(), w = ContentW(), y = kHeaderH + 20;
    DrawTextAA(hdc, L"关于", { x0, y, x0 + w, y + 26 }, t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);
    y += 36;
    RECT rc{ x0, y, x0 + w, y + 130 };
    DrawRoundRect(g, rc, t, 10, t.gpCard);
    DrawTextAA(hdc, L"MouseFlow", { rc.left + 20, rc.top + 20, rc.right - 20, rc.top + 46 },
               t.text, DT_SINGLELINE | DT_VCENTER, g_uiFontBold);
    DrawTextAA(hdc, (std::wstring(L"版本 ") + kBuildStamp + L" \u00B7 TrailFX 1.0 \u00B7 ClickFX 1.0").c_str(),
               { rc.left + 20, rc.top + 48, rc.right - 20, rc.top + 70 },
               t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);
    DrawTextAA(hdc, L"C++ \u00B7 Win32 \u00B7 分层窗口 \u00B7 软件渲染（点击穿透）",
               { rc.left + 20, rc.top + 74, rc.right - 20, rc.top + 96 },
               t.sub, DT_SINGLELINE | DT_VCENTER, g_uiFontSmall);
}

// ==================================================== 同步
static void SyncPreviewConfig() {
    EffectDef tr = g_app.settings.ResolveTrail();
    EffectDef cl = g_app.settings.ResolveClick();
    g_previewFx.Configure(tr, cl, g_app.settings.globalScale, g_app.settings.globalOpacity);
    g_previewDef = g_previewIsTrail ? tr : cl;
}
static void SelectEffect(bool isTrail, int idx) {
    if (isTrail) {
        g_selTrail = idx;
        g_app.settings.trailId = kTrailEffects[idx].id;
    } else {
        g_selClick = idx;
        g_app.settings.clickId = kClickEffects[idx].id;
    }
    g_app.ApplyConfig();
    g_previewIsTrail = isTrail;
    SyncPreviewConfig();
    RebuildParamPanel();
    g_app.Save();
}
static void FullSync() {
    g_selTrail = (std::max)(0, FindTrailIndex(g_app.settings.trailId));
    g_selClick = (std::max)(0, FindClickIndex(g_app.settings.clickId));
    g_previewIsTrail = (g_curPage == 1);
    SyncPreviewConfig();
    RebuildParamPanel();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ==================================================== 预览动画计时器
static void PreviewTick() {
    double now = NowSeconds();
    if (g_previewIsTrail) {
        Vec2 ctr{ (float)g_pvW * 0.5f, (float)g_pvH * 0.5f };
        g_previewFx.SeedPreviewPath(ctr, (float)(std::min)(g_pvW, g_pvH) * 0.33f, now);
    } else {
        static double last = 0;
        if (now - last > 0.85) { g_previewFx.PushClick(Vec2((float)g_pvW * 0.5f, (float)g_pvH * 0.5f), 0, now); last = now; }
    }
    g_previewFx.Update(now);
    RECT pr{ EffRightX(), EffPreviewY(), EffRightX() + EffRightW(), EffPreviewY() + EffPreviewH() };
    InvalidateRect(g_hwnd, &pr, FALSE);
}

// ==================================================== 前向声明
static PRow* g_dragRow = nullptr;
static int   g_setDrag = 0;
static int   g_dragPos = 0;
static void ShowPageInit();
static void OpenColorPicker(HWND owner, const EffectDef& d, bool isTrail, bool isA);
static void FullSync();

// ====================================================
//  ★★★ 窗口过程（重点重做特效页命中检测）★★★
// ====================================================
static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) {
        g_hwnd = h;
        GdiplusInit();
        InitFonts();
        FullSync();
        if (g_rows.empty()) RebuildParamPanel();
        g_previewFx.Configure(g_app.settings.ResolveTrail(), g_app.settings.ResolveClick(),
                              g_app.settings.globalScale, g_app.settings.globalOpacity);
        SetTimer(h, kPreviewTimer, 16, nullptr);
        ShowPageInit();
        return 0;
    }
    if (m == WM_DESTROY) {
        KillTimer(h, kPreviewTimer);
        FreeBackBuffer();
        FreeFonts();
        GdiplusDone();
        g_hwnd = nullptr;
        return 0;
    }
    if (m == WM_CLOSE) {
        if (g_app.settings.minimizeToTray) { DestroyWindow(h); g_app.hwndUI = nullptr; g_app.uiOpen = false; return 0; }
        PostQuitMessage(0); return 0;
    }
    if (m == WM_DPICHANGED) {
        RECT* rp = (RECT*)l;
        SetWindowPos(h, nullptr, rp->left, rp->top, rp->right - rp->left, rp->bottom - rp->top, SWP_NOZORDER | SWP_NOACTIVATE);
        FreeBackBuffer();
        return 0;
    }
    if (m == WM_SIZE) { return 0; }

    // ---------- NCHITTEST ----------
    if (m == WM_NCHITTEST) {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        // ★ 侧边栏区域始终返回 HTCLIENT（否则"概览"项上半部分落入标题栏区返回HTCAPTION导致无法点击）
        if (p.x < kSideW) return HTCLIENT;
        if (p.y < kHeaderH) {
            int bw = 34, bh = 28, by = (kHeaderH - bh) / 2;
            RECT rb{ kWinW - 20 - bw, by, kWinW - 20, by + bh };
            RECT rmin{ kWinW - 20 - bw * 2 - 8, by, kWinW - 20 - bw - 8, by + bh };
            if (PtInRect(&rb, p) || PtInRect(&rmin, p)) return HTCLIENT;
            return HTCAPTION;
        }
        int mm = 6; RECT r; GetClientRect(h, &r);
        if (p.x < mm) return HTLEFT;
        if (p.x > r.right - mm) return HTRIGHT;
        if (p.y < mm) return HTTOP;
        if (p.y > r.bottom - mm) return HTBOTTOM;
        return HTCLIENT;
    }

    // ---------- 鼠标左键 ----------
    if (m == WM_LBUTTONDOWN) {
        // ★ 安全释放可能残留的 capture（防止之前某处SetCapture后LBUTTONUP丢失导致后续点击全部被吞噬）
        if (GetCapture() == h) { ReleaseCapture(); g_dragRow = nullptr; g_setDrag = 0; }

        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        POINT pt{ x, y };

        // ★ 优先级1：标题栏按钮（最高优先级，不受页面影响）
        if (y < kHeaderH) {
            int bw = 34, bh = 28, by = (kHeaderH - bh) / 2;
            RECT rb{ kWinW - 20 - bw, by, kWinW - 20, by + bh };
            RECT rmin{ kWinW - 20 - bw * 2 - 8, by, kWinW - 20 - bw - 8, by + bh };
            if (PtInRect(&rb, pt)) { SendMessageW(h, WM_CLOSE, 0, 0); return 0; }
            if (PtInRect(&rmin, pt)) { DestroyWindow(h); g_app.hwndUI = nullptr; g_app.uiOpen = false; return 0; }
        }

        // ★ 优先级2：侧边栏导航（最高优先级 —— 解决"点了特效页就点不了其他设置"）
        if (x < kSideW) {
            int top = 20, rh = 40, gap = 4;
            int i = (y - top) / (rh + gap);
            if (i >= 0 && i < 5) {
                g_curPage = i;
                if (i == 1 || i == 2) g_previewIsTrail = (i == 1);
                SyncPreviewConfig(); RebuildParamPanel();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
        }

        // ★ 优先级3：概览页开关
        if (g_curPage == 0) {
            int cardY1 = kHeaderH + 20 + 36, cardY2 = cardY1 + 82;
            int x0 = ContentX0(), cw = ContentW();
            RECT tr1{ x0 + cw - 60, cardY1 + 20, x0 + cw - 16, cardY1 + 48 };
            RECT tr2{ x0 + cw - 60, cardY2 + 20, x0 + cw - 16, cardY2 + 48 };
            if (PtInRect(&tr1, pt)) {
                g_app.settings.trailEnabled = !g_app.settings.trailEnabled;
                g_app.ApplyConfig(); g_app.Save(); g_app.UpdateTrayTip(); InvalidateRect(h, nullptr, FALSE); return 0;
            }
            if (PtInRect(&tr2, pt)) {
                g_app.settings.clickEnabled = !g_app.settings.clickEnabled;
                g_app.ApplyConfig(); g_app.Save(); g_app.UpdateTrayTip(); InvalidateRect(h, nullptr, FALSE); return 0;
            }
        }

        // ★ 优先级4：特效页内容（仅页1/2）
        if (g_curPage == 1 || g_curPage == 2) {
            bool isTrail = (g_curPage == 1);

            // --- 4a: 预设列表点击 ---
            int n = isTrail ? kTrailEffectCount : kClickEffectCount;
            int itemH = 70;
            for (int i = 0; i < n; ++i) {
                int yy = EffListY() + i * itemH - g_listScroll;
                RECT ir{ EffListX(), yy, EffListX() + EffListW(), yy + itemH - 4 };
                if (ir.bottom < EffListY() || ir.top > EffListY() + EffListH()) continue;
                InflateRect(&ir, -6, -2);
                int btnW = 50;
                RECT brc{ ir.right - btnW - 6, ir.top + (ir.bottom - ir.top - 22) / 2,
                          ir.right - 6, ir.top + (ir.bottom - ir.top - 22) / 2 + 22 };
                RECT itemR{ ir.left, ir.top, ir.right - btnW - 8, ir.bottom };
                if (PtInRect(&brc, pt) || PtInRect(&itemR, pt)) {
                    SelectEffect(isTrail, i);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
            }

            // --- 4b: 配色块 ---
            int cw_half = (EffRightW() - 8) / 2;
            RECT ca{ EffRightX(), EffColorY(), EffRightX() + cw_half, EffColorY() + EffColorH() };
            RECT cb{ EffRightX() + cw_half + 8, EffColorY(), EffRightX() + EffRightW(), EffColorY() + EffColorH() };
            const EffectDef& d = g_previewIsTrail ? g_app.curTrail : g_app.curClick;
            if (PtInRect(&ca, pt)) { OpenColorPicker(h, d, g_previewIsTrail, true); return 0; }
            if (PtInRect(&cb, pt)) { OpenColorPicker(h, d, g_previewIsTrail, false); return 0; }

            // --- 4c: 重置按钮 ---
            RECT rr{ EffRightX(), EffResetY(), EffRightX() + EffRightW(), EffResetY() + EffResetH() };
            if (PtInRect(&rr, pt)) {
                g_app.settings.ResetEffect(d, g_previewIsTrail);
                g_app.ApplyConfig(); SyncPreviewConfig(); RebuildParamPanel(); g_app.Save();
                InvalidateRect(h, nullptr, FALSE); return 0;
            }

            // --- 4d: 参数面板滑块 ---
            for (auto& r : g_rows) {
                RECT rc = ParamSliderRect(r);
                if (PtInRect(&rc, pt)) {
                    int pos = SliderPosFromX(rc, x);
                    float v = r.lo + (pos / 1000.0f) * (r.hi - r.lo);
                    const EffectDef& ed = r.isTrail ? g_app.curTrail : g_app.curClick;
                    g_app.settings.SetParam(ed, r.isTrail, r.pid, v);
                    g_app.ApplyConfig(); SyncPreviewConfig();
                    g_dragRow = &r; g_dragPos = pos; SetCapture(h);
                    InvalidateRect(h, &rc, FALSE); return 0;
                }
            }
        }

        // ★ 优先级5：通用设置页
        if (g_curPage == 3) {
            int x0 = ContentX0(), cw = ContentW();
            int cardY = kHeaderH + 20 + 32;
            int sx = x0 + 28, slx = x0 + 120, slr = x0 + cw - 20;
            int rowH = 42;
            int ry = cardY + 46;

            RECT opRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
            if (PtInRect(&opRc, pt)) {
                int p = SliderPosFromX(opRc, x);
                g_app.settings.globalOpacity = p / 1000.0f;
                g_app.ApplyConfig(); SyncPreviewConfig(); g_setDrag = 1; g_dragPos = p; SetCapture(h);
                InvalidateRect(h, &opRc, FALSE); InvalidateRect(h, nullptr, FALSE); return 0;
            }
            ry += rowH;
            RECT scRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
            if (PtInRect(&scRc, pt)) {
                int p = SliderPosFromX(scRc, x);
                g_app.settings.globalScale = 0.3f + (p / 1000.0f) * (3.0f - 0.3f);
                g_app.ApplyConfig(); SyncPreviewConfig(); g_setDrag = 2; g_dragPos = p; SetCapture(h);
                InvalidateRect(h, &scRc, FALSE); InvalidateRect(h, nullptr, FALSE); return 0;
            }
            ry += rowH;
            int radioW = (slr - slx) / 3;
            int rby = ry + (rowH - 18) / 2;
            ThemeMode modes[] = { Theme_System, Theme_Light, Theme_Dark };
            for (int i = 0; i < 3; ++i) {
                RECT rr{ slx + i * radioW - 4, rby, slx + i * radioW + radioW + 4, rby + 18 };
                if (PtInRect(&rr, pt)) {
                    g_app.settings.theme = modes[i]; g_app.Save();
                    SyncPreviewConfig(); InvalidateRect(h, nullptr, TRUE); return 0;
                }
            }

            int c2y = cardY + 184;  // 匹配新卡片间距
            int cy = c2y + 36;
            struct ChkDef { const wchar_t* txt; bool* val; };
            ChkDef chks[] = {
                { L"开机自启动",           &g_app.settings.autoStart },
                { L"自启动时静默（不弹界面）", &g_app.settings.silentStart },
                { L"关闭窗口时最小化到托盘", &g_app.settings.minimizeToTray },
                { L"全屏应用时自动暂停",   &g_app.settings.pauseFullscreen },
                { L"鼠标静止时淡出拖尾",   &g_app.settings.hideOnIdle },
            };
            for (int ci = 0; ci < 5; ++ci) {
                RECT cr{ sx - 4, cy + (rowH - 15) / 2 - 2, sx + 300, cy + (rowH - 15) / 2 + 17 };
                if (PtInRect(&cr, pt)) {
                    *chks[ci].val = !(*chks[ci].val);
                    g_app.ApplyConfig(); g_app.Save(); g_app.UpdateTrayTip();
                    InvalidateRect(h, nullptr, FALSE); return 0;
                }
                cy += rowH;
            }
            RECT fr{ slx, cy + (rowH - 24) / 2, slx + 170, cy + (rowH - 24) / 2 + 24 };
            if (PtInRect(&fr, pt)) {
                g_fpsDropdownOpen = !g_fpsDropdownOpen;  // 切换下拉框
                InvalidateRect(h, nullptr, FALSE); return 0;
            }
            // ★ 点击下拉框选项
            if (g_fpsDropdownOpen) {
                const wchar_t* fpsLabels[] = { L"跟随刷新率", L"30 FPS", L"60 FPS", L"120 FPS", L"144 FPS" };
                int fpsVals[] = { 0, 30, 60, 120, 144 };
                int itemH = 28;
                RECT dropRc{ g_fpsDropdownRc.left, g_fpsDropdownRc.bottom + 2,
                             g_fpsDropdownRc.right, g_fpsDropdownRc.bottom + 2 + 5 * itemH + 4 };
                if (PtInRect(&dropRc, pt)) {
                    int idx = (pt.y - dropRc.top - 2) / itemH;
                    if (idx >= 0 && idx < 5) {
                        g_app.settings.fpsLimit = fpsVals[idx]; g_app.Save();
                    }
                    g_fpsDropdownOpen = false;
                    InvalidateRect(h, nullptr, FALSE); return 0;
                }
                // 点击下拉框外部 → 关闭
                g_fpsDropdownOpen = false;
                // 不 return，让后续逻辑处理（可能点击了其他地方）
            }
        }
        return 0;
    }

    // ---------- 鼠标移动（拖拽） ----------
    if (m == WM_MOUSEMOVE && (g_dragRow || g_setDrag)) {
        int mx = GET_X_LPARAM(l);
        if (g_dragRow) {
            RECT rc = ParamSliderRect(*g_dragRow);
            int pos = SliderPosFromX(rc, mx);
            float v = g_dragRow->lo + (pos / 1000.0f) * (g_dragRow->hi - g_dragRow->lo);
            const EffectDef& d = g_dragRow->isTrail ? g_app.curTrail : g_app.curClick;
            g_app.settings.SetParam(d, g_dragRow->isTrail, g_dragRow->pid, v);
            g_app.ApplyConfig(); SyncPreviewConfig();
            InvalidateRect(h, nullptr, FALSE);  // ★ 全刷新，让数值标签也更新
        } else if (g_setDrag == 1) {
            int x0 = ContentX0(), cw = ContentW();
            int slx = x0 + 120, slr = x0 + cw - 20;
            int rowH = 42, ry = kHeaderH + 20 + 32 + 46;
            RECT opRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
            int pos = SliderPosFromX(opRc, mx);
            g_app.settings.globalOpacity = pos / 1000.0f;
            g_app.ApplyConfig(); SyncPreviewConfig();
            InvalidateRect(h, &opRc, FALSE); InvalidateRect(h, nullptr, FALSE);
        } else if (g_setDrag == 2) {
            int x0 = ContentX0(), cw = ContentW();
            int slx = x0 + 120, slr = x0 + cw - 20;
            int rowH = 42, ry = kHeaderH + 20 + 32 + 46 + rowH;
            RECT scRc{ slx, ry + (rowH - 22) / 2, slr - 50, ry + (rowH - 22) / 2 + 22 };
            int pos = SliderPosFromX(scRc, mx);
            g_app.settings.globalScale = 0.3f + (pos / 1000.0f) * (3.0f - 0.3f);
            g_app.ApplyConfig(); SyncPreviewConfig();
            InvalidateRect(h, &scRc, FALSE); InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    if (m == WM_LBUTTONUP) {
        if (g_dragRow || g_setDrag) {
            g_dragRow = nullptr; g_setDrag = 0; g_app.Save(); ReleaseCapture();
        }
        return 0;
    }

    // ========== ★ 滚轮（独立滚动，X坐标分栏）==========
    // ⚠ WM_MOUSEWHEEL 的 lParam 是屏幕坐标！必须 ScreenToClient 转换
    if (m == WM_MOUSEWHEEL) {
        short delta = GET_WHEEL_DELTA_WPARAM(w);
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);  // 屏幕坐标 → 客户区坐标

        if (g_curPage == 1 || g_curPage == 2) {
            int midX = ContentX0() + EffListW() + 10;
            bool onLeft = (pt.x < midX);
            bool onRight = (pt.x >= midX);

            if (onLeft) {
                int n = (g_curPage == 1) ? kTrailEffectCount : kClickEffectCount;
                int itemH = 70;
                int totalContentH = n * itemH;
                int maxScroll = (std::max)(0, totalContentH - EffListH());
                if (maxScroll > 0) {
                    g_listScroll = Clampi(g_listScroll + (delta > 0 ? -60 : 60), 0, maxScroll);
                }
            } else if (onRight) {
                int contentH = (int)g_rows.size() * kParamRowH;
                int maxPS = (std::max)(0, contentH - (EffParamH() - 32));
                if (maxPS > 0) {
                    g_paramScroll = Clampi(g_paramScroll + (delta > 0 ? -40 : 40), 0, maxPS);
                }
            }
            InvalidateRect(h, nullptr, FALSE); return 0;
        }
        return 0;
    }

    // ---------- 计时器 ----------
    if (m == WM_TIMER && w == kPreviewTimer) { PreviewTick(); return 0; }

    // ========== WM_PAINT ==========
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        int W = cr.right - cr.left, H = cr.bottom - cr.top;

        EnsureBackBuffer(W, H);
        if (!g_backDC) { EndPaint(h, &ps); return 0; }

        HDC hdc = g_backDC;

        T t = Theme();
        {
            Gdiplus::Graphics tmpG(hdc);
            tmpG.Clear(t.gpBg);
        }

        HFONT oldF = (HFONT)SelectObject(hdc, g_uiFont);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        DrawSidebar(g, hdc, t);
        DrawHeader(g, hdc, t);
        switch (g_curPage) {
            case 0: DrawOverviewPage(g, hdc, t); break;
            case 1: DrawEffectPage(g, hdc, t, true); break;
            case 2: DrawEffectPage(g, hdc, t, false); break;
            case 3: DrawSettingsPage(g, hdc, t); break;
            case 4: DrawAboutPage(g, hdc, t); break;
        }

        // ★ FPS 下拉框弹出层
        if (g_fpsDropdownOpen) {
            const wchar_t* fpsLabels[] = { L"跟随刷新率", L"30 FPS", L"60 FPS", L"120 FPS", L"144 FPS" };
            int fpsVals[] = { 0, 30, 60, 120, 144 };
            int sel = 0; for (int i = 0; i < 5; ++i) if (g_app.settings.fpsLimit == fpsVals[i]) sel = i;
            int itemH = 28;
            RECT dropRc{ g_fpsDropdownRc.left, g_fpsDropdownRc.bottom + 2,
                         g_fpsDropdownRc.right, g_fpsDropdownRc.bottom + 2 + 5 * itemH + 4 };
            // 弹出背景
            DrawRoundRect(g, dropRc, t, 6, t.gpCard);
            // 阴影边框
            GP_StrokeRoundRect(g, dropRc.left, dropRc.top,
                               dropRc.right - dropRc.left, dropRc.bottom - dropRc.top,
                               6, Gdiplus::Color(40, 40, 48), 1.0f);
            for (int i = 0; i < 5; ++i) {
                RECT ir{ dropRc.left + 4, dropRc.top + 2 + i * itemH,
                         dropRc.right - 4, dropRc.top + 2 + (i + 1) * itemH };
                if (i == sel)
                    FillRect(hdc, &ir, (HBRUSH)GetStockObject(BLACK_BRUSH));  // 选中项深色
                int txtCol = (i == sel) ? RGB(255,255,255) : t.text;
                DrawTextAA(hdc, fpsLabels[i], ir, txtCol,
                           DT_SINGLELINE | DT_VCENTER | DT_LEFT, g_uiFontSmall);
            }
        }

        SelectObject(hdc, oldF);

        BitBlt(hdcScreen, 0, 0, W, H, g_backDC, 0, 0, SRCCOPY);

        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ==================================================== 辅助函数定义
static void ShowPageInit() {
    g_previewIsTrail = (g_curPage == 1);
    SyncPreviewConfig();
    RebuildParamPanel();
}

static void OpenColorPicker(HWND owner, const EffectDef& d, bool isTrail, bool isA) {
    Color4f cur = isA ? g_app.settings.GetColorA(d, isTrail) : g_app.settings.GetColorB(d, isTrail);
    CHOOSECOLORW cc{}; cc.lStructSize = sizeof(cc); cc.hwndOwner = owner;
    cc.lpCustColors = g_customColors;
    COLORREF cr = RGB((int)(cur.r * 255), (int)(cur.g * 255), (int)(cur.b * 255));
    cc.rgbResult = cr;
    if (ChooseColorW(&cc)) {
        Color4f nc((int)GetRValue(cc.rgbResult) / 255.0f, (int)GetGValue(cc.rgbResult) / 255.0f,
                   (int)GetBValue(cc.rgbResult) / 255.0f, 1);
        if (isA) g_app.settings.SetColorA(d, isTrail, nc); else g_app.settings.SetColorB(d, isTrail, nc);
        g_app.ApplyConfig(); SyncPreviewConfig(); g_app.Save();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

// ==================================================== 对外接口
static bool g_registered = false;
static void RegisterClasses(HINSTANCE hInst) {
    if (g_registered) return;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MF_Settings";
    wc.hIcon = g_app.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    g_registered = true;
}

void App::OpenSettings() {
    if (hwndUI) { ShowWindow(hwndUI, SW_SHOW); SetForegroundWindow(hwndUI); return; }
    HINSTANCE hi = GetModuleHandleW(nullptr);
    RegisterClasses(hi);
    int cx = (GetSystemMetrics(SM_CXSCREEN) - kWinW) / 2;
    int cy = (GetSystemMetrics(SM_CYSCREEN) - kWinH) / 2;
    HWND hw = CreateWindowExW(WS_EX_APPWINDOW, L"MF_Settings", L"MouseFlow 设置",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
        cx, cy, kWinW, kWinH, nullptr, nullptr, hi, nullptr);
    if (!hw) return;
    hwndUI = hw; uiOpen = true;
    ShowWindow(hw, SW_SHOW);
    SetForegroundWindow(hw);
}

void App::HideSettingsToTray() {
    if (hwndUI) { DestroyWindow(hwndUI); hwndUI = nullptr; uiOpen = false; }
}

void UISync(HWND hwnd) {
    if (!hwnd) return;
    FullSync();
}

} // namespace mf
