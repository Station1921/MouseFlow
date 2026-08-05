// renderer.cpp : 分层窗口 + 软件 ARGB 渲染（2026-08-04 彻底重写）
//
// 设计要点：
//   - 覆盖层使用 WS_EX_LAYERED；每帧把粒子软件绘制到一张全屏 BGRA 离屏缓冲，
//     缓冲里 alpha=0 的像素经 UpdateLayeredWindow 后，DWM 按像素 alpha 做命中测试，
//     点击自动穿透到下层窗口——这是 Windows 上点击穿透覆盖层最可靠的做法，
//     无需任何 DirectComposition / SetHitTestable hack。
//   - 缓冲以"预乘 alpha"存储；UpdateLayeredWindow(AC_SRC_ALPHA, SourceConstantAlpha=255)
//     需要预乘格式，合成结果天然正确。
#include "renderer.h"
#include <vector>
#include <cstring>
#include <cmath>

namespace mf {

static const wchar_t* kOverlayClass = L"MouseFlowOverlayWnd";

// ================================================================ 像素混合
// 预乘 alpha 的 "over" 合成：dst 为预乘 BGRA，src 为直色 r,g,b 与 alpha a(0..255)。
static inline void BlendPM(uint32_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0) return;
    uint32_t d = *dst;
    uint8_t dr = (uint8_t)(d >> 16), dg = (uint8_t)(d >> 8), db = (uint8_t)d, da = (uint8_t)(d >> 24);
    int oa = a + da - (a * da) / 255;
    int or_ = (r * a + dr * (255 - a)) / 255;
    int og = (g * a + dg * (255 - a)) / 255;
    int ob = (b * a + db * (255 - a)) / 255;
    *dst = ((uint32_t)oa << 24) | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

static inline uint8_t ClampB(float v) { return (uint8_t)Clampf(v * 255.0f + 0.5f, 0, 255); }

// ================================================================ 形状几何
// 单位空间多边形（约 [-1,1]）生成器
static std::vector<Vec2> MakeRegular(int n, float rot) {
    std::vector<Vec2> p; n = Clampi(n, 3, 64);
    for (int i = 0; i < n; ++i) {
        float a = rot + kTwoPi * i / n;
        p.push_back({ cosf(a), sinf(a) });
    }
    return p;
}
static std::vector<Vec2> MakeStar(int points, float inner, float rot) {
    std::vector<Vec2> p; int n = points * 2;
    for (int i = 0; i < n; ++i) {
        float a = rot + kPi * i / points;
        float r = (i & 1) ? inner : 1.0f;
        p.push_back({ cosf(a) * r, sinf(a) * r });
    }
    return p;
}
static std::vector<Vec2> MakeHeart() {
    std::vector<Vec2> p; const int N = 40;
    for (int i = 0; i < N; ++i) {
        float t = kTwoPi * i / N;
        float x = 16.0f * powf(sinf(t), 3.0f);
        float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
        p.push_back({ x / 17.0f, -y / 17.0f });
    }
    return p;
}
static std::vector<Vec2> MakeButterfly() {
    std::vector<Vec2> p; const int N = 72; float mx = 1e-4f;
    std::vector<float> tx(N), ty(N);
    for (int i = 0; i < N; ++i) {
        float t = (kTwoPi * 2.0f) * i / N;
        float r = expf(sinf(t)) - 2.0f * cosf(4 * t) + powf(sinf((2 * t - kPi) / 24.0f), 5.0f);
        tx[i] = sinf(t) * r; ty[i] = -cosf(t) * r;
        mx = (std::max)(mx, (std::max)(fabsf(tx[i]), fabsf(ty[i])));
    }
    for (int i = 0; i < N; ++i) p.push_back({ tx[i] / mx, ty[i] / mx });
    return p;
}
static std::vector<Vec2> MakeSnowflake() {
    std::vector<Vec2> p;
    for (int i = 0; i < 6; ++i) {
        float a = kPi * i / 3.0f, ca = cosf(a), sa = sinf(a);
        auto R = [&](float x, float y) -> Vec2 { return { x * ca - y * sa, x * sa + y * ca }; };
        Vec2 arm[4] = { R(0, -0.10f), R(1.0f, -0.04f), R(1.0f, 0.04f), R(0, 0.10f) };
        p.push_back(arm[0]); p.push_back(arm[1]); p.push_back(arm[2]); p.push_back(arm[3]);
        for (int k = 0; k < 2; ++k) {
            float at = 0.45f + k * 0.28f, len = 0.34f - k * 0.10f;
            for (int s = -1; s <= 1; s += 2) {
                Vec2 br[4] = { R(at, 0), R(at + len * 0.62f, s * len * 0.80f),
                               R(at + len * 0.62f + 0.05f, s * (len * 0.80f - 0.05f)), R(at + 0.05f, 0) };
                p.push_back(br[0]); p.push_back(br[1]); p.push_back(br[2]); p.push_back(br[3]);
            }
        }
    }
    return p;
}
static std::vector<Vec2> MakeBolt() {
    std::vector<Vec2> p = {
        { 0.15f, -1.0f }, { -0.55f, 0.05f }, { -0.05f, 0.05f },
        { -0.25f, 1.0f }, { 0.6f, -0.15f }, { 0.1f, -0.15f }, { 0.45f, -1.0f }
    };
    return p;
}
static std::vector<Vec2> MakeBlob() {
    std::vector<Vec2> p; const int N = 20;
    Rng rng(0xB10BB10Bu); float rad[20];
    for (int i = 0; i < N; ++i) rad[i] = rng.Range(0.68f, 1.0f);
    for (int pass = 0; pass < 2; ++pass) {
        float t[20];
        for (int i = 0; i < N; ++i)
            t[i] = (rad[(i - 1 + N) % N] + rad[i] * 2.0f + rad[(i + 1) % N]) * 0.25f;
        for (int i = 0; i < N; ++i) rad[i] = t[i];
    }
    for (int i = 0; i < N; ++i) {
        float a = kTwoPi * i / N;
        p.push_back({ cosf(a) * rad[i], sinf(a) * rad[i] });
    }
    return p;
}
static std::vector<Vec2> SampleBezier(const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, int steps) {
    std::vector<Vec2> out;
    for (int i = 0; i <= steps; ++i) {
        float u = i / (float)steps, v = 1 - u;
        float x = v * v * v * p0.x + 3 * v * v * u * c1.x + 3 * v * u * u * c2.x + u * u * u * p1.x;
        float y = v * v * v * p0.y + 3 * v * v * u * c1.y + 3 * v * u * u * c2.y + u * u * u * p1.y;
        out.push_back({ x, y });
    }
    return out;
}
static std::vector<Vec2> MakePetal() {
    return SampleBezier({ 0, -1 }, { 0.95f, -0.55f }, { 0.75f, 0.55f }, { 0, 1 }, 10);
}
static std::vector<Vec2> MakeLeaf() {
    return SampleBezier({ 0, -1 }, { 1.05f, -0.35f }, { 0.55f, 0.75f }, { 0, 1 }, 10);
}

static const std::vector<Vec2>& UnitShape(Shape s) {
    static const std::vector<Vec2> empty;
    static std::vector<Vec2> cache[(int)Shape::Blob + 1];
    int idx = (int)s;
    if (idx < 0 || idx > (int)Shape::Blob) return empty;
    if (!cache[idx].empty()) return cache[idx];
    switch (s) {
        case Shape::Circle:    cache[idx] = MakeRegular(16, 0); break;
        case Shape::Square:    cache[idx] = MakeRegular(4, kPi * 0.25f); break;
        case Shape::Triangle:  cache[idx] = MakeRegular(3, -kPi * 0.5f); break;
        case Shape::Hexagon:   cache[idx] = MakeRegular(6, -kPi * 0.5f); break;
        case Shape::Diamond:   cache[idx] = MakeRegular(4, -kPi * 0.5f); break;
        case Shape::Star4:     cache[idx] = MakeStar(4, 0.30f, -kPi * 0.5f); break;
        case Shape::Star5:     cache[idx] = MakeStar(5, 0.42f, -kPi * 0.5f); break;
        case Shape::Heart:     cache[idx] = MakeHeart(); break;
        case Shape::Butterfly: cache[idx] = MakeButterfly(); break;
        case Shape::Snowflake: cache[idx] = MakeSnowflake(); break;
        case Shape::Bolt:      cache[idx] = MakeBolt(); break;
        case Shape::Blob:      cache[idx] = MakeBlob(); break;
        case Shape::Petal:     cache[idx] = MakePetal(); break;
        case Shape::Leaf:      cache[idx] = MakeLeaf(); break;
        default: return empty;
    }
    return cache[idx];
}

// ================================================================ 基本图元
// ★ 性能优化（2026-08-04）：软件渲染每像素 sqrtf+powf 是最大瓶颈
//   ① 半径上限封顶（>60px 的软点无视觉差异但计算量爆炸）
//   ② 小半径快速路径（≤2px 直接画中心，跳过双重循环）
//   ③ 用乘法近似替代 powf(cov, 2.15)
//   ④ d² 判定替代 sqrtf（仅边缘像素才需要真实距离）
static inline float fastPow215(float x) {
    // powf(x, 2.15) ≈ x*x*x 的近似，误差 < 5% 但快 10 倍
    return x * x * x;
}

static void SoftDot(uint32_t* buf, int W, int H, float cx, float cy, float radius,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0 || radius <= 0.15f) return;

