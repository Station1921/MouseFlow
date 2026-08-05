#include "particles.h"

namespace mf {

// ---------------------------------------------------------------- 工具
static inline float ParamOr(const EffectDef& d, int pid) { return d.p[pid]; }

FxEngine::FxEngine() {
    for (auto& q : pool_) q.alive = false;
}

void FxEngine::Configure(const EffectDef& trail, const EffectDef& click,
                         float globalScale, float globalOpacity) {
    trail_    = trail;
    click_    = click;
    hasTrail_ = true;
    hasClick_ = true;
    scale_    = Clampf(globalScale, 0.3f, 3.0f);
    opacity_  = Clampf(globalOpacity, 0.05f, 1.0f);
}

void FxEngine::Clear() {
    for (auto& q : pool_) q.alive = false;
    live_ = 0;
    sampleN_ = 0;
    sampleHead_ = 0;
    ribbonN_ = 0;
    first_ = true;
    emitAccum_ = 0;
}

Particle* FxEngine::Alloc() {
    for (int i = 0; i < kMaxParticles; ++i) {
        int k = (cursor_ + i) % kMaxParticles;
        if (!pool_[k].alive) {
            cursor_ = (k + 1) % kMaxParticles;
            pool_[k] = Particle{};
            pool_[k].alive = true;
            ++live_;
            return &pool_[k];
        }
    }
    // 池满：覆盖最老的槽位
    Particle* q = &pool_[cursor_];
    cursor_ = (cursor_ + 1) % kMaxParticles;
    *q = Particle{};
    q->alive = true;
    return q;
}

// ---------------------------------------------------------------- 颜色
Color4f FxEngine::PickColor(const EffectDef& d, float t) const {
    if (d.flags & E_Rainbow) {
        float h = hueClock_ * (0.05f + d.p[P_Hue] / 200.0f) + t * 0.35f;
        return HsvToRgb(h, 0.85f, 1.0f);
    }
    Color4f c = LerpC(d.colorA, d.colorB, Clampf(t, 0.0f, 1.0f));
    if (d.p[P_Hue] > 0.5f) {
        float h, s, v;
        RgbToHsv(c, h, s, v);
        h += hueClock_ * (d.p[P_Hue] / 300.0f);
        c = HsvToRgb(h, s, v);
    }
    return c;
}

// ---------------------------------------------------------------- 输入
void FxEngine::PushMouse(Vec2 p, double t) {
    if (sampleN_ > 0) {
        const Sample& last = samples_[(sampleHead_ - 1 + kMaxSamples) % kMaxSamples];
        if ((p - last.p).LengthSq() < 0.6f && (t - last.t) < 0.05) return;
    }
    samples_[sampleHead_] = { p, t };
    sampleHead_ = (sampleHead_ + 1) % kMaxSamples;
    if (sampleN_ < kMaxSamples) ++sampleN_;
    lastMove_ = t;
}

void FxEngine::PushClick(Vec2 p, int /*button*/, double t) {
    if (!clickOn_ || !hasClick_) return;
    SpawnClick(p, t);
}

// ---------------------------------------------------------------- 拖尾发射
void FxEngine::EmitTrail(Vec2 p, Vec2 dir, float speedPix, double now) {
    const EffectDef& d = trail_;
    float sz     = d.p[P_Size]   * scale_;
    float life   = d.p[P_Life];
    float spd    = d.p[P_Speed];
    float spread = d.p[P_Spread] * scale_;
    float grav   = d.p[P_Gravity];
    float spin   = d.p[P_Spin];
    float swing  = d.p[P_Swing];

    Particle* q = Alloc();
    q->pos    = p;
    q->anchor = p;
    q->life   = life * g_rng.Range(0.75f, 1.15f);
    q->age    = 0;
    q->shape  = d.shape;
    q->behav  = PBehav::Free;
    q->flags  = (uint16_t)(d.flags & 0xFFFF);
    q->rot    = g_rng.Range(0, kTwoPi);
    q->spin   = spin * kDegToRad * g_rng.Range(-1.0f, 1.0f);
    q->phase  = g_rng.Range(0, kTwoPi);
    q->swing  = swing * 0.5f;
    q->gravity= grav;
    q->drag   = 0.965f;
    q->size0  = sz * g_rng.Range(0.7f, 1.25f);
    q->size1  = q->size0 * 0.05f;
    q->glow   = d.p[P_Glow] / 100.0f;

    Vec2 perp = dir.Perp();
    Vec2 off  = perp * (g_rng.Signed() * spread * 0.5f);
    q->pos += off;

    // 运动模式
    Vec2 base{};
    switch (d.motion) {
        case Motion::Rise:
            base = Vec2(g_rng.Signed() * spd * 0.35f, -spd * g_rng.Range(0.5f, 1.0f));
            break;
        case Motion::Fall:
            base = Vec2(g_rng.Signed() * spd * 0.35f, spd * g_rng.Range(0.3f, 0.8f));
            break;
        case Motion::Explode: {
            float a = g_rng.Range(0, kTwoPi);
            base = Vec2(cosf(a), sinf(a)) * (spd * g_rng.Range(0.4f, 1.0f));
            break;
        }
        case Motion::Orbit: {
            q->behav  = PBehav::Orbit;
            q->param  = spread > 1 ? spread : 24.0f;
            q->phase  = g_rng.Range(0, kTwoPi);
            q->spin   = (spin > 1 ? spin : 180.0f) * kDegToRad * (g_rng.Chance(0.5f) ? 1.0f : -1.0f);
            break;
        }
        case Motion::Swing:
            base = perp * (g_rng.Signed() * spd * 0.5f);
            q->swing = (swing > 1 ? swing : 30.0f);
            break;
        case Motion::Cling:
            base = dir * (-speedPix * 0.05f) + perp * (g_rng.Signed() * spd * 0.2f);
            q->drag = 0.90f;
            break;
        case Motion::Drift:
        default: {
            float a = g_rng.Range(0, kTwoPi);
            base = Vec2(cosf(a), sinf(a)) * (spd * g_rng.Range(0.2f, 0.7f));
            break;
        }
    }
    q->vel = base;

    float tc = g_rng.NextFloat();
    q->cA = PickColor(d, tc);
    q->cB = PickColor(d, Clampf(tc + 0.45f, 0.0f, 1.0f));
}

// ---------------------------------------------------------------- 点击特效
void FxEngine::SpawnClick(Vec2 p, double /*now*/) {
    const EffectDef& d = click_;
    int   n      = Clampi((int)(d.p[P_Count] + 0.5f), 1, 220);
    float sz     = d.p[P_Size]   * scale_;
    float life   = d.p[P_Life];
    float spd    = d.p[P_Speed]  * scale_;
    float spread = d.p[P_Spread] * scale_;
    float grav   = d.p[P_Gravity];
    float spin   = d.p[P_Spin];
    float swing  = d.p[P_Swing];

    auto mk = [&](void) -> Particle* {
        Particle* q = Alloc();
        q->anchor = p;
        q->pos    = p;
        q->shape  = d.shape;
        q->flags  = (uint16_t)(d.flags & 0xFFFF);
        q->gravity= grav;
        q->swing  = swing * 0.5f;
        q->drag   = 0.94f;
        q->glow   = d.p[P_Glow] / 100.0f;
        return q;
    };

    switch (d.click) {
        // ---------- 多层同心涟漪 ----------
        case ClickStyle::Ripple: {
            int rings = Clampi(n / 8, 2, 6);
            for (int i = 0; i < rings; ++i) {
                Particle* q = mk();
                q->behav = PBehav::Ring;
                q->life  = life * (1.0f + i * 0.18f);
                q->age   = -i * life * 0.14f;          // 依次触发
                q->param = (spread > 1 ? spread : 90.0f) * (1.0f - i * 0.10f);
                q->size0 = (std::max)(1.5f, sz * 0.22f) * (1.0f - i * 0.12f);
                q->cA = PickColor(d, i / (float)rings);
                q->cB = PickColor(d, 1.0f);
            }
            break;
        }
        // ---------- 冲击光环（单个厚环 + 高亮） ----------
        case ClickStyle::Shock: {
            Particle* q = mk();
            q->behav = PBehav::Ring;
            q->life  = life;
            q->param = spread > 1 ? spread : 110.0f;
            q->size0 = (std::max)(2.0f, sz * 0.4f);
            q->cA = PickColor(d, 0.0f);
            q->cB = PickColor(d, 1.0f);
            for (int i = 0; i < n; ++i) {
                Particle* s = mk();
                float a = g_rng.Range(0, kTwoPi);
                s->behav = PBehav::Free;
                s->life  = life * g_rng.Range(0.5f, 0.9f);
                s->vel   = Vec2(cosf(a), sinf(a)) * (spd * g_rng.Range(0.6f, 1.2f));
                s->size0 = sz * 0.28f * g_rng.Range(0.6f, 1.2f);
                s->size1 = 0;
                s->spin  = spin * kDegToRad;
                s->cA = PickColor(d, g_rng.NextFloat());
                s->cB = PickColor(d, 1.0f);
            }
            break;
        }
        // ---------- 放射爆发 ----------
        case ClickStyle::Burst:
        case ClickStyle::Splash:
        case ClickStyle::Shard: {
            bool splash = (d.click == ClickStyle::Splash);
            for (int i = 0; i < n; ++i) {
                Particle* q = mk();
                float a = (kTwoPi * i) / n + g_rng.Signed() * (kPi / n);
                float v = spd * g_rng.Range(splash ? 0.25f : 0.55f, 1.15f);
                q->vel   = Vec2(cosf(a), sinf(a)) * v;
                q->life  = life * g_rng.Range(0.7f, 1.2f);
                q->size0 = sz * g_rng.Range(0.55f, 1.3f);
                q->size1 = splash ? q->size0 * 0.4f : 0.0f;
                q->rot   = a;
                q->spin  = spin * kDegToRad * g_rng.Range(-1.0f, 1.0f);
                q->drag  = splash ? 0.90f : 0.945f;
                q->cA = PickColor(d, g_rng.NextFloat());
                q->cB = PickColor(d, 1.0f);
            }
            break;
        }
        // ---------- 纸屑翻飞 ----------
        case ClickStyle::Confetti: {
            for (int i = 0; i < n; ++i) {
                Particle* q = mk();
                float a = g_rng.Range(-kPi * 0.95f, -kPi * 0.05f);
                float v = spd * g_rng.Range(0.45f, 1.1f);
                q->vel   = Vec2(cosf(a), sinf(a)) * v;
                q->life  = life * g_rng.Range(0.8f, 1.35f);
                q->size0 = sz * g_rng.Range(0.6f, 1.15f);
                q->size1 = q->size0;
                q->rot   = g_rng.Range(0, kTwoPi);
                q->spin  = (spin > 1 ? spin : 300.0f) * kDegToRad * g_rng.Range(-1.4f, 1.4f);
                q->swing = (swing > 1 ? swing : 35.0f);
                q->drag  = 0.985f;
                q->cA = PickColor(d, g_rng.NextFloat());
                q->cB = q->cA;
            }
            break;
        }
        // ---------- 旋转法阵 ----------
        case ClickStyle::Rune: {
            Particle* c = mk();
            c->behav = PBehav::Rune;
            c->life  = life;
            c->param = spread > 1 ? spread : 70.0f;
            c->size0 = c->param;
            c->spin  = (spin > 1 ? spin : 90.0f) * kDegToRad;
            c->cA = PickColor(d, 0.0f);
            c->cB = PickColor(d, 1.0f);
            int glyphs = Clampi(n, 3, 24);
            for (int i = 0; i < glyphs; ++i) {
                Particle* q = mk();
                q->behav = PBehav::Orbit;
                q->life  = life * g_rng.Range(0.85f, 1.0f);
                q->param = (spread > 1 ? spread : 70.0f) * 0.78f;
                q->phase = (kTwoPi * i) / glyphs;
                q->spin  = (spin > 1 ? spin : 90.0f) * kDegToRad;
                q->size0 = sz * 0.55f;
                q->size1 = 0;
                q->cA = PickColor(d, i / (float)glyphs);
                q->cB = PickColor(d, 1.0f);
            }
            break;
        }
        // ---------- 电弧放射 ----------
        case ClickStyle::Arc: {
            int arcs = Clampi(n / 3, 3, 16);
            for (int i = 0; i < arcs; ++i) {
                Particle* q = mk();
                q->behav = PBehav::Arc;
                q->life  = life * g_rng.Range(0.5f, 1.0f);
                q->phase = (kTwoPi * i) / arcs + g_rng.Signed() * 0.25f;
                q->param = (spread > 1 ? spread : 90.0f) * g_rng.Range(0.55f, 1.15f);
                q->size0 = (std::max)(1.2f, sz * 0.16f);
                q->rot   = g_rng.NextFloat() * 1000.0f;   // 折线随机种子
                q->cA = PickColor(d, g_rng.NextFloat());
                q->cB = PickColor(d, 1.0f);
            }
            break;
        }
        // ---------- 光柱节拍 ----------
        case ClickStyle::Beam: {
            int beams = Clampi(n / 2, 3, 24);
            for (int i = 0; i < beams; ++i) {
                Particle* q = mk();
                q->behav = PBehav::Beam;
                q->life  = life * g_rng.Range(0.6f, 1.1f);
                q->phase = (kTwoPi * i) / beams + g_rng.Signed() * 0.1f;
                q->param = (spread > 1 ? spread : 100.0f) * g_rng.Range(0.6f, 1.25f);
                q->size0 = (std::max)(1.5f, sz * 0.22f);
                q->cA = PickColor(d, i / (float)beams);
                q->cB = PickColor(d, 1.0f);
            }
            Particle* c = mk();
            c->behav = PBehav::Ring;
            c->life  = life * 0.75f;
            c->param = (spread > 1 ? spread : 100.0f) * 0.55f;
            c->size0 = (std::max)(1.5f, sz * 0.3f);
            c->cA = PickColor(d, 0.0f);
            c->cB = PickColor(d, 1.0f);
            break;
        }
        // ---------- 卫星轨道 ----------
        case ClickStyle::Orbit: {
            int sats = Clampi(n, 3, 40);
            for (int i = 0; i < sats; ++i) {
                Particle* q = mk();
                q->behav = PBehav::Orbit;
                q->life  = life * g_rng.Range(0.8f, 1.2f);
                q->param = (spread > 1 ? spread : 60.0f) * g_rng.Range(0.6f, 1.15f);
                q->phase = (kTwoPi * i) / sats;
                q->spin  = (spin > 1 ? spin : 240.0f) * kDegToRad * (g_rng.Chance(0.35f) ? -1.0f : 1.0f);
                q->size0 = sz * g_rng.Range(0.6f, 1.1f);
                q->size1 = 0;
                q->cA = PickColor(d, i / (float)sats);
                q->cB = PickColor(d, 1.0f);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------- 缎带
void FxEngine::BuildRibbon(double now) {
    ribbonN_ = 0;
    if (!trailOn_ || !hasTrail_) return;
    if (trail_.trail != TrailStyle::Ribbon && trail_.trail != TrailStyle::RibbonPlus) return;
    if (sampleN_ < 2) return;

    const EffectDef& d = trail_;
    float life  = (std::max)(0.08f, d.p[P_Life]);
    float width = (std::max)(1.0f, d.p[P_Size] * scale_);
    int   segs  = Clampi((int)(d.p[P_Count] + 0.5f), 6, 100);
    float swing = d.p[P_Swing];

    // 收集有效期内的采样点（从新到旧）
    Vec2  pts[kMaxSamples];
    float ages[kMaxSamples];
    int   m = 0;
    for (int i = 0; i < sampleN_; ++i) {
        int k = (sampleHead_ - 1 - i + kMaxSamples * 2) % kMaxSamples;
        double age = now - samples_[k].t;
        if (age > life) break;
        pts[m]  = samples_[k].p;
        ages[m] = (float)(age / life);
        ++m;
        if (m >= kMaxSamples) break;
    }
    if (m < 2) return;

    // Catmull-Rom 细分
    int total = Clampi(segs, 8, kMaxRibbon - 2);
    for (int i = 0; i < total; ++i) {
        float u  = i / (float)(total - 1);        // 0=头(最新) 1=尾(最旧)
        float fp = u * (m - 1);
        int   i1 = Clampi((int)fp, 0, m - 1);
        int   i0 = Clampi(i1 - 1, 0, m - 1);
        int   i2 = Clampi(i1 + 1, 0, m - 1);
        int   i3 = Clampi(i1 + 2, 0, m - 1);
        float ft = fp - i1;
        Vec2 pos = CatmullRom(pts[i0], pts[i1], pts[i2], pts[i3], ft);
        float ag = Lerp(ages[i1], ages[i2], ft);

        // 摆动
        if (swing > 0.5f && i > 0) {
            Vec2 dir  = (pts[i2] - pts[i0]).Normalized();
            Vec2 perp = dir.Perp();
            float w = sinf(u * 9.0f - (float)now * 5.0f) * swing * 0.14f * scale_;
            pos += perp * w;
        }

        float fade = 1.0f - Clampf(ag, 0.0f, 1.0f);
        float taper = (d.flags & E_Taper) ? powf(fade, 0.62f) : 1.0f;
        // 头部略微收窄，避免突兀的圆头
        float head = u < 0.06f ? SmoothStep(u / 0.06f) * 0.35f + 0.65f : 1.0f;

        RibbonNode& nd = ribbon_[ribbonN_++];
        nd.pos   = pos;
        nd.width = width * taper * head;
        Color4f c = PickColor(d, u);
        nd.color = WithAlpha(c, fade * fade * opacity_);
        if (ribbonN_ >= kMaxRibbon) break;
    }
}

// ---------------------------------------------------------------- 主循环
void FxEngine::Update(double now) {
    float dt = first_ ? 0.016f : (float)(now - lastUpdate_);
    dt = Clampf(dt, 0.0f, 0.1f);
    lastUpdate_ = now;
    hueClock_ += dt;

    // --- 拖尾粒子发射 ---
    if (trailOn_ && hasTrail_ && sampleN_ >= 2) {
        const EffectDef& d = trail_;
        bool wantParticles = (d.trail == TrailStyle::Particles ||
                              d.trail == TrailStyle::Glow ||
                              d.trail == TrailStyle::RibbonPlus);
        if (wantParticles) {
            const Sample& s1 = samples_[(sampleHead_ - 1 + kMaxSamples) % kMaxSamples];
            Vec2 cur = s1.p;
            if (first_) { lastEmitPos_ = cur; first_ = false; }

            Vec2  delta = cur - lastEmitPos_;
            float dist  = delta.Length();
            float count = (std::max)(2.0f, d.p[P_Count]);
            float spacing = 300.0f / count / (d.trail == TrailStyle::Glow ? 1.6f : 1.0f);
            spacing = Clampf(spacing, 1.5f, 40.0f);

            if (dist > 0.01f) {
                Vec2 dir = delta.Normalized();
                emitAccum_ += dist;
                int guard = 0;
                while (emitAccum_ >= spacing && guard++ < 64) {
                    emitAccum_ -= spacing;
                    float k = 1.0f - (emitAccum_ / (std::max)(dist, 0.001f));
                    Vec2 sp = LerpV(lastEmitPos_, cur, Clampf(k, 0.0f, 1.0f));
                    EmitTrail(sp, dir, dist / (std::max)(dt, 0.001f), now);
                }
                lastEmitPos_ = cur;
            } else if (now - lastMove_ < 0.12 && d.trail == TrailStyle::Glow) {
                // 悬停时缓慢续冒
                if (now - lastEmit_ > 0.05) {
                    lastEmit_ = now;
                    EmitTrail(cur, Vec2(1, 0), 0.0f, now);
                }
            }
        }
        first_ = false;
    }

    // --- 粒子推进 ---
    for (int i = 0; i < kMaxParticles; ++i) {
        Particle& q = pool_[i];
        if (!q.alive) continue;
        q.age += dt;
        if (q.age < 0) continue;               // 延迟触发
        if (q.age >= q.life) { q.alive = false; --live_; continue; }

        float t = q.age / q.life;
        switch (q.behav) {
            case PBehav::Free: {
                q.vel.y += q.gravity * dt;
                float damp = powf(q.drag, dt * 60.0f);
                q.vel = q.vel * damp;
                q.pos += q.vel * dt;
                if (q.swing > 0.01f) {
                    q.pos.x += sinf(q.age * 6.0f + q.phase) * q.swing * dt;
                    q.pos.y += cosf(q.age * 4.5f + q.phase) * q.swing * 0.35f * dt;
                }
                q.rot += q.spin * dt;
                break;
            }
            case PBehav::Ring: {
                q.rot += q.spin * dt;
                break;                          // 半径在渲染期按 t 求值
            }
            case PBehav::Orbit: {
                float ang = q.phase + q.spin * q.age;
                float r   = q.param * EaseOutCubic(Clampf(t * 1.6f, 0.0f, 1.0f));
                q.pos = q.anchor + Vec2(cosf(ang), sinf(ang)) * r;
                q.rot = ang + kPi * 0.5f;
                break;
            }
            case PBehav::Beam:
            case PBehav::Arc:
            case PBehav::Rune:
                q.rot += q.spin * dt;
                break;
        }
    }

    BuildRibbon(now);
}

// ---------------------------------------------------------------- 预览
void FxEngine::SeedPreviewPath(Vec2 center, float radius, double now) {
    // 生成一条李萨如曲线轨迹，用于设置界面的实时预览
    float w = (float)now * 1.15f;
    Vec2 p{ center.x + cosf(w * 1.0f) * radius,
            center.y + sinf(w * 1.7f) * radius * 0.55f };
    PushMouse(p, now);
}

} // namespace mf
