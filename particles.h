// particles.h : 粒子引擎（固定容量对象池，运行期零堆分配）
#pragma once
#include "common.h"
#include "effects.h"
#include "config.h"

namespace mf {

// 粒子更新行为
enum class PBehav : uint8_t {
    Free,    // 常规物理：速度 + 重力 + 阻尼 + 摆动
    Ring,    // 同心环：半径随时间扩张
    Orbit,   // 绕锚点公转
    Beam,    // 自锚点发出的射线
    Arc,     // 折线电弧
    Rune,    // 法阵（自转 + 缩放）
};

struct Particle {
    Vec2     pos, vel, anchor;
    float    age = 0, life = 1;
    float    size0 = 4, size1 = 0;   // 起止尺寸
    float    rot = 0, spin = 0;
    float    phase = 0;              // 摆动/闪烁相位
    float    param = 0;              // 通用：环厚度 / 轨道半径 / 射线角
    float    swing = 0, gravity = 0, drag = 0.9f;
    float    glow = 0;               // 0~1 辉光强度
    float    hueShift = 0;
    Color4f  cA, cB;
    Shape    shape = Shape::Soft;
    PBehav   behav = PBehav::Free;
    uint16_t flags = 0;
    bool     alive = false;
};

// 生命周期 alpha 曲线（渲染期使用）
inline float ParticleAlpha(const Particle& q) {
    if (q.age < 0) return 0.0f;
    float t = q.age / (q.life > 1e-4f ? q.life : 1e-4f);
    float in  = t < 0.08f ? (t / 0.08f) : 1.0f;
    float out = 1.0f - SmoothStep(Clampf((t - 0.25f) / 0.75f, 0.0f, 1.0f));
    return Clampf(in * out, 0.0f, 1.0f);
}

// 当前尺寸
inline float ParticleSize(const Particle& q) {
    float t = Clampf(q.age / (q.life > 1e-4f ? q.life : 1e-4f), 0.0f, 1.0f);
    return Lerp(q.size0, q.size1, EaseOutQuad(t));
}

// 缎带节点（每帧重建）
struct RibbonNode {
    Vec2    pos;
    float   width;
    Color4f color;
};

inline constexpr int kMaxParticles = 3072;
inline constexpr int kMaxSamples   = 192;   // 鼠标轨迹采样环形缓冲
inline constexpr int kMaxRibbon    = 512;

class FxEngine {
public:
    FxEngine();

    // 配置（切换特效或改参数时调用）
    void Configure(const EffectDef& trail, const EffectDef& click,
                   float globalScale, float globalOpacity);
    void SetTrailOn(bool v) { trailOn_ = v; }
    void SetClickOn(bool v) { clickOn_ = v; }
    void SetIdleFade(bool v){ idleFade_ = v; }

    // 输入
    void PushMouse(Vec2 p, double t);
    void PushClick(Vec2 p, int button, double t);

    // 推进模拟
    void Update(double now);
    void Clear();

    // 输出
    const Particle*   ParticleData() const { return pool_; }
    int               ParticleCap()  const { return kMaxParticles; }
    int               LiveCount()    const { return live_; }
    const RibbonNode* RibbonData()   const { return ribbon_; }
    int               RibbonCount()  const { return ribbonN_; }
    bool              Idle()         const { return live_ == 0 && ribbonN_ < 2; }

    // 供预览：强制注入一段轨迹
    void SeedPreviewPath(Vec2 center, float radius, double now);

private:
    Particle* Alloc();
    void      EmitTrail(Vec2 p, Vec2 dir, float speedPix, double now);
    void      BuildRibbon(double now);
    void      SpawnClick(Vec2 p, double now);
    Color4f   PickColor(const EffectDef& d, float t) const;

    Particle pool_[kMaxParticles];
    int      cursor_ = 0;
    int      live_   = 0;

    struct Sample { Vec2 p; double t; };
    Sample   samples_[kMaxSamples];
    int      sampleN_ = 0, sampleHead_ = 0;

    RibbonNode ribbon_[kMaxRibbon];
    int        ribbonN_ = 0;

    EffectDef trail_{}, click_{};
    bool      hasTrail_ = false, hasClick_ = false;
    bool      trailOn_ = true, clickOn_ = true, idleFade_ = true;
    float     scale_ = 1.0f, opacity_ = 1.0f;

    double    lastUpdate_ = 0;
    double    lastEmit_   = 0;
    double    lastMove_   = 0;
    Vec2      lastPos_{};
    Vec2      lastEmitPos_{};
    float     emitAccum_ = 0;
    float     hueClock_  = 0;
    bool      first_ = true;
};

} // namespace mf