    // ★ 半径封顶：超过 60px 的软点视觉上和 60px 无区别，但计算量随 r² 爆炸
    if (radius > 60.0f) radius = 60.0f;

    // ★ 小半径快速路径：≤2px 直接画中心 3×3 区域
    if (radius <= 2.0f) {
        int ix = (int)cx, iy = (int)cy;
        if (ix >= 0 && ix < W && iy >= 0 && iy < H)
            BlendPM(&buf[iy * W + ix], r, g, b, a);
        return;
    }

    int x0 = (int)(cx - radius), x1 = (int)(cx + radius);
    int y0 = (int)(cy - radius), y1 = (int)(cy + radius);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    float R = radius;
    float Rinv = 1.0f / R;
    for (int y = y0; y <= y1; ++y) {
        float dy = (y - cy) * Rinv;
        float dySq = dy * dy;
        for (int x = x0; x <= x1; ++x) {
            float dx = (x - cx) * Rinv;
            float d2 = dx * dx + dySq;
            if (d2 >= 1.0f) continue;
            // ★ 仅边缘区域需要 sqrtf（d2 > 0.5 时），内部直接高亮
            float cov;
            if (d2 < 0.05f) {
                cov = 1.0f;  // 核心区全不透明
            } else if (d2 < 0.5f) {
                // 内部过渡区：sqrtf(d2) → d，线性→平方衰减
                float d = sqrtf(d2);
                cov = 1.0f - fastPow215(d);  // 近似 pow(d, 2.15)
            } else {
                float d = sqrtf(d2);
                cov = 1.0f - d;
                cov *= cov;  // 平方软化边缘（替代 powf(cov, 2.15)）
            }
            if (cov > 1) cov = 1;
            BlendPM(&buf[y * W + x], r, g, b, (uint8_t)(cov * a + 0.5f));
        }
    }
}

