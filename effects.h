// effects.h : 参数化特效定义（30 拖尾 + 25 点击）
#pragma once
#include "common.h"

namespace mf {

// ---------------------------------------------------------------- 可调参数
enum ParamId : int {
    P_Count = 0,   // 元素数量
    P_Size,        // 元素大小
    P_Life,        // 存续时间
    P_Speed,       // 扩散速度
    P_Spread,      // 扩散范围
    P_Gravity,     // 垂直加速度
    P_Spin,        // 旋转速度
    P_Swing,       // 摆动强度
    P_Glow,        // 辉光强度
    P_Hue,         // 色相流动
    P_NUMERIC
};

struct ParamMeta {
    const wchar_t* name;
    float lo, hi;
    int   decimals;
    const wchar_t* suffix;
};

extern const ParamMeta kParamMeta[P_NUMERIC];

// 参数暴露位
inline constexpr uint32_t Bit(int i) { return 1u << i; }
inline constexpr uint32_t F_ColorA = Bit(24);   // 暴露起始颜色
inline constexpr uint32_t F_ColorB = Bit(25);   // 暴露结束颜色

// 特效行为标志
inline constexpr uint32_t E_Additive = Bit(0);  // 加色混合（发光类）
inline constexpr uint32_t E_Rainbow  = Bit(1);  // 默认按色相循环着色
inline constexpr uint32_t E_Flicker  = Bit(2);  // 明暗闪烁
inline constexpr uint32_t E_Jitter   = Bit(3);  // 位置抖动（电流感）
inline constexpr uint32_t E_Taper    = Bit(4);  // 缎带尾部收窄
inline constexpr uint32_t E_Core     = Bit(5);  // 高亮内芯

// ---------------------------------------------------------------- 形状
enum class Shape : uint8_t {
    Soft,       // 柔光圆点
    Circle,     // 实心圆
    Ring,       // 圆环
    Square,     // 方块
    Triangle,   // 三角
    Hexagon,    // 六边形
    Diamond,    // 菱形
    Star4,      // 四角星芒
    Star5,      // 五角星
    Heart,      // 爱心
    Petal,      // 花瓣
    Leaf,       // 叶片
    Butterfly,  // 蝴蝶
    Snowflake,  // 雪晶
    Bubble,     // 气泡
    Bolt,       // 电弧
    Streak,     // 短线
    Blob,       // 不规则墨滴
};

// ---------------------------------------------------------------- 运动
enum class Motion : uint8_t {
    Drift,      // 自然漂移
    Rise,       // 向上升腾
    Fall,       // 缓缓下落
    Orbit,      // 环绕旋转
    Swing,      // 柔和摆动
    Explode,    // 向外冲击
    Cling,      // 紧凑跟随
};

// ---------------------------------------------------------------- 拖尾渲染风格
enum class TrailStyle : uint8_t {
    Ribbon,     // 连续缎带
    Glow,       // 柔雾光团
    Particles,  // 粒子喷发
    RibbonPlus, // 缎带 + 粒子
};

// ---------------------------------------------------------------- 点击渲染风格
enum class ClickStyle : uint8_t {
    Ripple,     // 多层涟漪
    Burst,      // 放射粒子
    Confetti,   // 翻飞纸片
    Splash,     // 飞溅墨滴
    Rune,       // 旋转法阵
    Arc,        // 电弧放射
    Beam,       // 光柱节拍
    Orbit,      // 卫星轨道
    Shard,      // 晶体扩散
    Shock,      // 冲击光环
};

// ---------------------------------------------------------------- 特效定义
struct EffectDef {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* desc;
    TrailStyle  trail;
    ClickStyle  click;
    Shape       shape;
    Motion      motion;
    Color4f     colorA;
    Color4f     colorB;
    float       p[P_NUMERIC];
    uint32_t    exposed;
    uint32_t    flags;
};

extern const EffectDef kTrailEffects[];
extern const int       kTrailEffectCount;
extern const EffectDef kClickEffects[];
extern const int       kClickEffectCount;

int FindTrailIndex(const std::wstring& id);
int FindClickIndex(const std::wstring& id);

} // namespace mf
