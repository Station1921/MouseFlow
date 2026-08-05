// effects.cpp : 特效参数表
#include "effects.h"

namespace mf {

Rng g_rng(0x1234ABCDu);

const ParamMeta kParamMeta[P_NUMERIC] = {
    { L"元素数量",   1.0f,  100.0f, 0, L"" },
    { L"元素大小",   2.0f,   48.0f, 0, L" px" },
    { L"存续时间",   0.15f,   3.0f, 2, L" s" },
    { L"扩散速度",   0.0f,  400.0f, 0, L"" },
    { L"扩散范围",   0.0f,  160.0f, 0, L" px" },
    { L"垂直加速度", -400.0f, 800.0f, 0, L"" },
    { L"旋转速度",   0.0f,  720.0f, 0, L" °/s" },
    { L"摆动强度",   0.0f,  100.0f, 0, L"" },
    { L"辉光强度",   0.0f,  100.0f, 0, L"%" },
    { L"色相流动",   0.0f,  200.0f, 0, L"" },
};

// 参数暴露组合
#define EX_RIBBON (Bit(P_Count)|Bit(P_Size)|Bit(P_Life)|Bit(P_Swing)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)
#define EX_GLOW   (Bit(P_Size)|Bit(P_Life)|Bit(P_Spread)|Bit(P_Swing)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)
#define EX_PART   (Bit(P_Count)|Bit(P_Size)|Bit(P_Life)|Bit(P_Speed)|Bit(P_Spread)|Bit(P_Gravity)|Bit(P_Spin)|Bit(P_Swing)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)
#define EX_RPLUS  (EX_PART)
#define EX_CBURST (Bit(P_Count)|Bit(P_Size)|Bit(P_Life)|Bit(P_Speed)|Bit(P_Gravity)|Bit(P_Spin)|Bit(P_Swing)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)
#define EX_CWAVE  (Bit(P_Count)|Bit(P_Size)|Bit(P_Life)|Bit(P_Speed)|Bit(P_Spread)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)
#define EX_CRUNE  (Bit(P_Count)|Bit(P_Size)|Bit(P_Life)|Bit(P_Spread)|Bit(P_Spin)|Bit(P_Glow)|Bit(P_Hue)|F_ColorA|F_ColorB)

// =============================================================== 拖尾特效
const EffectDef kTrailEffects[] = {
{ L"neon-comet", L"霓虹彗星", L"高亮内芯配青紫渐变辉光，锐利且明亮。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Cling,
  Rgb(0x22E6FF), Rgb(0xA855F7),
  { 60, 14, 0.55f, 0, 0, 0, 0, 0, 85, 0 },
  EX_RIBBON, E_Additive | E_Taper | E_Core },

{ L"rainbow-trail", L"彩虹光轨", L"连续圆角渐变与多层辉光构成的丝滑彩虹光带。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Cling,
  Rgb(0xFF3B6B), Rgb(0x3B82F6),
  { 70, 16, 0.70f, 0, 0, 0, 0, 0, 75, 60 },
  EX_RIBBON, E_Additive | E_Taper | E_Rainbow | E_Core },

{ L"deepsea-ribbon", L"深海绸带", L"海蓝与薄荷青交融的柔软波动光带。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Swing,
  Rgb(0x1E6BFF), Rgb(0x5EEAD4),
  { 64, 18, 0.80f, 0, 0, 0, 0, 25, 60, 0 },
  EX_RIBBON, E_Additive | E_Taper },

{ L"emerald-mist", L"翡翠流雾", L"翠绿与湖蓝交融的清透光雾。",
  TrailStyle::Glow, ClickStyle::Ripple, Shape::Soft, Motion::Drift,
  Rgb(0x10B981), Rgb(0x38BDF8),
  { 40, 26, 1.10f, 12, 18, 0, 0, 10, 55, 0 },
  EX_GLOW, E_Additive },

{ L"rose-cloud", L"玫瑰云雾", L"玫粉与淡紫组成温柔朦胧的云雾轨迹。",
  TrailStyle::Glow, ClickStyle::Ripple, Shape::Soft, Motion::Drift,
  Rgb(0xF472B6), Rgb(0xC4B5FD),
  { 40, 28, 1.20f, 10, 20, -20, 0, 12, 50, 0 },
  EX_GLOW, E_Additive },

{ L"moonlight-veil", L"月光流纱", L"银白月光拖出安静细腻的冰蓝薄纱。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Cling,
  Rgb(0xEAF6FF), Rgb(0xA5C8E8),
  { 58, 12, 0.90f, 0, 0, 0, 0, 8, 45, 0 },
  EX_RIBBON, E_Additive | E_Taper },

{ L"candy-ribbon", L"糖果缎带", L"蜜桃橙与莓果粉交替摆动的柔亮缎带。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Swing,
  Rgb(0xFF9A62), Rgb(0xFF5FA2),
  { 62, 17, 0.75f, 0, 0, 0, 0, 40, 65, 0 },
  EX_RIBBON, E_Additive | E_Taper },

{ L"ember-flame", L"余烬火焰", L"橙红余烬向上升腾并逐渐熄灭。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Soft, Motion::Rise,
  Rgb(0xFF7A18), Rgb(0xFFD166),
  { 45, 7, 0.85f, 45, 14, -140, 0, 12, 80, 0 },
  EX_PART, E_Additive | E_Flicker },

{ L"ghost-flame", L"幽灵冷焰", L"幽蓝冷焰向上升腾并化为透明薄雾。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Soft, Motion::Rise,
  Rgb(0x38BDF8), Rgb(0xA78BFA),
  { 40, 8, 1.00f, 40, 16, -120, 0, 16, 70, 0 },
  EX_PART, E_Additive | E_Flicker },

{ L"nebula-mist", L"星云光雾", L"蓝紫星云般宽阔柔和的弥散光团。",
  TrailStyle::Glow, ClickStyle::Ripple, Shape::Soft, Motion::Drift,
  Rgb(0x6366F1), Rgb(0xC084FC),
  { 40, 34, 1.40f, 14, 26, 0, 0, 14, 60, 0 },
  EX_GLOW, E_Additive },

{ L"plasma-band", L"等离子飘带", L"洋红与紫电轻微抖动的高能流体光带。",
  TrailStyle::Ribbon, ClickStyle::Arc, Shape::Soft, Motion::Cling,
  Rgb(0xFF2FD0), Rgb(0x7C3AED),
  { 66, 15, 0.60f, 0, 0, 0, 0, 18, 90, 0 },
  EX_RIBBON, E_Additive | E_Taper | E_Jitter | E_Core },

{ L"cyber-hex", L"赛博六角", L"青色六边形以数字化节奏扩散。",
  TrailStyle::Particles, ClickStyle::Shard, Shape::Hexagon, Motion::Explode,
  Rgb(0x22D3EE), Rgb(0x0EA5E9),
  { 28, 10, 0.70f, 70, 22, 0, 180, 0, 70, 0 },
  EX_PART, E_Additive },

{ L"pixel-blocks", L"像素方块", L"利落的绿色像素方块轨迹。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Square, Motion::Cling,
  Rgb(0x4ADE80), Rgb(0x16A34A),
  { 35, 8, 0.55f, 20, 10, 0, 0, 0, 25, 0 },
  EX_PART, 0 },

{ L"prism-triangle", L"三角棱镜", L"彩色三角棱镜沿轨迹折射流动。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Triangle, Motion::Drift,
  Rgb(0xFF6B6B), Rgb(0x4ECDC4),
  { 30, 11, 0.80f, 35, 18, 0, 200, 0, 55, 90 },
  EX_PART, E_Rainbow | E_Additive },

{ L"aurora-ribbon", L"极光丝带", L"青绿与紫罗兰交织的柔软波浪。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Swing,
  Rgb(0x34D399), Rgb(0x8B5CF6),
  { 68, 22, 1.00f, 0, 0, 0, 0, 45, 60, 0 },
  EX_RIBBON, E_Additive | E_Taper },

{ L"galaxy-dust", L"银河星尘", L"细密星芒以螺旋方式围绕鼠标轨迹。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Star4, Motion::Orbit,
  Rgb(0xE0E7FF), Rgb(0x818CF8),
  { 50, 6, 1.10f, 30, 28, 0, 120, 0, 75, 0 },
  EX_PART, E_Additive | E_Flicker },

{ L"mint-leaf", L"薄荷叶影", L"清新绿叶旋转摆动并缓缓飘落。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Leaf, Motion::Fall,
  Rgb(0x6EE7B7), Rgb(0x34D399),
  { 22, 13, 1.60f, 25, 20, 90, 140, 35, 15, 0 },
  EX_PART, 0 },

{ L"sakura-fall", L"樱花飘落", L"粉色花瓣旋转飞散并轻轻下落。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Petal, Motion::Fall,
  Rgb(0xFFC1DA), Rgb(0xFF7FB0),
  { 24, 12, 1.80f, 22, 22, 80, 160, 40, 18, 0 },
  EX_PART, 0 },

{ L"butterfly-dream", L"蝶梦流萤", L"薰衣草色蝴蝶在鼠标经过处轻盈飞舞。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Butterfly, Motion::Swing,
  Rgb(0xC4B5FD), Rgb(0xA78BFA),
  { 14, 16, 1.50f, 35, 26, -10, 60, 55, 30, 0 },
  EX_PART, 0 },

{ L"firefly-glow", L"萤火微光", L"忽明忽暗的黄绿色光点。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Soft, Motion::Drift,
  Rgb(0xFDE047), Rgb(0xA3E635),
  { 26, 7, 1.30f, 18, 24, -15, 0, 20, 85, 0 },
  EX_PART, E_Additive | E_Flicker },

{ L"crimson-laser", L"赤红激光", L"迅速熄灭的高速红色扫描线。",
  TrailStyle::Ribbon, ClickStyle::Arc, Shape::Soft, Motion::Cling,
  Rgb(0xFF1744), Rgb(0xFF6B6B),
  { 50, 8, 0.22f, 0, 0, 0, 0, 0, 95, 0 },
  EX_RIBBON, E_Additive | E_Taper | E_Core },

{ L"thunder-echo", L"雷霆残影", L"高亮电火花急速抖动，留下短促雷光。",
  TrailStyle::RibbonPlus, ClickStyle::Arc, Shape::Bolt, Motion::Cling,
  Rgb(0xA5F3FC), Rgb(0x6366F1),
  { 18, 10, 0.30f, 60, 16, 0, 0, 0, 95, 0 },
  EX_RPLUS, E_Additive | E_Jitter | E_Core },

{ L"solar-flare", L"太阳耀斑", L"金橙色火花高速喷射并向上翻卷。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Streak, Motion::Rise,
  Rgb(0xFFB020), Rgb(0xFF5722),
  { 48, 9, 0.70f, 90, 18, -200, 90, 0, 88, 0 },
  EX_PART, E_Additive | E_Flicker },

{ L"corona-storm", L"日冕风暴", L"金黄向赤橙过渡的灼热太阳风光雾。",
  TrailStyle::Glow, ClickStyle::Shock, Shape::Soft, Motion::Drift,
  Rgb(0xFFD34E), Rgb(0xFF6A00),
  { 40, 30, 1.00f, 16, 22, -30, 0, 14, 75, 0 },
  EX_GLOW, E_Additive },

{ L"ink-smoke", L"墨影烟岚", L"灰紫墨烟缓慢舒展，边缘柔和消散。",
  TrailStyle::Glow, ClickStyle::Splash, Shape::Soft, Motion::Drift,
  Rgb(0x6B7280), Rgb(0x7C3AED),
  { 40, 32, 1.60f, 10, 24, -25, 0, 18, 20, 0 },
  EX_GLOW, 0 },

{ L"void-portal", L"虚空传送门", L"深紫六边能量围绕轨迹形成微型传送门。",
  TrailStyle::RibbonPlus, ClickStyle::Rune, Shape::Hexagon, Motion::Orbit,
  Rgb(0x7C3AED), Rgb(0x4C1D95),
  { 20, 12, 0.90f, 25, 30, 0, 220, 0, 70, 0 },
  EX_RPLUS, E_Additive },

{ L"bubble-drift", L"气泡漂流", L"透明气泡摇摆上浮并自然消散。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Bubble, Motion::Rise,
  Rgb(0xBAE6FD), Rgb(0xE0F2FE),
  { 18, 14, 1.60f, 25, 22, -70, 0, 30, 30, 0 },
  EX_PART, 0 },

{ L"heart-rose", L"心动玫瑰", L"玫红爱心随轨迹轻柔摆动并逐渐绽放。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Heart, Motion::Swing,
  Rgb(0xFF4D8D), Rgb(0xFF9EC4),
  { 18, 14, 1.20f, 28, 20, -20, 40, 35, 45, 0 },
  EX_PART, 0 },

{ L"soft-glow", L"柔光粒子", L"周身发光的顺滑柔和轨迹。",
  TrailStyle::Particles, ClickStyle::Ripple, Shape::Soft, Motion::Drift,
  Rgb(0xFFFFFF), Rgb(0x93C5FD),
  { 40, 10, 0.90f, 20, 16, 0, 0, 10, 70, 0 },
  EX_PART, E_Additive },

{ L"golden-star", L"鎏金星芒", L"闪耀星芒随重力缓缓坠落。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Star5, Motion::Fall,
  Rgb(0xFFE066), Rgb(0xFFA500),
  { 26, 11, 1.30f, 40, 24, 180, 180, 0, 80, 0 },
  EX_PART, E_Additive | E_Flicker },
};
const int kTrailEffectCount = (int)(sizeof(kTrailEffects) / sizeof(kTrailEffects[0]));

// =============================================================== 点击特效
const EffectDef kClickEffects[] = {
{ L"ripple", L"光波涟漪", L"三层柔光波纹从点击位置依次扩散。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Explode,
  Rgb(0x7DD3FC), Rgb(0xC4B5FD),
  { 3, 6, 0.70f, 200, 90, 0, 0, 0, 70, 0 },
  EX_CWAVE, E_Additive },

{ L"rainbow-ripple", L"彩虹涟漪", L"多层彩色光波依次扩散消失。",
  TrailStyle::Ribbon, ClickStyle::Ripple, Shape::Soft, Motion::Explode,
  Rgb(0xFF4D6D), Rgb(0x38BDF8),
  { 5, 5, 0.90f, 220, 110, 0, 0, 0, 70, 120 },
  EX_CWAVE, E_Additive | E_Rainbow },

{ L"firework", L"烟花爆裂", L"高亮粒子爆开后受重力缓缓坠落。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Soft, Motion::Explode,
  Rgb(0xFFD166), Rgb(0xFF5F6D),
  { 36, 6, 0.90f, 280, 0, 420, 0, 0, 90, 0 },
  EX_CBURST, E_Additive | E_Flicker },

{ L"golden-shatter", L"烟花碎金", L"金橙色火花高速喷射后翻卷坠落。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Star4, Motion::Explode,
  Rgb(0xFFE9A8), Rgb(0xFF9F1C),
  { 28, 7, 1.00f, 240, 0, 380, 240, 0, 92, 0 },
  EX_CBURST, E_Additive | E_Flicker },

{ L"pixel-burst", L"像素爆破", L"多彩像素方块从点击位置弹射。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Square, Motion::Explode,
  Rgb(0x4ADE80), Rgb(0x38BDF8),
  { 30, 8, 0.80f, 250, 0, 300, 120, 0, 35, 100 },
  EX_CBURST, E_Rainbow },

{ L"ink-splash", L"墨迹飞溅", L"不规则墨滴向四周飞溅并淡出。",
  TrailStyle::Particles, ClickStyle::Splash, Shape::Blob, Motion::Explode,
  Rgb(0x1F2937), Rgb(0x4B5563),
  { 16, 12, 0.70f, 200, 20, 120, 0, 0, 0, 0 },
  EX_CBURST, 0 },

{ L"frost-spread", L"冰晶扩散", L"冰蓝雪晶从点击中心均匀扩散。",
  TrailStyle::Particles, ClickStyle::Shard, Shape::Snowflake, Motion::Explode,
  Rgb(0xBFEFFF), Rgb(0x60A5FA),
  { 14, 12, 0.90f, 170, 0, 0, 140, 0, 60, 0 },
  EX_CBURST, E_Additive },

{ L"heart-bloom", L"爱心绽放", L"玫红爱心围绕点击位置向外盛开。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Heart, Motion::Explode,
  Rgb(0xFF4D8D), Rgb(0xFFC0DA),
  { 12, 15, 0.90f, 150, 0, -60, 60, 0, 55, 0 },
  EX_CBURST, 0 },

{ L"energy-impact", L"能量冲击", L"高能光环快速膨胀并伴随中心闪光。",
  TrailStyle::Ribbon, ClickStyle::Shock, Shape::Ring, Motion::Explode,
  Rgb(0xA5F3FC), Rgb(0x22D3EE),
  { 2, 8, 0.55f, 320, 100, 0, 0, 0, 95, 0 },
  EX_CWAVE, E_Additive | E_Core },

{ L"arc-pulse", L"电弧脉冲", L"多道蓝紫电弧瞬间击向四周。",
  TrailStyle::Particles, ClickStyle::Arc, Shape::Bolt, Motion::Explode,
  Rgb(0x93C5FD), Rgb(0xA855F7),
  { 8, 6, 0.35f, 260, 80, 0, 0, 0, 95, 0 },
  EX_CWAVE, E_Additive | E_Jitter },

{ L"magic-circle", L"魔法法阵", L"旋转六边符文与双层光环组成法阵。",
  TrailStyle::Ribbon, ClickStyle::Rune, Shape::Hexagon, Motion::Orbit,
  Rgb(0xC4B5FD), Rgb(0x7C3AED),
  { 6, 48, 1.10f, 0, 60, 0, 90, 0, 75, 0 },
  EX_CRUNE, E_Additive },

{ L"starburst", L"星芒绽放", L"整齐星芒从中心向四周放射。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Streak, Motion::Explode,
  Rgb(0xFFFFFF), Rgb(0xFDE68A),
  { 16, 10, 0.55f, 300, 0, 0, 0, 0, 90, 0 },
  EX_CBURST, E_Additive | E_Core },

{ L"golden-starburst", L"鎏金星芒", L"彩色星芒高速散开后受重力落下。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Star5, Motion::Explode,
  Rgb(0xFFE066), Rgb(0xFF8A00),
  { 22, 9, 1.00f, 270, 0, 400, 200, 0, 88, 0 },
  EX_CBURST, E_Additive | E_Flicker },

{ L"confetti", L"庆典彩纸", L"多彩方片翻飞下落，呈现轻快庆典效果。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Square, Motion::Fall,
  Rgb(0xFF6B6B), Rgb(0x4ECDC4),
  { 30, 9, 1.60f, 230, 0, 420, 320, 45, 10, 140 },
  EX_CBURST, E_Rainbow },

{ L"rhythm-pulse", L"音律脉冲", L"彩色光柱像节拍器一样摆动和跳跃。",
  TrailStyle::Ribbon, ClickStyle::Beam, Shape::Square, Motion::Swing,
  Rgb(0xF472B6), Rgb(0x38BDF8),
  { 9, 10, 0.70f, 120, 70, 0, 0, 40, 65, 80 },
  EX_CWAVE, E_Additive | E_Rainbow },

{ L"lightning-strike", L"雷电闪击", L"快速抖动的蓝紫电火花。",
  TrailStyle::Particles, ClickStyle::Arc, Shape::Bolt, Motion::Explode,
  Rgb(0xDDD6FE), Rgb(0x4F46E5),
  { 6, 7, 0.30f, 300, 90, 0, 0, 0, 98, 0 },
  EX_CWAVE, E_Additive | E_Jitter },

{ L"bubble-ring", L"气泡圆环", L"透明气泡摇摆上浮并自然消散。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Bubble, Motion::Rise,
  Rgb(0xBAE6FD), Rgb(0xE0F2FE),
  { 14, 13, 1.30f, 140, 0, -120, 0, 30, 30, 0 },
  EX_CBURST, 0 },

{ L"hex-lattice", L"六边晶格", L"青色六边形以数字化节奏扩散。",
  TrailStyle::Particles, ClickStyle::Shard, Shape::Hexagon, Motion::Explode,
  Rgb(0x22D3EE), Rgb(0x0369A1),
  { 12, 13, 0.80f, 180, 0, 0, 160, 0, 65, 0 },
  EX_CBURST, E_Additive },

{ L"prism-shine", L"棱镜晶光", L"彩色棱镜碎片折射出斑斓光泽。",
  TrailStyle::Particles, ClickStyle::Shard, Shape::Diamond, Motion::Explode,
  Rgb(0xFF6B6B), Rgb(0x4ECDC4),
  { 14, 12, 0.85f, 190, 0, 0, 180, 0, 70, 110 },
  EX_CBURST, E_Additive | E_Rainbow },

{ L"snow-crystal", L"冰晶飘雪", L"旋转雪晶带着冰蓝色冷光落下。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Snowflake, Motion::Fall,
  Rgb(0xE0F2FE), Rgb(0x7DD3FC),
  { 18, 11, 1.70f, 120, 0, 200, 150, 40, 40, 0 },
  EX_CBURST, E_Additive },

{ L"petal-scatter", L"花瓣飞散", L"粉色花瓣旋转飞散并轻轻下落。",
  TrailStyle::Particles, ClickStyle::Confetti, Shape::Petal, Motion::Fall,
  Rgb(0xFFC1DA), Rgb(0xFF7FB0),
  { 20, 12, 1.60f, 170, 0, 260, 200, 45, 20, 0 },
  EX_CBURST, 0 },

{ L"pixel-matrix", L"像素矩阵", L"绿色像素格以矩阵方式向外扩散。",
  TrailStyle::Particles, ClickStyle::Shard, Shape::Square, Motion::Explode,
  Rgb(0x4ADE80), Rgb(0x15803D),
  { 16, 9, 0.70f, 160, 0, 0, 0, 0, 30, 0 },
  EX_CBURST, 0 },

{ L"star-orbit", L"星环轨道", L"带卫星光点的紫色微型轨道。",
  TrailStyle::Particles, ClickStyle::Orbit, Shape::Star4, Motion::Orbit,
  Rgb(0xC084FC), Rgb(0x7C3AED),
  { 6, 8, 1.20f, 0, 42, 0, 260, 0, 70, 0 },
  EX_CRUNE, E_Additive },

{ L"triangle-prism", L"三角棱镜", L"彩色三角棱镜向四周折射飞散。",
  TrailStyle::Particles, ClickStyle::Burst, Shape::Triangle, Motion::Explode,
  Rgb(0xFF6B6B), Rgb(0x4ECDC4),
  { 18, 11, 0.90f, 210, 0, 0, 240, 0, 60, 100 },
  EX_CBURST, E_Rainbow | E_Additive },

{ L"rose-mist", L"玫瑰云雾", L"玫粉与淡紫的柔雾自点击处轻轻散开。",
  TrailStyle::Glow, ClickStyle::Shock, Shape::Soft, Motion::Explode,
  Rgb(0xF9A8D4), Rgb(0xC4B5FD),
  { 3, 26, 1.00f, 140, 70, 0, 0, 0, 45, 0 },
  EX_CWAVE, E_Additive },
};
const int kClickEffectCount = (int)(sizeof(kClickEffects) / sizeof(kClickEffects[0]));

int FindTrailIndex(const std::wstring& id) {
    for (int i = 0; i < kTrailEffectCount; ++i)
        if (id == kTrailEffects[i].id) return i;
    return -1; // ★ 找不到返回 -1（不是 0！），防止索引 0 误匹配
}
int FindClickIndex(const std::wstring& id) {
    for (int i = 0; i < kClickEffectCount; ++i)
        if (id == kClickEffects[i].id) return i;
    return -1; // ★ 同上
}

} // namespace mf