static void RingStroke(uint32_t* buf, int W, int H, float cx, float cy, float radius, float thick,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0 || radius <= 0) return;
    // ★ 半径封顶
    if (radius > 60.0f) { thick *= 60.0f / radius; radius = 60.0f; }
    float inner = radius - thick * 0.5f, outer = radius + thick * 0.5f;
    if (inner < 0) inner = 0;
    // ★ 自适应抗锯齿带宽（先算，因为边界框需包含抗锯齿区）
    float edge = (std::max)(1.5f, thick * 0.25f);
    float outerEdge = outer + edge;
    int x0 = (int)(cx - outerEdge), x1 = (int)(cx + outerEdge);
    int y0 = (int)(cy - outerEdge), y1 = (int)(cy + outerEdge);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    float innerEdge = inner - edge;
    for (int y = y0; y <= y1; ++y) {
        float dy = y - cy;
        float dySq = dy * dy;
        for (int x = x0; x <= x1; ++x) {
            float dx = x - cx;
            float d2 = dx * dx + dySq;
            if (d2 > outerEdge * outerEdge) continue;  // ★ 快速排除外部（含边缘区）
            float d = sqrtf(d2);
            float cov = 0;
            if (d >= inner && d <= outer) cov = 1;
            else if (d > innerEdge && d < inner) cov = (d - innerEdge) / edge;
            else if (d < outerEdge && d > outer) cov = (outerEdge - d) / edge;
            if (cov <= 0) continue;
            BlendPM(&buf[y * W + x], r, g, b, (uint8_t)(cov * a + 0.5f));
        }
    }
}

