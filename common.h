// MouseFlow - 轻量级鼠标特效
// common.h : 公共类型、数学与工具
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <unknwn.h>
#include <objbase.h>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------- 常量
namespace mf {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kDegToRad = kPi / 180.0f;

// 构建戳记：每次发布递增，用于确认实际运行的二进制版本
inline constexpr const wchar_t* kBuildStamp = L"2026.08.04-r1";

// ---------------------------------------------------------------- 数学
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   Clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerp(float a, float b, float t)     { return a + (b - a) * t; }
inline float SmoothStep(float t)                 { return t * t * (3.0f - 2.0f * t); }
inline float EaseOutCubic(float t)               { float u = 1.0f - t; return 1.0f - u * u * u; }
inline float EaseOutQuad(float t)                { float u = 1.0f - t; return 1.0f - u * u; }
inline float EaseInQuad(float t)                 { return t * t; }
inline float EaseOutExpo(float t)                { return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t); }
inline float EaseOutBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float a, float b) : x(a), y(b) {}
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const       { return { x * s, y * s }; }
    Vec2& operator+=(const Vec2& o)     { x += o.x; y += o.y; return *this; }
    float Length() const                { return sqrtf(x * x + y * y); }
    float LengthSq() const              { return x * x + y * y; }
    Vec2 Normalized() const {
        float l = Length();
        return l > 1e-6f ? Vec2{ x / l, y / l } : Vec2{ 0.0f, 0.0f };
    }
    Vec2 Perp() const { return { -y, x }; }
};

inline Vec2 LerpV(const Vec2& a, const Vec2& b, float t) {
    return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
}

// Catmull-Rom 样条插值，用于轨迹平滑
inline Vec2 CatmullRom(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3)
    };
}

// ---------------------------------------------------------------- 颜色
struct Color4f {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    Color4f() = default;
    constexpr Color4f(float rr, float gg, float bb, float aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}
};

inline Color4f LerpC(const Color4f& x, const Color4f& y, float t) {
    return { Lerp(x.r, y.r, t), Lerp(x.g, y.g, t), Lerp(x.b, y.b, t), Lerp(x.a, y.a, t) };
}

inline Color4f WithAlpha(const Color4f& c, float a) { return { c.r, c.g, c.b, a }; }

// 0xRRGGBB -> Color4f
inline constexpr Color4f Rgb(uint32_t hex, float a = 1.0f) {
    return Color4f(((hex >> 16) & 0xFF) / 255.0f,
                   ((hex >> 8) & 0xFF) / 255.0f,
                   (hex & 0xFF) / 255.0f, a);
}

inline uint32_t ToHex(const Color4f& c) {
    auto q = [](float v) -> uint32_t { return (uint32_t)Clampf(v * 255.0f + 0.5f, 0.0f, 255.0f); };
    return (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

// HSV -> RGB, h 单位为循环圈数(0..1)
inline Color4f HsvToRgb(float h, float s, float v, float a = 1.0f) {
    h = h - floorf(h);
    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (((int)i) % 6) {
        case 0:  return { v, t, p, a };
        case 1:  return { q, v, p, a };
        case 2:  return { p, v, t, a };
        case 3:  return { p, q, v, a };
        case 4:  return { t, p, v, a };
        default: return { v, p, q, a };
    }
}

inline void RgbToHsv(const Color4f& c, float& h, float& s, float& v) {
    float mx = (std::max)({ c.r, c.g, c.b });
    float mn = (std::min)({ c.r, c.g, c.b });
    float d = mx - mn;
    v = mx;
    s = mx <= 1e-6f ? 0.0f : d / mx;
    if (d <= 1e-6f) { h = 0.0f; return; }
    if (mx == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else                h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;
}

// ---------------------------------------------------------------- 随机数 (xorshift32，快且无依赖)
class Rng {
public:
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s_(seed ? seed : 0x9E3779B9u) {}
    uint32_t Next() {
        s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5;
        return s_;
    }
    // [0,1)
    float NextFloat() { return (Next() >> 8) * (1.0f / 16777216.0f); }
    // [lo,hi)
    float Range(float lo, float hi) { return lo + NextFloat() * (hi - lo); }
    // [-1,1)
    float Signed() { return NextFloat() * 2.0f - 1.0f; }
    int   RangeI(int lo, int hi) { return hi <= lo ? lo : lo + (int)(Next() % (uint32_t)(hi - lo)); }
    bool  Chance(float p) { return NextFloat() < p; }
private:
    uint32_t s_;
};

extern Rng g_rng;

// ---------------------------------------------------------------- COM 智能指针
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) reinterpret_cast<IUnknown*>(p_)->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) { Reset(); p_ = o.p_; if (p_) reinterpret_cast<IUnknown*>(p_)->AddRef(); }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { Reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }

    // 通过 IUnknown* 调用，使 ComPtr 可在仅前向声明接口时使用
    void Reset() { if (p_) { reinterpret_cast<IUnknown*>(p_)->Release(); p_ = nullptr; } }
    T*   Get() const { return p_; }
    T**  GetAddressOf() { Reset(); return &p_; }
    T*   operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator!() const { return p_ == nullptr; }

    template <typename U>
    HRESULT As(ComPtr<U>& out) const {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.GetAddressOf()));
    }
    void Attach(T* p) { Reset(); p_ = p; }
    T* Detach() { T* t = p_; p_ = nullptr; return t; }

private:
    T* p_ = nullptr;
};

// ---------------------------------------------------------------- 时间
inline double NowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

// ---------------------------------------------------------------- 字符串
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

} // namespace mf