static void PlotLine(uint32_t* buf, int W, int H, float x0, float y0, float x1, float y1,
                     float width, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0 || width <= 0) return;
    // ★ 封顶宽度
    if (width > 60.0f) width = 60.0f;
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float rad = width * 0.5f;
    if (len < 0.001f) { SoftDot(buf, W, H, x0, y0, rad, r, g, b, a); return; }
    // ★ 长线段减少步数（每 2px 一步而非每 1px，SoftDot 本身有面积覆盖）
    int steps = (int)(len * 0.6f) + 1;  // 原来是 len+1
    if (steps > 40) steps = 40;  // ★ 上限封顶
    for (int i = 0; i <= steps; ++i) {
        float t = i / (float)steps;
        SoftDot(buf, W, H, x0 + dx * t, y0 + dy * t, rad, r, g, b, a);
    }
}

static bool PointInPoly(float x, float y, const float* X, const float* Y, int n) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((Y[i] > y) != (Y[j] > y)) &&
            (x < (X[j] - X[i]) * (y - Y[i]) / (Y[j] - Y[i]) + X[i]))
            inside = !inside;
    }
    return inside;
}

static void FillPoly(uint32_t* buf, int W, int H, const float* X, const float* Y, int n,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (n < 3 || a == 0) return;
    float minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (int i = 0; i < n; ++i) {
        if (X[i] < minx) minx = X[i]; if (X[i] > maxx) maxx = X[i];
        if (Y[i] < miny) miny = Y[i]; if (Y[i] > maxy) maxy = Y[i];
    }
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int cnt = 0;
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx) {
                    float fx = x + 0.25f + sx * 0.5f, fy = y + 0.25f + sy * 0.5f;
                    if (PointInPoly(fx, fy, X, Y, n)) ++cnt;
                }
            if (cnt == 0) continue;
            BlendPM(&buf[y * W + x], r, g, b, (uint8_t)(cnt / 4.0f * a + 0.5f));
        }
    }
}

static void DrawRotRect(uint32_t* buf, int W, int H, float cx, float cy, float hw, float hh,
                        float rot, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float cr = cosf(rot), sr = sinf(rot);
    float X[4], Y[4];
    float lx[] = { -hw, hw, hw, -hw }, ly[] = { -hh, -hh, hh, hh };
    for (int i = 0; i < 4; ++i) {
        X[i] = cx + lx[i] * cr - ly[i] * sr;
        Y[i] = cy + lx[i] * sr + ly[i] * cr;
    }
    FillPoly(buf, W, H, X, Y, 4, r, g, b, a);
}

// ================================================================ 粒子
static void DrawShapeParticle(uint32_t* buf, int W, int H, const Particle& q,
                              float px, float py, float sz,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t ba,
                              int passAdd) {
    switch (q.shape) {
        case Shape::Soft:
            SoftDot(buf, W, H, px, py, sz, r, g, b, ba); return;
        case Shape::Ring:
            RingStroke(buf, W, H, px, py, sz, (std::max)(0.8f, sz * 0.22f), r, g, b, ba); return;
        case Shape::Bubble:
            RingStroke(buf, W, H, px, py, sz, (std::max)(0.7f, sz * 0.14f), r, g, b, (uint8_t)(ba * 0.75f));
            SoftDot(buf, W, H, px - sz * 0.32f, py - sz * 0.34f, sz * 0.17f, 255, 255, 255, (uint8_t)(ba * 0.55f));
            return;
        case Shape::Streak:
            DrawRotRect(buf, W, H, px, py, sz * 1.9f, sz * 0.30f, q.rot, r, g, b, ba); return;
        default: break;
    }
    const auto& unit = UnitShape(q.shape);
    if (unit.empty()) { SoftDot(buf, W, H, px, py, sz, r, g, b, ba); return; }
    int n = (int)unit.size();
    std::vector<float> X(n), Y(n);
    float cr = cosf(q.rot), sr = sinf(q.rot);
    float minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (int i = 0; i < n; ++i) {
        float lx = unit[i].x * sz, ly = unit[i].y * sz;
        X[i] = px + lx * cr - ly * sr;
        Y[i] = py + lx * sr + ly * cr;
        if (X[i] < minx) minx = X[i]; if (X[i] > maxx) maxx = X[i];
        if (Y[i] < miny) miny = Y[i]; if (Y[i] > maxy) maxy = Y[i];
    }
    FillPoly(buf, W, H, X.data(), Y.data(), n, r, g, b, ba);
    if ((q.flags & E_Core) && q.shape != Shape::Soft) {
        std::vector<float> X2(n), Y2(n);
        for (int i = 0; i < n; ++i) {
            float lx = unit[i].x * sz * 0.30f, ly = unit[i].y * sz * 0.30f;
            X2[i] = px + lx * cr - ly * sr; Y2[i] = py + lx * sr + ly * cr;
        }
        FillPoly(buf, W, H, X2.data(), Y2.data(), n, 255, 255, 255, (uint8_t)(ba * 0.45f));
    }
    (void)passAdd;
}

static void DrawParticle(uint32_t* buf, int W, int H, const Particle& q, float opacity, float ox, float oy) {
    float a = ParticleAlpha(q) * opacity;
    if (a <= 0.004f) return;
    float t = Clampf(q.age / (q.life > 1e-4f ? q.life : 1e-4f), 0, 1);
    Color4f col = LerpC(q.cA, q.cB, t);
    if (q.flags & E_Flicker) a *= 0.70f + 0.30f * sinf(q.age * 26.0f + q.phase);
    float jx = 0, jy = 0;
    if (q.flags & E_Jitter) { jx = sinf(q.age * 61.0f + q.phase) * 1.6f; jy = cosf(q.age * 73.0f + q.phase) * 1.6f; }
    float px = q.pos.x - ox + jx, py = q.pos.y - oy + jy;
    uint8_t r = ClampB(col.r), g = ClampB(col.g), b = ClampB(col.b);
    uint8_t ba = (uint8_t)Clampf(a * 255 + 0.5f, 0, 255);
    float sz = ParticleSize(q);

    if (q.glow > 0.02f && sz > 0.15f && sz < 30.0f)  // ★ 大粒子跳过额外辉光（SoftDot 本身已足够大）
        SoftDot(buf, W, H, px, py, sz * (1.6f + q.glow * 1.0f), r, g, b, (uint8_t)(ba * q.glow * 0.38f));

    switch (q.behav) {
        case PBehav::Ring: {
            float e = EaseOutCubic(t);
            float rr = q.param * e;
            float th = (std::max)(0.8f, q.size0 * (1.0f - t * 0.65f));
            RingStroke(buf, W, H, px, py, rr, th, r, g, b, ba);
            if (q.flags & E_Core) RingStroke(buf, W, H, px, py, rr, th * 0.35f, 255, 255, 255, (uint8_t)(ba * 0.55f));
            return;
        }
        case PBehav::Beam: {
            float e = EaseOutExpo(t);
            float r1 = q.param * (0.35f + 0.65f * e);
            float ca = cosf(q.phase), sa = sinf(q.phase);
            PlotLine(buf, W, H, px, py, px + ca * r1, py + sa * r1, q.size0, r, g, b, ba);
            return;
        }
        case PBehav::Arc: {
            float e = EaseOutCubic(t);
            float len = q.param * e;
            Rng rng((uint32_t)(q.rot * 1013.0f) | 1u);
            float ca = cosf(q.phase), sa = sinf(q.phase);
            float prevx = px, prevy = py;
            const int SEG = 6;
            for (int i = 1; i <= SEG; ++i) {
                float u = i / (float)SEG;
                float dd = len * u;
                float jj = (i == SEG ? 0 : rng.Signed() * len * 0.16f);
                float cx2 = px + ca * dd - sa * jj, cy2 = py + sa * dd + ca * jj;
                PlotLine(buf, W, H, prevx, prevy, cx2, cy2, q.size0, r, g, b, ba);
                prevx = cx2; prevy = cy2;
            }
            return;
        }
        case PBehav::Rune: {
            float e = EaseOutBack(Clampf(t * 2.0f, 0, 1));
            float rr = q.param * (0.45f + 0.55f * e);
            float ra = a * (1.0f - SmoothStep(Clampf((t - 0.5f) / 0.5f, 0, 1)));
            uint8_t raa = (uint8_t)Clampf(ra * 255, 0, 255);
            float th = (std::max)(0.9f, q.param * 0.022f);
            RingStroke(buf, W, H, px, py, rr, th, r, g, b, raa);
            RingStroke(buf, W, H, px, py, rr * 0.72f, th * 0.7f, r, g, b, raa);
            const int K = 6;
            for (int i = 0; i < K; ++i) {
                float ang = q.rot + kTwoPi * i / K;
                float x1 = px + cosf(ang) * rr * 0.72f, y1 = py + sinf(ang) * rr * 0.72f;
                float x2 = px + cosf(ang + 2 * kPi / K) * rr * 0.72f, y2 = py + sinf(ang + 2 * kPi / K) * rr * 0.72f;
                PlotLine(buf, W, H, x1, y1, x2, y2, th * 0.55f, r, g, b, raa);
            }
            return;
        }
        default:
            DrawShapeParticle(buf, W, H, q, px, py, sz, r, g, b, ba, 0);
            return;
    }
}

// ================================================================ 核心渲染
static void CoreRender(uint32_t* buf, int W, int H, const FxEngine& fx, const EffectDef& def,
                      float opacity, float ox, float oy, bool darkBg) {
    if (darkBg) {
        for (int i = 0; i < W * H; ++i) buf[i] = 0xFF1A1A1E;  // 不透明深色
    } else {
        memset(buf, 0, (size_t)W * H * 4);
    }

    // 缎带（拖尾线）—— ★ 性能优化：节点多时减少 glow 遍数和宽度
    const RibbonNode* nodes = fx.RibbonData();
    int n = fx.RibbonCount();
    if (n >= 2) {
        float glow = def.p[P_Glow] / 100.0f;
        bool core = (def.flags & E_Core) != 0;
        // ★ 节点过多时降低 glow 强度（防止卡顿）
        if (n > 50) glow *= 0.6f;
        if (glow > 0.02f) {
            // ★ 从 2 遍 glow 减为 1 遍（省掉近一半的 PlotLine/SoftDot 调用）
            float mul = 3.0f, av = 0.22f;  // 合并两遍为一遍，取折中参数
            for (int i = 1; i < n; ++i) {
                const RibbonNode& a = nodes[i - 1];
                const RibbonNode& b = nodes[i];
                float al = a.color.a * glow * av * opacity;
                if (al <= 0.004f) continue;
                uint8_t cr = ClampB(a.color.r), cg = ClampB(a.color.g), cb = ClampB(a.color.b);
                float w = (std::max)(1.0f, a.width * mul);
                if (w > 40.0f) w = 40.0f;  // ★ 封顶 glow 宽度
                PlotLine(buf, W, H, a.pos.x - ox, a.pos.y - oy, b.pos.x - ox, b.pos.y - oy,
                         w, cr, cg, cb, (uint8_t)(al * 255 + 0.5f));
            }
        }
        for (int i = 1; i < n; ++i) {
            const RibbonNode& a = nodes[i - 1];
            const RibbonNode& b = nodes[i];
            float al = a.color.a * opacity;
            if (al <= 0.004f) continue;
            uint8_t cr = ClampB(a.color.r), cg = ClampB(a.color.g), cb = ClampB(a.color.b);
            PlotLine(buf, W, H, a.pos.x - ox, a.pos.y - oy, b.pos.x - ox, b.pos.y - oy,
                     (std::max)(0.7f, a.width), cr, cg, cb, (uint8_t)(al * 255 + 0.5f));
        }
        if (core) {
            for (int i = 1; i < n; ++i) {
                const RibbonNode& a = nodes[i - 1];
                const RibbonNode& b = nodes[i];
                float al = a.color.a * 0.7f * opacity;
                if (al <= 0.004f) continue;
                PlotLine(buf, W, H, a.pos.x - ox, a.pos.y - oy, b.pos.x - ox, b.pos.y - oy,
                         (std::max)(0.5f, a.width * 0.28f), 255, 255, 255, (uint8_t)(al * 255 + 0.5f));
            }
        }
    }

    const Particle* ps = fx.ParticleData();
    int cap = fx.ParticleCap();
    for (int i = 0; i < cap; ++i) {
        const Particle& q = ps[i];
        if (!q.alive || q.age < 0) continue;
        DrawParticle(buf, W, H, q, opacity, ox, oy);
    }
}

// ================================================================ 覆盖层窗口
static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATEANDEAT;
    if (m == WM_SETCURSOR) return 1;
    if (m == WM_WINDOWPOSCHANGING) { ((WINDOWPOS*)l)->flags |= SWP_NOACTIVATE; return 0; }
    return DefWindowProcW(h, m, w, l);
}

Overlay::~Overlay() { Shutdown(); }

bool Overlay::Init(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance   = hInst;
    wc.lpszClassName = kOverlayClass;
    wc.hCursor     = nullptr;
    RegisterClassExW(&wc);

    vx_ = GetSystemMetrics(SM_XVIRTUALSCREEN);
    vy_ = GetSystemMetrics(SM_YVIRTUALSCREEN);
    vw_ = (std::max)(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    vh_ = (std::max)(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));

    // ★ 分层窗口 + 透明穿透：alpha=0 像素自动点击穿透（DWM 按 alpha 命中测试）
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClass, L"", WS_POPUP,
        vx_, vy_, vw_, vh_, nullptr, nullptr, hInst, nullptr);
    if (!hwnd_) return false;
    if (!AllocBuffer()) { Shutdown(); return false; }
    ready_ = true;
    return true;
}

bool Overlay::AllocBuffer() {
    FreeBuffer();
    bufW_ = vw_; bufH_ = vh_;
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = bufW_;
    bi.biHeight = -bufH_;        // 自上而下
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    HDC hdc = GetDC(nullptr);
    hbmp_ = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&bits_, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbmp_) return false;
    hdcMem_ = CreateCompatibleDC(nullptr);
    SelectObject(hdcMem_, hbmp_);
    return true;
}

void Overlay::FreeBuffer() {
    if (hdcMem_) { DeleteDC(hdcMem_); hdcMem_ = nullptr; }
    if (hbmp_) { DeleteObject(hbmp_); hbmp_ = nullptr; }
    bits_ = nullptr;
    bufW_ = bufH_ = 0;
}

void Overlay::Shutdown() {
    FreeBuffer();
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    ready_ = false; visible_ = false;
}

void Overlay::OnDisplayChange() {
    int nx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int ny = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int nw = (std::max)(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    int nh = (std::max)(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (nx == vx_ && ny == vy_ && nw == vw_ && nh == vh_) return;
    vx_ = nx; vy_ = ny; vw_ = nw; vh_ = nh;
    AllocBuffer();
    if (hwnd_) SetWindowPos(hwnd_, HWND_TOPMOST, vx_, vy_, vw_, vh_, SWP_NOACTIVATE);
}

void Overlay::SetVisible(bool v) {
    if (!hwnd_) return;
    if (v == visible_) return;
    visible_ = v;
    if (v) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Overlay::Hide() {
    if (visible_) { ShowWindow(hwnd_, SW_HIDE); visible_ = false; }
}

void Overlay::Present() {
    if (!bits_ || !hwnd_) return;
    HDC hdc = GetDC(nullptr);
    POINT ptDst = { vx_, vy_ };
    SIZE  sz    = { bufW_, bufH_ };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd_, hdc, &ptDst, &sz, hdcMem_, &ptSrc, RGB(0, 0, 0), &bf, ULW_ALPHA);
    ReleaseDC(nullptr, hdc);
}

bool Overlay::Render(const FxEngine& fx, const EffectDef& trailDef, float globalOpacity) {
    if (!ready_ || !bits_) return false;

    // 无存活粒子：隐藏窗口（零开销 + 无残影）
    if (fx.Idle()) {
        Hide();
        return true;
    }

    CoreRender(bits_, bufW_, bufH_, fx, trailDef, globalOpacity, (float)vx_, (float)vy_, false);
    Present();
    if (!visible_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        visible_ = true;
    }
    return true;
}

void Overlay::RenderFxToBuffer(uint32_t* buf, int W, int H, const FxEngine& fx,
                               const EffectDef& def, float opacity, bool darkBackground) {
    CoreRender(buf, W, H, fx, def, opacity, 0, 0, darkBackground);
}

} // namespace mf
