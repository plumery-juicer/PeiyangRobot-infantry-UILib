/**
 *******************************************************************************
 * @file    referee-hud-ui.cpp
 * @brief   裁判系统 HUD 业务 UI 组件实现
 *******************************************************************************
 * @attention
 * 本文件只负责把 RefereeHudInput 翻译为裁判系统图形命令。图形命令通过
 * UiRendererSrvc::draw(...) 提交，实际队列缓存、1/2/5/7 打包和串口发送由
 * UiRendererSrvc 统一处理。
 *******************************************************************************
 */

#include "referee-hud-ui.h"

#include <cmath>
namespace {

/* ------- graphic name allocation ------------------------------------------*/
/*
 * 裁判系统每个图形只有 3 字节名称。这里按 group/component/index 分配，
 * 避免不同 UI 部件之间的 ADD/UPDATE 名称冲突。
 */
constexpr uint8_t kCapacitorGroup         = 1;
constexpr uint8_t kCapArcTrack            = 0;
constexpr uint8_t kCapThresholdTick       = 1;
constexpr uint8_t kCapVoltageLeftArc      = 2;
constexpr uint8_t kCapVoltageRightArc     = 3;
constexpr uint8_t kCapVoltageValue        = 4;

constexpr uint8_t kWheelLegGroup          = 2;
constexpr uint8_t kWheelLegBodyHull       = 0;
constexpr uint8_t kWheelLegBodyRef        = 1;
constexpr uint8_t kWheelLegTicks          = 2;
constexpr uint8_t kWheelLegUpperLink      = 10;
constexpr uint8_t kWheelLegLowerLink      = 11;
constexpr uint8_t kWheelLegWheel          = 12;
constexpr uint8_t kWheelLegLeft           = 0;
constexpr uint8_t kWheelLegRight          = 1;

constexpr uint8_t kSchoolEmblemGroup      = 3;
constexpr float kSchoolEmblemCenterX      = 960.0f;
constexpr float kSchoolEmblemCenterY      = 900.0f;

constexpr uint8_t kAutoAimGroup           = 4;
constexpr uint8_t kAutoAimFrame           = 0;
constexpr uint8_t kAutoAimVehicle         = 1;
constexpr uint8_t kAutoAimEnergyA         = 2;
constexpr uint8_t kAutoAimEnergyB         = 3;
constexpr uint8_t kAutoAimOutpost         = 4;
constexpr uint8_t kAutoAimTrack           = 5;
constexpr uint8_t kAutoAimStateVehicle    = 0;
constexpr uint8_t kAutoAimStateOutpost    = 1;
constexpr uint8_t kAutoAimStateEnergyA    = 2;
constexpr uint8_t kAutoAimStateEnergyB    = 3;
constexpr uint8_t kAimTargetNone          = 0;
constexpr uint8_t kAimTargetLocked        = 1;
constexpr uint8_t kAimTargetFire          = 2;

constexpr uint8_t kBottomFrameGroup       = 5;
constexpr uint8_t kSwitchDeckGroup        = 6;

/* ------- capacitor gauge specification ------------------------------------*/

constexpr float kVoltageStage1            = 7.0f;
constexpr float kVoltageStage2            = 15.0f;
constexpr float kVoltageStage3            = 22.0f;
constexpr float kVoltageStage4            = 26.0f;

constexpr float kCapGaugeCenterX          = 960.0f;
constexpr float kCapGaugeStaticCenterY    = 1000.0f;
constexpr float kCapGaugeDynamicCenterY   = 980.0f;
constexpr float kCapGaugeRadiusX          = 800.0f;
constexpr float kCapGaugeRadiusY          = 220.0f;
constexpr float kCapGaugeTickInnerRadiusX = 772.0f;
constexpr float kCapGaugeTickInnerRadiusY = 210.0f;
constexpr uint16_t kCapGaugeStartAngle    = 140;
constexpr uint16_t kCapGaugeCenterAngle   = 180;
constexpr uint16_t kCapGaugeEndAngle      = 220;
constexpr uint16_t kCapGaugeCenterGap     = 10;
constexpr float kCapVoltageTextX          = 915.0f;
constexpr float kCapVoltageTextY          = 790.0f;
constexpr float kPi                       = 3.14159265358979323846f;

/// 电容弧上的电压刻度，左右镜像绘制。
constexpr float kCapVoltageThresholds[]   = {
    kVoltageStage1,
    kVoltageStage2,
    kVoltageStage3,
    kVoltageStage4,
};

/* ------- wheel-leg widget specification -----------------------------------*/
/*
 * UI 绘制使用缩放后的 105:125 连杆，对应真实机构 210:250 的大腿/小腿比例。
 * 腿长变化通过 thigh angle 和 hip-wheel distance 连续解算，不缩放连杆长度。
 */
constexpr float kWheelLegScale               = 0.48f;
constexpr float kWheelLegHipX                = 960.0f;
constexpr float kWheelLegHipY                = 174.0f;
constexpr float kWheelLegSide                = 1.0f;
constexpr float kWheelLegUpperLinkLength     = 105.0f * kWheelLegScale;
constexpr float kWheelLegLowerLinkLength     = 125.0f * kWheelLegScale;
constexpr float kWheelLegDistanceMinRatio    = RefereeHudSpec::kWheelLegDistanceMinRatio;
constexpr float kWheelLegDistanceMidRatio    = RefereeHudSpec::kWheelLegDistanceMidRatio;
constexpr float kWheelLegDistanceMaxRatio    = RefereeHudSpec::kWheelLegDistanceMaxRatio;

constexpr float kWheelLegDistanceMarkRatios[3] = {
    kWheelLegDistanceMinRatio,
    kWheelLegDistanceMidRatio,
    kWheelLegDistanceMaxRatio,
};

/* ------- local geometry types ---------------------------------------------*/

/**
 * @brief 裁判 UI 坐标系中的浮点点位。
 */
struct PointF {
    float x;
    float y;
};

/**
 * @brief 单条轮腿的三点几何结果。
 */
struct WheelLegGeometry {
    PointF hip;
    PointF knee;
    PointF wheel;
};

/**
 * @brief 单条轮腿的输入姿态。
 */
struct WheelLegPose {
    float thighAngleDeg;
    float hipWheelDistance;
};

/**
 * @brief 左右腿绘制配置。
 */
struct WheelLegConfig {
    uint8_t id;
    UiColor color;
    float hipOffsetX;
};

/**
 * @brief 车体静态线段配置。
 */
struct WheelLegBodyLine {
    float x1;
    float y1;
    float x2;
    float y2;
    UiColor color;
};

/**
 * @brief 校徽静态圆弧配置。
 */
struct SchoolEmblemArc {
    uint8_t id;
    UiColor color;
    uint16_t startAngle;
    uint16_t endAngle;
    uint16_t width;
    int16_t offsetX;
    int16_t offsetY;
    uint16_t radiusX;
    uint16_t radiusY;
};

/**
 * @brief 校徽静态直线配置。
 */
struct SchoolEmblemLine {
    uint8_t id;
    uint16_t width;
    int16_t startOffsetX;
    int16_t startOffsetY;
    int16_t endOffsetX;
    int16_t endOffsetY;
};

/**
 * @brief 自瞄图标的几何语义。
 */
enum class AutoAimGlyph : uint8_t {
    Vehicle,
    EnergyStatic,
    EnergySweep,
    Outpost,
};

/**
 * @brief 单个自瞄模式图标的布局配置。
 */
struct AutoAimIcon {
    uint8_t id;
    uint8_t state;
    AutoAimGlyph glyph;
    float x;
    float y;
    float scale;
    float anchorX;
    float anchorY;
};

/**
 * @brief 自瞄模式区域静态边框线。
 */
struct AutoAimFrameLine {
    uint8_t id;
    UiColor color;
    uint16_t width;
    float x1;
    float y1;
    float x2;
    float y2;
};

/**
 * @brief HUD 通用静态线段配置。
 */
struct HudLine {
    uint8_t id;
    UiColor color;
    uint16_t width;
    float x1;
    float y1;
    float x2;
    float y2;
};

/**
 * @brief 自瞄状态轨道上的选中刻度。
 */
struct AutoAimTrackTick {
    uint8_t id;
    uint8_t state;
    float x1;
    float y;
    float x2;
};

/**
 * @brief 底部四开关图标语义。
 */
enum class SwitchGlyph : uint8_t {
    Capacitor,
    Turbo,
    Feeder,
    Spin,
};

/**
 * @brief 底部开关模块的布局和旋转配置。
 */
struct SwitchModule {
    uint8_t id;
    SwitchGlyph glyph;
    float x;
    float y;
    float rotationDeg;
    float scale;
};

/* ------- static drawing tables --------------------------------------------*/

/// 双腿车体外形静态线段。
constexpr WheelLegBodyLine kWheelLegHullLines[] = {
    {-78.0f, -20.0f, -68.0f, 18.0f, UiColor::Cyan},  {-68.0f, 18.0f, -42.0f, 32.0f, UiColor::Cyan},
    {-42.0f, 32.0f, -8.0f, 32.0f, UiColor::Cyan},    {8.0f, 32.0f, 46.0f, 32.0f, UiColor::Orange},
    {46.0f, 32.0f, 74.0f, 14.0f, UiColor::Orange},   {74.0f, 14.0f, 78.0f, -10.0f, UiColor::Orange},
    {78.0f, -10.0f, 54.0f, -24.0f, UiColor::Orange}, {54.0f, -24.0f, 8.0f, -24.0f, UiColor::Orange},
    {-8.0f, -24.0f, -56.0f, -24.0f, UiColor::Cyan},  {-56.0f, -24.0f, -78.0f, -20.0f, UiColor::Cyan},
};

/// 左右腿动态绘制颜色和横向错开量。
constexpr WheelLegConfig kWheelLegConfigs[] = {
    {kWheelLegLeft, UiColor::Cyan, -6.0f},
    {kWheelLegRight, UiColor::Orange, 6.0f},
};

/// 1x 校徽圆弧近似图形，来自 rm 旧代码的阵营指示器/校徽方案。
constexpr SchoolEmblemArc kSchoolEmblemArcs[] = {
    {1, UiColor::Cyan, 130, 210, 3, -14, 20, 20, 20},  {2, UiColor::Cyan, 155, 225, 3, 14, 20, 20, 20},
    {3, UiColor::Cyan, 298, 315, 3, 3, -55, 40, 80},   {4, UiColor::Cyan, 43, 62, 3, -3, -55, 40, 80},
    {5, UiColor::Cyan, 15, 180, 3, -35, -25, 10, 7},   {6, UiColor::Cyan, 180, 345, 3, 35, -25, 10, 7},
    {7, UiColor::Cyan, 230, 270, 3, 7, -33, 40, 40},   {8, UiColor::Cyan, 90, 130, 3, -7, -33, 40, 40},
    {9, UiColor::Cyan, 217, 230, 3, 91, 18, 150, 120}, {10, UiColor::Cyan, 130, 143, 3, -91, 18, 150, 120},
    {11, UiColor::White, 33, 327, 3, 0, -70, 40, 15},  {12, UiColor::White, 33, 327, 3, 0, -76, 50, 23},
    {16, UiColor::White, 80, 250, 3, -5, -40, 5, 5},   {19, UiColor::White, 80, 280, 3, 13, -40, 7, 5},
};

/// 1x 校徽直线补充图形。
constexpr SchoolEmblemLine kSchoolEmblemLines[] = {
    {13, 3, -20, -25, -6, -25}, {14, 3, -13, -25, -13, -45}, {15, 3, 0, -23, 0, -40},
    {17, 3, 6, -23, 6, -40},    {18, 4, 20, -23, 20, -40},
};

/// 四种自瞄模式图标位置，左右两侧围绕中间视野布置。
constexpr AutoAimIcon kAutoAimIcons[] = {
    {kAutoAimVehicle, kAutoAimStateVehicle, AutoAimGlyph::Vehicle, 674.0f, 575.0f, 0.78f, 0.0f, 0.0f},
    {kAutoAimEnergyA, kAutoAimStateEnergyA, AutoAimGlyph::EnergyStatic, 674.0f, 465.0f, 0.76f, 0.0f, -5.0f},
    {kAutoAimEnergyB, kAutoAimStateEnergyB, AutoAimGlyph::EnergySweep, 1246.0f, 465.0f, 0.76f, 2.0f, 0.0f},
    {kAutoAimOutpost, kAutoAimStateOutpost, AutoAimGlyph::Outpost, 1246.0f, 575.0f, 0.78f, 0.0f, 1.0f},
};

/// 自瞄状态轨道静态角标。
constexpr AutoAimFrameLine kAutoAimFrameLines[] = {
    {0, UiColor::White, 1, 710.0f, 644.0f, 642.0f, 644.0f},
    {1, UiColor::White, 1, 710.0f, 396.0f, 642.0f, 396.0f},
    {2, UiColor::White, 1, 1210.0f, 644.0f, 1278.0f, 644.0f},
    {3, UiColor::White, 1, 1210.0f, 396.0f, 1278.0f, 396.0f},
};

/// 自瞄目标状态轨道，根据 none/locked/fire 动态变色。
constexpr HudLine kAutoAimTrackLines[] = {
    {0, UiColor::Cyan, 2, 746.0f, 420.0f, 746.0f, 620.0f},   {1, UiColor::Cyan, 2, 746.0f, 620.0f, 710.0f, 644.0f},
    {2, UiColor::Cyan, 2, 746.0f, 420.0f, 710.0f, 396.0f},   {3, UiColor::Cyan, 2, 1174.0f, 420.0f, 1174.0f, 620.0f},
    {4, UiColor::Cyan, 2, 1174.0f, 620.0f, 1210.0f, 644.0f}, {5, UiColor::Cyan, 2, 1174.0f, 420.0f, 1210.0f, 396.0f},
};

/// 自瞄模式选中刻度，随 aimModeState 动态加粗。
constexpr AutoAimTrackTick kAutoAimTrackTicks[] = {
    {0, kAutoAimStateVehicle, 746.0f, 575.0f, 704.0f},
    {1, kAutoAimStateEnergyA, 746.0f, 465.0f, 704.0f},
    {2, kAutoAimStateEnergyB, 1174.0f, 465.0f, 1216.0f},
    {3, kAutoAimStateOutpost, 1174.0f, 575.0f, 1216.0f},
};

/// 底部仪表台开放式透视边框。
constexpr HudLine kBottomFrameLines[] = {
    {0, UiColor::Cyan, 3, 520.0f, 58.0f, 650.0f, 84.0f},
    {1, UiColor::Cyan, 3, 690.0f, 92.0f, 848.0f, 128.0f},
    {2, UiColor::White, 2, 588.0f, 124.0f, 666.0f, 142.0f},
    {3, UiColor::White, 2, 720.0f, 154.0f, 852.0f, 184.0f},
    {4, UiColor::White, 1, 520.0f, 58.0f, 576.0f, 112.0f},
    {5, UiColor::White, 1, 656.0f, 86.0f, 684.0f, 146.0f},
    {6, UiColor::White, 1, 790.0f, 116.0f, 810.0f, 174.0f},
    {7, UiColor::Cyan, 3, 1400.0f, 58.0f, 1270.0f, 84.0f},
    {8, UiColor::Cyan, 3, 1230.0f, 92.0f, 1072.0f, 128.0f},
    {9, UiColor::White, 2, 1332.0f, 124.0f, 1254.0f, 142.0f},
    {10, UiColor::White, 2, 1200.0f, 154.0f, 1068.0f, 184.0f},
    {11, UiColor::White, 1, 1400.0f, 58.0f, 1344.0f, 112.0f},
    {12, UiColor::White, 1, 1264.0f, 86.0f, 1236.0f, 146.0f},
    {13, UiColor::White, 1, 1130.0f, 116.0f, 1110.0f, 174.0f},
    {14, UiColor::Cyan, 3, 850.0f, 92.0f, 878.0f, 176.0f},
    {15, UiColor::Cyan, 3, 878.0f, 176.0f, 918.0f, 220.0f},
    {16, UiColor::Cyan, 3, 918.0f, 220.0f, 950.0f, 220.0f},
    {17, UiColor::Cyan, 3, 970.0f, 220.0f, 1002.0f, 220.0f},
    {18, UiColor::Cyan, 3, 1002.0f, 220.0f, 1042.0f, 176.0f},
    {19, UiColor::Cyan, 3, 1042.0f, 176.0f, 1070.0f, 92.0f},
    {20, UiColor::White, 2, 904.0f, 48.0f, 940.0f, 70.0f},
    {21, UiColor::White, 2, 980.0f, 70.0f, 1016.0f, 48.0f},
    {22, UiColor::Cyan, 2, 850.0f, 92.0f, 820.0f, 122.0f},
    {23, UiColor::Cyan, 2, 1070.0f, 92.0f, 1100.0f, 122.0f},
};

/// 四个底部开关模块布局。
constexpr SwitchModule kSwitchModules[] = {
    {0, SwitchGlyph::Capacitor, 612.0f, 98.0f, -12.0f, 0.78f},
    {1, SwitchGlyph::Turbo, 760.0f, 134.0f, -7.0f, 0.90f},
    {2, SwitchGlyph::Feeder, 1160.0f, 134.0f, 7.0f, 0.90f},
    {3, SwitchGlyph::Spin, 1308.0f, 98.0f, 12.0f, 0.78f},
};

/* ------- utility helpers ---------------------------------------------------*/

/**
 * @brief 将浮点值钳制后转换为 uint16_t。
 */
uint16_t clampToUInt16(float value, float minValue, float maxValue) {
    if (value < minValue)
        return static_cast<uint16_t>(minValue);
    if (value > maxValue)
        return static_cast<uint16_t>(maxValue);
    return static_cast<uint16_t>(value);
}

/**
 * @brief 转换为裁判 UI 坐标字段。
 */
uint16_t roundToUiCoord(float value) { return clampToUInt16(value + 0.5f, 0.0f, 2047.0f); }

/**
 * @brief 转换为裁判 UI 宽度字段。
 */
uint16_t roundToUiWidth(float value) { return clampToUInt16(value + 0.5f, 1.0f, 1023.0f); }

uint8_t normalizeLegLengthState(uint8_t state) { return static_cast<uint8_t>(state % 3); }

uint8_t normalizeAimModeState(uint8_t state) { return static_cast<uint8_t>(state % 4); }

uint8_t normalizeAimTargetState(uint8_t state) {
    if (state >= kAimTargetFire)
        return kAimTargetFire;
    return state;
}

/**
 * @brief 根据电容输入状态选择电容弧颜色。
 */
UiColor capVoltageColor(const RefereeHudInput& input) {
    if (!input.capEnabled)
        return UiColor::Cyan;
    if (input.capError)
        return UiColor::Pink;
    if (input.capVoltage > kVoltageStage3)
        return UiColor::Green;
    if (input.capVoltage > kVoltageStage2)
        return UiColor::Orange;
    return UiColor::Pink;
}

/**
 * @brief 根据自瞄目标状态选择状态轨道颜色。
 */
UiColor aimTargetColor(uint8_t targetState) {
    switch (normalizeAimTargetState(targetState)) {
        case kAimTargetFire:
            return UiColor::Purple;
        case kAimTargetLocked:
            return UiColor::Green;
        case kAimTargetNone:
        default:
            return UiColor::Pink;
    }
}

/**
 * @brief 计算电容弧上某个角度对应的点位。
 */
PointF capArcPoint(float angleDegrees, float radiusX = kCapGaugeRadiusX, float radiusY = kCapGaugeRadiusY) {
    const float rad = angleDegrees * kPi / 180.0f;
    return {
        kCapGaugeCenterX + radiusX * std::sin(rad),
        kCapGaugeStaticCenterY + radiusY * std::cos(rad),
    };
}

/**
 * @brief 将电压映射到左右电容弧的角度跨度。
 */
uint16_t capGaugeVoltageAngleSpan(float voltage) {
    const float sweep = static_cast<float>(kCapGaugeEndAngle - kCapGaugeCenterAngle - kCapGaugeCenterGap);
    return static_cast<uint16_t>(kCapGaugeCenterGap + clampToUInt16(voltage * sweep / kVoltageStage4, 0.0f, sweep));
}

/**
 * @brief 将电容电压压缩为三位有效数字，减小数值显示抖动。
 */
float capVoltageThreeSignificant(float voltage) {
    if (!std::isfinite(voltage) || voltage < 0.0f)
        return 0.0f;
    if (voltage < 10.0f)
        return std::round(voltage * 100.0f) / 100.0f;
    return std::round(voltage * 10.0f) / 10.0f;
}

/**
 * @brief 3 字节裁判图形名称。
 */
struct GraphicName {
    uint8_t bytes[3];
};

GraphicName graphicName(uint8_t group, uint8_t id, uint8_t subId) { return GraphicName{{group, id, subId}}; }

/**
 * @brief 将任意角度规整到裁判系统圆弧角度范围。
 */
uint16_t normalizeArcAngle(float angleDegrees) {
    while (angleDegrees < 0.0f) {
        angleDegrees += 360.0f;
    }
    while (angleDegrees >= 360.0f) {
        angleDegrees -= 360.0f;
    }
    return roundToUiCoord(angleDegrees);
}

/* ------- auto-aim icon helpers --------------------------------------------*/

/**
 * @brief 将自瞄图标局部坐标转换为裁判 UI 坐标。
 */
PointF autoAimLocal(const AutoAimIcon& icon, float x, float y) {
    return {icon.x + (x - icon.anchorX) * icon.scale, icon.y + (y - icon.anchorY) * icon.scale};
}

UiColor autoAimMainColor(bool active) { return active ? UiColor::Main : UiColor::White; }

UiColor autoAimDetailColor(bool) { return UiColor::White; }

uint16_t autoAimWidth(const AutoAimIcon& icon, float width) { return roundToUiWidth(width * icon.scale); }

/**
 * @brief 绘制自瞄图标局部直线。
 */
void drawAutoAimLine(UiRendererSrvc& renderer, const AutoAimIcon& icon, uint8_t subId, GraphicOption option,
                     UiColor color, float width, float x1, float y1, float x2, float y2) {
    const PointF start = autoAimLocal(icon, x1, y1);
    const PointF end   = autoAimLocal(icon, x2, y2);
    auto name          = graphicName(kAutoAimGroup, icon.id, subId);
    auto graphic       = renderer.draw(name.bytes, option);
    graphic.layer(1)
        .color(color)
        .width(autoAimWidth(icon, width))
        .start(roundToUiCoord(start.x), roundToUiCoord(start.y))
        .asLine(roundToUiCoord(end.x), roundToUiCoord(end.y));
}

/**
 * @brief 绘制自瞄图标局部圆。
 */
void drawAutoAimCircle(UiRendererSrvc& renderer, const AutoAimIcon& icon, uint8_t subId, GraphicOption option,
                       UiColor color, float width, float x, float y, float radius) {
    const PointF center = autoAimLocal(icon, x, y);
    auto name           = graphicName(kAutoAimGroup, icon.id, subId);
    auto graphic        = renderer.draw(name.bytes, option);
    graphic.layer(1)
        .color(color)
        .width(autoAimWidth(icon, width))
        .start(roundToUiCoord(center.x), roundToUiCoord(center.y))
        .asCircle(roundToUiCoord(radius * icon.scale));
}

/**
 * @brief 绘制自瞄图标局部圆弧。
 */
void drawAutoAimArc(UiRendererSrvc& renderer, const AutoAimIcon& icon, uint8_t subId, GraphicOption option,
                    UiColor color, float width, float x, float y, float radius, float startAngle, float endAngle) {
    const PointF center = autoAimLocal(icon, x, y);
    auto name           = graphicName(kAutoAimGroup, icon.id, subId);
    auto graphic        = renderer.draw(name.bytes, option);
    graphic.layer(1)
        .color(color)
        .width(autoAimWidth(icon, width))
        .start(roundToUiCoord(center.x), roundToUiCoord(center.y))
        .asArc(normalizeArcAngle(startAngle), normalizeArcAngle(endAngle), roundToUiCoord(radius * icon.scale),
               roundToUiCoord(radius * icon.scale));
}

/**
 * @brief 绘制五扇叶能量机关图标主体。
 * @return 下一个可用的子图形 ID。
 */
uint8_t drawAutoAimFan(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, bool active,
                       float phaseDeg) {
    const UiColor bladeColor = autoAimMainColor(active);
    for (uint8_t i = 0; i < 5; ++i) {
        const float angle = phaseDeg + static_cast<float>(i) * 72.0f;
        drawAutoAimArc(renderer, icon, i, option, bladeColor, 5.0f, 0.0f, 0.0f, 19.0f, angle - 17.0f, angle + 17.0f);
    }
    drawAutoAimCircle(renderer, icon, 5, option, autoAimDetailColor(active), 2.0f, 0.0f, 0.0f, 5.0f);
    return 6;
}

/**
 * @brief 绘制车辆/装甲目标模式图标。
 */
void drawAutoAimVehicleIcon(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, bool active) {
    const UiColor main   = autoAimMainColor(active);
    const UiColor detail = autoAimDetailColor(active);
    drawAutoAimArc(renderer, icon, 0, option, main, 3.0f, 0.0f, 0.0f, 25.0f, 25.0f, 125.0f);
    drawAutoAimArc(renderer, icon, 1, option, main, 3.0f, 0.0f, 0.0f, 25.0f, 205.0f, 305.0f);
    drawAutoAimLine(renderer, icon, 2, option, detail, 3.0f, 0.0f, 16.0f, 16.0f, 0.0f);
    drawAutoAimLine(renderer, icon, 3, option, detail, 3.0f, 16.0f, 0.0f, 0.0f, -16.0f);
    drawAutoAimLine(renderer, icon, 4, option, detail, 3.0f, 0.0f, -16.0f, -16.0f, 0.0f);
    drawAutoAimLine(renderer, icon, 5, option, detail, 3.0f, -16.0f, 0.0f, 0.0f, 16.0f);
    drawAutoAimLine(renderer, icon, 6, option, main, 2.0f, -24.0f, 0.0f, 24.0f, 0.0f);
    drawAutoAimCircle(renderer, icon, 7, option, main, 2.0f, 0.0f, 0.0f, 4.0f);
}

/**
 * @brief 绘制能量机关定点模式图标。
 */
void drawAutoAimEnergyStaticIcon(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, bool active) {
    const uint8_t nextId = drawAutoAimFan(renderer, icon, option, active, -90.0f);
    drawAutoAimLine(renderer, icon, nextId, option, autoAimMainColor(active), 3.0f, -11.0f, -29.0f, 11.0f, -29.0f);
}

/**
 * @brief 绘制能量机关扫掠模式图标。
 */
void drawAutoAimEnergySweepIcon(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, bool active) {
    const UiColor main   = autoAimMainColor(active);
    const uint8_t nextId = drawAutoAimFan(renderer, icon, option, active, -54.0f);
    drawAutoAimArc(renderer, icon, nextId, option, main, 3.0f, 0.0f, 0.0f, 30.0f, 18.0f, 120.0f);
    drawAutoAimLine(renderer, icon, static_cast<uint8_t>(nextId + 1), option, main, 3.0f, 27.0f, 16.0f, 34.0f, 23.0f);
}

/**
 * @brief 绘制前哨站模式图标，选中时加粗关键部分。
 */
void drawAutoAimOutpostIcon(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, bool active) {
    const UiColor main    = autoAimMainColor(active);
    const UiColor detail  = autoAimDetailColor(active);
    const float mainWidth = active ? 6.0f : 3.0f;
    const float coreWidth = active ? 4.0f : 2.0f;
    drawAutoAimArc(renderer, icon, 0, option, main, mainWidth, 0.0f, 8.0f, 17.0f, 42.0f, 138.0f);
    drawAutoAimLine(renderer, icon, 1, option, detail, 3.0f, -10.0f, 14.0f, 10.0f, 14.0f);
    drawAutoAimLine(renderer, icon, 2, option, detail, 3.0f, 0.0f, 18.0f, 0.0f, -18.0f);
    drawAutoAimLine(renderer, icon, 3, option, detail, 3.0f, -15.0f, -20.0f, 0.0f, 13.0f);
    drawAutoAimLine(renderer, icon, 4, option, detail, 3.0f, 15.0f, -20.0f, 0.0f, 13.0f);
    drawAutoAimLine(renderer, icon, 5, option, main, mainWidth, -19.0f, -22.0f, 19.0f, -22.0f);
    drawAutoAimCircle(renderer, icon, 6, option, main, coreWidth, 0.0f, 2.0f, 5.0f);
}

/**
 * @brief 按图标语义分发自瞄模式图标绘制。
 */
void drawAutoAimIcon(UiRendererSrvc& renderer, const AutoAimIcon& icon, GraphicOption option, uint8_t activeState) {
    const bool active = icon.state == activeState;
    switch (icon.glyph) {
        case AutoAimGlyph::Vehicle:
            drawAutoAimVehicleIcon(renderer, icon, option, active);
            break;
        case AutoAimGlyph::EnergyStatic:
            drawAutoAimEnergyStaticIcon(renderer, icon, option, active);
            break;
        case AutoAimGlyph::EnergySweep:
            drawAutoAimEnergySweepIcon(renderer, icon, option, active);
            break;
        case AutoAimGlyph::Outpost:
            drawAutoAimOutpostIcon(renderer, icon, option, active);
            break;
        default:
            break;
    }
}

/* ------- switch deck helpers ----------------------------------------------*/

/**
 * @brief 将开关模块局部坐标转换为裁判 UI 坐标。
 */
PointF switchPoint(const SwitchModule& module, float x, float y) {
    const float rad = module.rotationDeg * kPi / 180.0f;
    const float px  = x * module.scale;
    const float py  = y * module.scale;
    return {
        module.x + std::cos(rad) * px - std::sin(rad) * py,
        module.y + std::sin(rad) * px + std::cos(rad) * py,
    };
}

/**
 * @brief 绘制开关模块局部直线。
 */
void drawSwitchLine(UiRendererSrvc& renderer, const SwitchModule& module, uint8_t subId, GraphicOption option,
                    UiColor color, float width, float x1, float y1, float x2, float y2, uint8_t layer) {
    const PointF start = switchPoint(module, x1, y1);
    const PointF end   = switchPoint(module, x2, y2);
    auto name          = graphicName(kSwitchDeckGroup, module.id, subId);
    auto graphic       = renderer.draw(name.bytes, option);
    graphic.layer(layer)
        .color(color)
        .width(roundToUiWidth(width))
        .start(roundToUiCoord(start.x), roundToUiCoord(start.y))
        .asLine(roundToUiCoord(end.x), roundToUiCoord(end.y));
}

/**
 * @brief 绘制开关模块局部圆。
 */
void drawSwitchCircle(UiRendererSrvc& renderer, const SwitchModule& module, uint8_t subId, GraphicOption option,
                      UiColor color, float width, float x, float y, float radius, uint8_t layer) {
    const PointF center = switchPoint(module, x, y);
    auto name           = graphicName(kSwitchDeckGroup, module.id, subId);
    auto graphic        = renderer.draw(name.bytes, option);
    graphic.layer(layer)
        .color(color)
        .width(roundToUiWidth(width * module.scale))
        .start(roundToUiCoord(center.x), roundToUiCoord(center.y))
        .asCircle(roundToUiCoord(radius * module.scale));
}

/**
 * @brief 绘制开关模块局部圆弧。
 */
void drawSwitchArc(UiRendererSrvc& renderer, const SwitchModule& module, uint8_t subId, GraphicOption option,
                   UiColor color, float width, float x, float y, float radius, float startAngle, float endAngle,
                   uint8_t layer) {
    const PointF center = switchPoint(module, x, y);
    auto name           = graphicName(kSwitchDeckGroup, module.id, subId);
    auto graphic        = renderer.draw(name.bytes, option);
    graphic.layer(layer)
        .color(color)
        .width(roundToUiWidth(width * module.scale))
        .start(roundToUiCoord(center.x), roundToUiCoord(center.y))
        .asArc(normalizeArcAngle(startAngle + module.rotationDeg), normalizeArcAngle(endAngle + module.rotationDeg),
               roundToUiCoord(radius * module.scale), roundToUiCoord(radius * module.scale));
}

/**
 * @brief 绘制电容开关图标。
 */
void drawCapSwitchIcon(UiRendererSrvc& renderer, const SwitchModule& module, GraphicOption option, UiColor active,
                       UiColor fill) {
    drawSwitchLine(renderer, module, 10, option, fill, 3.0f, -10.0f, 12.0f, -10.0f, -8.0f, 4);
    drawSwitchLine(renderer, module, 11, option, fill, 3.0f, 10.0f, 12.0f, 10.0f, -8.0f, 4);
    drawSwitchLine(renderer, module, 12, option, active, 2.0f, -18.0f, 0.0f, -10.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 13, option, active, 2.0f, 10.0f, 0.0f, 18.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 14, option, active, 2.0f, -2.0f, 15.0f, -8.0f, 3.0f, 4);
    drawSwitchLine(renderer, module, 15, option, active, 2.0f, -8.0f, 3.0f, 4.0f, 3.0f, 4);
    drawSwitchLine(renderer, module, 16, option, active, 2.0f, 4.0f, 3.0f, -2.0f, -13.0f, 4);
}

/**
 * @brief 绘制极速模式开关图标。
 */
void drawTurboSwitchIcon(UiRendererSrvc& renderer, const SwitchModule& module, GraphicOption option, UiColor active,
                         UiColor fill) {
    drawSwitchLine(renderer, module, 10, option, fill, 3.0f, -21.0f, -12.0f, -5.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 11, option, fill, 3.0f, -5.0f, 0.0f, -21.0f, 12.0f, 4);
    drawSwitchLine(renderer, module, 12, option, active, 4.0f, -3.0f, -12.0f, 13.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 13, option, active, 4.0f, 13.0f, 0.0f, -3.0f, 12.0f, 4);
    drawSwitchLine(renderer, module, 14, option, active, 3.0f, 15.0f, -12.0f, 27.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 15, option, active, 3.0f, 27.0f, 0.0f, 15.0f, 12.0f, 4);
}

/**
 * @brief 绘制发弹机构开关图标。
 */
void drawFeederSwitchIcon(UiRendererSrvc& renderer, const SwitchModule& module, GraphicOption option, UiColor active,
                          UiColor fill) {
    drawSwitchCircle(renderer, module, 10, option, fill, 3.0f, -13.0f, 0.0f, 10.0f, 4);
    drawSwitchCircle(renderer, module, 11, option, fill, 3.0f, 13.0f, 0.0f, 10.0f, 4);
    drawSwitchLine(renderer, module, 12, option, active, 4.0f, -3.0f, 0.0f, 3.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 13, option, active, 4.0f, 27.0f, 0.0f, 40.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 14, option, active, 3.0f, 34.0f, 6.0f, 40.0f, 0.0f, 4);
    drawSwitchLine(renderer, module, 15, option, active, 3.0f, 34.0f, -6.0f, 40.0f, 0.0f, 4);
}

/**
 * @brief 绘制底盘自转开关图标。
 */
void drawSpinSwitchIcon(UiRendererSrvc& renderer, const SwitchModule& module, GraphicOption option, UiColor active,
                        UiColor fill) {
    drawSwitchArc(renderer, module, 10, option, active, 4.0f, 0.0f, 0.0f, 18.0f, 35.0f, 300.0f, 4);
    drawSwitchLine(renderer, module, 11, option, active, 3.0f, 15.0f, 13.0f, 25.0f, 13.0f, 4);
    drawSwitchLine(renderer, module, 12, option, active, 3.0f, 25.0f, 13.0f, 20.0f, 3.0f, 4);
    drawSwitchLine(renderer, module, 13, option, fill, 2.0f, -12.0f, -4.0f, 12.0f, -4.0f, 4);
    drawSwitchLine(renderer, module, 14, option, fill, 2.0f, -8.0f, 8.0f, 8.0f, 8.0f, 4);
}

/**
 * @brief 按开关语义分发图标绘制。
 */
void drawSwitchIcon(UiRendererSrvc& renderer, const SwitchModule& module, GraphicOption option, UiColor active,
                    UiColor fill) {
    switch (module.glyph) {
        case SwitchGlyph::Capacitor:
            drawCapSwitchIcon(renderer, module, option, active, fill);
            break;
        case SwitchGlyph::Turbo:
            drawTurboSwitchIcon(renderer, module, option, active, fill);
            break;
        case SwitchGlyph::Feeder:
            drawFeederSwitchIcon(renderer, module, option, active, fill);
            break;
        case SwitchGlyph::Spin:
            drawSpinSwitchIcon(renderer, module, option, active, fill);
            break;
        default:
            break;
    }
}

/**
 * @brief 查询某个开关模块在当前输入下是否开启。
 */
bool switchModuleEnabled(const RefereeHudInput& input, const SwitchModule& module) {
    switch (module.glyph) {
        case SwitchGlyph::Capacitor:
            return input.capEnabled;
        case SwitchGlyph::Turbo:
            return input.turboEnabled;
        case SwitchGlyph::Feeder:
            return input.feederEnabled;
        case SwitchGlyph::Spin:
            return input.spinEnabled;
        default:
            return false;
    }
}

/* ------- wheel-leg helpers -------------------------------------------------*/

/**
 * @brief 将轮腿控件局部坐标转换为裁判 UI 坐标。
 */
PointF wheelLegLocal(float x, float y) {
    return {kWheelLegHipX + kWheelLegSide * kWheelLegScale * x, kWheelLegHipY + kWheelLegScale * y};
}

/**
 * @brief 将浮点值钳制到指定区间。
 */
float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

/**
 * @brief 根据大腿夹角和目标 hip-wheel distance 连续解算轮腿几何。
 */
WheelLegGeometry wheelLegGeometry(const WheelLegPose& pose, const WheelLegConfig& leg) {
    const float hipX     = kWheelLegHipX + leg.hipOffsetX * kWheelLegScale;
    const float distance = pose.hipWheelDistance * kWheelLegUpperLinkLength;
    const float minDistance = std::fabs(kWheelLegLowerLinkLength - kWheelLegUpperLinkLength) + 1.0f;
    const float maxDistance = kWheelLegLowerLinkLength + kWheelLegUpperLinkLength - 1.0f;
    const float solveDist   = clampFloat(distance, minDistance, maxDistance);

    WheelLegGeometry geometry{};
    geometry.hip   = {hipX, kWheelLegHipY};
    const float angleRad = pose.thighAngleDeg * RefereeHudSpec::kPi / 180.0f;
    geometry.knee        = {hipX + std::cos(angleRad) * kWheelLegUpperLinkLength * kWheelLegSide,
                            kWheelLegHipY - std::sin(angleRad) * kWheelLegUpperLinkLength};

    const float hipToKneeX = geometry.knee.x - geometry.hip.x;
    const float hipToKneeY = geometry.knee.y - geometry.hip.y;
    const float centerDist = std::sqrt(hipToKneeX * hipToKneeX + hipToKneeY * hipToKneeY);
    const float dirX       = hipToKneeX / centerDist;
    const float dirY       = hipToKneeY / centerDist;
    const float along =
        (solveDist * solveDist + centerDist * centerDist - kWheelLegLowerLinkLength * kWheelLegLowerLinkLength) /
        (2.0f * centerDist);
    const float height  = std::sqrt(std::fmax(0.0f, solveDist * solveDist - along * along));
    const PointF base   = {geometry.hip.x + dirX * along, geometry.hip.y + dirY * along};
    const PointF wheelA = {base.x - dirY * height, base.y + dirX * height};
    const PointF wheelB = {base.x + dirY * height, base.y - dirX * height};
    geometry.wheel      = wheelA.y < wheelB.y ? wheelA : wheelB;
    return geometry;
}

/**
 * @brief 从统一输入中取左腿姿态。
 */
WheelLegPose leftWheelLegPose(const RefereeHudInput& input) {
    return {input.leftLegThighAngleDeg, input.leftLegHipWheelDistance};
}

/**
 * @brief 从统一输入中取右腿姿态。
 */
WheelLegPose rightWheelLegPose(const RefereeHudInput& input) {
    return {input.rightLegThighAngleDeg, input.rightLegHipWheelDistance};
}

/**
 * @brief 判断连续腿部信号是否达到需要更新的变化量。
 */
bool legAngleSignalChanged(float current, float previous) { return std::fabs(current - previous) > 0.35f; }

bool legDistanceRatioSignalChanged(float current, float previous) { return std::fabs(current - previous) > 0.005f; }

} // namespace

/* ------- public draw interface --------------------------------------------*/

void RefereeHudUi::draw(UiRendererSrvc& renderer, const RefereeHudInput& input) {
    updateDynamicDirtyState(input);
    _input = input;
    drawStaticGraphics(renderer);
    drawDynamicGraphics(renderer);
}

void RefereeHudUi::drawStatic(UiRendererSrvc& renderer) { drawStaticGraphics(renderer); }

void RefereeHudUi::drawDynamic(UiRendererSrvc& renderer, const RefereeHudInput& input) {
    updateDynamicDirtyState(input);
    _input = input;
    drawDynamicGraphics(renderer);
}

/* ------- state tracking ----------------------------------------------------*/

/**
 * @brief 根据新输入更新动态图形脏标记。
 */
void RefereeHudUi::updateDynamicDirtyState(const RefereeHudInput& input) {
    const uint8_t legLengthState = normalizeLegLengthState(input.legLengthState);
    if (legLengthState != _lastLegLengthState ||
        legAngleSignalChanged(input.leftLegThighAngleDeg, _lastLeftLegThighAngleDeg) ||
        legDistanceRatioSignalChanged(input.leftLegHipWheelDistance, _lastLeftLegHipWheelDistance) ||
        legAngleSignalChanged(input.rightLegThighAngleDeg, _lastRightLegThighAngleDeg) ||
        legDistanceRatioSignalChanged(input.rightLegHipWheelDistance, _lastRightLegHipWheelDistance)) {
        _wheelLegDynamicDirty         = true;
        _lastLegLengthState           = legLengthState;
        _lastLeftLegThighAngleDeg     = input.leftLegThighAngleDeg;
        _lastLeftLegHipWheelDistance  = input.leftLegHipWheelDistance;
        _lastRightLegThighAngleDeg    = input.rightLegThighAngleDeg;
        _lastRightLegHipWheelDistance = input.rightLegHipWheelDistance;
    }

    const uint8_t aimModeState = normalizeAimModeState(input.aimModeState);
    if (aimModeState != _lastAimModeState) {
        _autoAimIconsDynamicDirty = true;
        _autoAimTrackDynamicDirty = true;
    }

    const uint8_t aimTargetState = normalizeAimTargetState(input.aimTargetState);
    if (aimTargetState != _lastAimTargetState) {
        _autoAimTrackDynamicDirty = true;
    }

    if (input.capEnabled != _lastCapSwitchState || input.turboEnabled != _lastTurboSwitchState ||
        input.feederEnabled != _lastFeederSwitchState || input.spinEnabled != _lastSpinSwitchState) {
        _switchDeckDynamicDirty = true;
    }
}

/**
 * @brief 清屏并复位所有静态/动态 ADD 状态。
 */
void RefereeHudUi::reset(UiRendererSrvc& renderer) {
    renderer.clearGraphic(GraphicDelMode::All);
    _capVoltageStaticDrawn        = false;
    _capVoltageDynamicDrawn       = false;
    _schoolEmblemStaticDrawn      = false;
    _wheelLegStaticDrawn          = false;
    _wheelLegDynamicDrawn         = false;
    _wheelLegDynamicDirty         = true;
    _bottomFrameStaticDrawn       = false;
    _switchDeckDynamicDrawn       = false;
    _switchDeckDynamicDirty       = true;
    _autoAimFrameStaticDrawn      = false;
    _autoAimTrackDynamicDrawn     = false;
    _autoAimTrackDynamicDirty     = true;
    _autoAimIconsDynamicDrawn     = false;
    _autoAimIconsDynamicDirty     = true;
    _lastLegLengthState           = 0xFF;
    _lastAimModeState             = 0xFF;
    _lastAimTargetState           = 0xFF;
    _lastCapSwitchState           = false;
    _lastTurboSwitchState         = false;
    _lastFeederSwitchState        = false;
    _lastSpinSwitchState          = false;
    _lastLeftLegThighAngleDeg     = -1000.0f;
    _lastLeftLegHipWheelDistance  = -1000.0f;
    _lastRightLegThighAngleDeg    = -1000.0f;
    _lastRightLegHipWheelDistance = -1000.0f;
}

/* ------- component draw implementation ------------------------------------*/

/**
 * @brief 绘制需要实时刷新的动态图形。
 */
void RefereeHudUi::drawDynamicGraphics(UiRendererSrvc& renderer) {
    const GraphicOption option      = _capVoltageDynamicDrawn ? GraphicOption::Update : GraphicOption::Add;
    const UiColor color             = capVoltageColor(_input);
    const uint16_t voltageAngleSpan = capGaugeVoltageAngleSpan(_input.capVoltage);
    const uint16_t visibleAngleSpan =
        voltageAngleSpan > kCapGaugeCenterGap ? voltageAngleSpan : static_cast<uint16_t>(kCapGaugeCenterGap + 1);

    {
        uint8_t name[3] = {kCapacitorGroup, kCapVoltageLeftArc, 0};
        auto graphic    = renderer.draw(name, option);
        graphic.layer(0)
            .color(color)
            .width(15)
            .start(roundToUiCoord(kCapGaugeCenterX), roundToUiCoord(kCapGaugeDynamicCenterY))
            .asArc(kCapGaugeCenterAngle + kCapGaugeCenterGap, kCapGaugeCenterAngle + visibleAngleSpan,
                   roundToUiCoord(kCapGaugeRadiusX), roundToUiCoord(kCapGaugeRadiusY));
    }
    {
        uint8_t name[3] = {kCapacitorGroup, kCapVoltageRightArc, 0};
        auto graphic    = renderer.draw(name, option);
        graphic.layer(0)
            .color(color)
            .width(15)
            .start(roundToUiCoord(kCapGaugeCenterX), roundToUiCoord(kCapGaugeDynamicCenterY))
            .asArc(kCapGaugeCenterAngle - visibleAngleSpan, kCapGaugeCenterAngle - kCapGaugeCenterGap,
                   roundToUiCoord(kCapGaugeRadiusX), roundToUiCoord(kCapGaugeRadiusY));
    }
    {
        uint8_t name[3] = {kCapacitorGroup, kCapVoltageValue, 0};
        auto graphic    = renderer.draw(name, option);
        graphic.layer(0)
            .color(color)
            .width(2)
            .start(roundToUiCoord(kCapVoltageTextX), roundToUiCoord(kCapVoltageTextY))
            .asFloat(capVoltageThreeSignificant(_input.capVoltage), 20);
    }

    _capVoltageDynamicDrawn = true;

    drawWheelLegDynamicGraphics(renderer);
    drawSwitchDeckDynamicGraphics(renderer);
    drawAutoAimTrackDynamicGraphics(renderer);
    drawAutoAimIconsDynamicGraphics(renderer);
}

/**
 * @brief 绘制所有静态图形。
 */
void RefereeHudUi::drawStaticGraphics(UiRendererSrvc& renderer) {
    if (!_capVoltageStaticDrawn) {
        {
            uint8_t name[3] = {kCapacitorGroup, kCapArcTrack, 0};
            auto graphic    = renderer.draw(name);
            graphic.layer(0)
                .color(UiColor::White)
                .width(5)
                .start(roundToUiCoord(kCapGaugeCenterX), roundToUiCoord(kCapGaugeStaticCenterY))
                .asArc(kCapGaugeStartAngle, kCapGaugeCenterAngle - kCapGaugeCenterGap, roundToUiCoord(kCapGaugeRadiusX),
                       roundToUiCoord(kCapGaugeRadiusY));
        }
        {
            uint8_t name[3] = {kCapacitorGroup, kCapArcTrack, 1};
            auto graphic    = renderer.draw(name);
            graphic.layer(0)
                .color(UiColor::White)
                .width(5)
                .start(roundToUiCoord(kCapGaugeCenterX), roundToUiCoord(kCapGaugeStaticCenterY))
                .asArc(kCapGaugeCenterAngle + kCapGaugeCenterGap, kCapGaugeEndAngle, roundToUiCoord(kCapGaugeRadiusX),
                       roundToUiCoord(kCapGaugeRadiusY));
        }

        for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(kCapVoltageThresholds) / sizeof(kCapVoltageThresholds[0]));
             ++i) {
            const float sweep     = static_cast<float>(kCapGaugeEndAngle - kCapGaugeCenterAngle - kCapGaugeCenterGap);
            const float angleSpan = kCapGaugeCenterGap + kCapVoltageThresholds[i] * sweep / kVoltageStage4;
            const PointF leftInner =
                capArcPoint(kCapGaugeCenterAngle + angleSpan, kCapGaugeTickInnerRadiusX, kCapGaugeTickInnerRadiusY);
            const PointF leftOuter = capArcPoint(kCapGaugeCenterAngle + angleSpan);
            const PointF rightInner =
                capArcPoint(kCapGaugeCenterAngle - angleSpan, kCapGaugeTickInnerRadiusX, kCapGaugeTickInnerRadiusY);
            const PointF rightOuter = capArcPoint(kCapGaugeCenterAngle - angleSpan);

            {
                uint8_t name[3] = {kCapacitorGroup, kCapThresholdTick, i};
                auto graphic    = renderer.draw(name);
                graphic.layer(0)
                    .color(UiColor::White)
                    .width(2)
                    .start(roundToUiCoord(leftInner.x), roundToUiCoord(leftInner.y))
                    .asLine(roundToUiCoord(leftOuter.x), roundToUiCoord(leftOuter.y));
            }
            {
                uint8_t name[3] = {kCapacitorGroup, kCapThresholdTick, static_cast<uint8_t>(i + 4)};
                auto graphic    = renderer.draw(name);
                graphic.layer(0)
                    .color(UiColor::White)
                    .width(2)
                    .start(roundToUiCoord(rightInner.x), roundToUiCoord(rightInner.y))
                    .asLine(roundToUiCoord(rightOuter.x), roundToUiCoord(rightOuter.y));
            }
        }

        _capVoltageStaticDrawn = true;
    }

    drawWheelLegStaticGraphics(renderer);
    drawSchoolEmblemStaticGraphics(renderer);
    drawBottomFrameStaticGraphics(renderer);
    drawAutoAimFrameStaticGraphics(renderer);
}

/**
 * @brief 绘制校徽静态图形。
 */
void RefereeHudUi::drawSchoolEmblemStaticGraphics(UiRendererSrvc& renderer) {
    if (_schoolEmblemStaticDrawn)
        return;

    for (const auto& arc : kSchoolEmblemArcs) {
        auto name    = graphicName(kSchoolEmblemGroup, arc.id, 0);
        auto graphic = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(arc.color)
            .width(arc.width)
            .start(roundToUiCoord(kSchoolEmblemCenterX + static_cast<float>(arc.offsetX)),
                   roundToUiCoord(kSchoolEmblemCenterY + static_cast<float>(arc.offsetY)))
            .asArc(arc.startAngle, arc.endAngle, arc.radiusX, arc.radiusY);
    }
    for (const auto& lineCfg : kSchoolEmblemLines) {
        auto name    = graphicName(kSchoolEmblemGroup, lineCfg.id, 0);
        auto graphic = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(UiColor::White)
            .width(lineCfg.width)
            .start(roundToUiCoord(kSchoolEmblemCenterX + static_cast<float>(lineCfg.startOffsetX)),
                   roundToUiCoord(kSchoolEmblemCenterY + static_cast<float>(lineCfg.startOffsetY)))
            .asLine(roundToUiCoord(kSchoolEmblemCenterX + static_cast<float>(lineCfg.endOffsetX)),
                    roundToUiCoord(kSchoolEmblemCenterY + static_cast<float>(lineCfg.endOffsetY)));
    }

    _schoolEmblemStaticDrawn = true;
}

/**
 * @brief 绘制底部仪表台开放式边框。
 */
void RefereeHudUi::drawBottomFrameStaticGraphics(UiRendererSrvc& renderer) {
    if (_bottomFrameStaticDrawn)
        return;

    for (const auto& lineCfg : kBottomFrameLines) {
        auto name    = graphicName(kBottomFrameGroup, 0, lineCfg.id);
        auto graphic = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(lineCfg.color)
            .width(lineCfg.width)
            .start(roundToUiCoord(lineCfg.x1), roundToUiCoord(lineCfg.y1))
            .asLine(roundToUiCoord(lineCfg.x2), roundToUiCoord(lineCfg.y2));
    }

    _bottomFrameStaticDrawn = true;
}

/**
 * @brief 绘制自瞄状态轨道的静态角标。
 */
void RefereeHudUi::drawAutoAimFrameStaticGraphics(UiRendererSrvc& renderer) {
    if (_autoAimFrameStaticDrawn)
        return;

    for (const auto& lineCfg : kAutoAimFrameLines) {
        auto name    = graphicName(kAutoAimGroup, kAutoAimFrame, lineCfg.id);
        auto graphic = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(lineCfg.color)
            .width(lineCfg.width)
            .start(roundToUiCoord(lineCfg.x1), roundToUiCoord(lineCfg.y1))
            .asLine(roundToUiCoord(lineCfg.x2), roundToUiCoord(lineCfg.y2));
    }

    _autoAimFrameStaticDrawn = true;
}

/**
 * @brief 绘制底部四开关动态图形。
 */
void RefereeHudUi::drawSwitchDeckDynamicGraphics(UiRendererSrvc& renderer) {
    if (_switchDeckDynamicDrawn && !_switchDeckDynamicDirty)
        return;

    const GraphicOption option = _switchDeckDynamicDrawn ? GraphicOption::Update : GraphicOption::Add;

    for (const auto& module : kSwitchModules) {
        const bool on         = switchModuleEnabled(_input, module);
        const UiColor active  = on ? UiColor::Main : UiColor::White;
        const UiColor rail    = active;
        const UiColor fill    = UiColor::White;
        const float railWidth = on ? 3.0f : 2.0f;

        drawSwitchLine(renderer, module, 0, option, rail, railWidth, -58.0f, -22.0f, -42.0f, 22.0f, 2);
        drawSwitchLine(renderer, module, 1, option, rail, railWidth, -42.0f, 22.0f, -12.0f, 22.0f, 2);
        drawSwitchLine(renderer, module, 2, option, rail, railWidth, 12.0f, 22.0f, 42.0f, 22.0f, 2);
        drawSwitchLine(renderer, module, 3, option, rail, railWidth, 42.0f, 22.0f, 58.0f, -22.0f, 2);
        drawSwitchLine(renderer, module, 4, option, UiColor::White, 1.0f, -58.0f, -22.0f, -24.0f, -22.0f, 2);
        drawSwitchLine(renderer, module, 5, option, UiColor::White, 1.0f, 24.0f, -22.0f, 58.0f, -22.0f, 2);
        drawSwitchCircle(renderer, module, 6, option, active, on ? 4.0f : 2.0f, 0.0f, 0.0f, on ? 24.0f : 20.0f, 3);
        drawSwitchCircle(renderer, module, 7, option, fill, 2.0f, 0.0f, 0.0f, 5.0f, 4);
        drawSwitchIcon(renderer, module, option, active, fill);
    }

    _lastCapSwitchState     = _input.capEnabled;
    _lastTurboSwitchState   = _input.turboEnabled;
    _lastFeederSwitchState  = _input.feederEnabled;
    _lastSpinSwitchState    = _input.spinEnabled;
    _switchDeckDynamicDrawn = true;
    _switchDeckDynamicDirty = false;
}

/**
 * @brief 绘制自瞄目标状态轨道。
 */
void RefereeHudUi::drawAutoAimTrackDynamicGraphics(UiRendererSrvc& renderer) {
    if (_autoAimTrackDynamicDrawn && !_autoAimTrackDynamicDirty)
        return;

    const GraphicOption option = _autoAimTrackDynamicDrawn ? GraphicOption::Update : GraphicOption::Add;
    const UiColor trackColor   = aimTargetColor(_input.aimTargetState);
    const uint8_t activeState  = normalizeAimModeState(_input.aimModeState);

    for (const auto& lineCfg : kAutoAimTrackLines) {
        auto name    = graphicName(kAutoAimGroup, kAutoAimTrack, lineCfg.id);
        auto graphic = renderer.draw(name.bytes, option);
        graphic.layer(0)
            .color(trackColor)
            .width(lineCfg.width)
            .start(roundToUiCoord(lineCfg.x1), roundToUiCoord(lineCfg.y1))
            .asLine(roundToUiCoord(lineCfg.x2), roundToUiCoord(lineCfg.y2));
    }

    for (const auto& tick : kAutoAimTrackTicks) {
        const bool selected = tick.state == activeState;
        auto name           = graphicName(kAutoAimGroup, kAutoAimTrack, static_cast<uint8_t>(10U + tick.id));
        auto graphic        = renderer.draw(name.bytes, option);
        graphic.layer(0)
            .color(selected ? UiColor::Main : UiColor::White)
            .width(selected ? 3 : 1)
            .start(roundToUiCoord(tick.x1), roundToUiCoord(tick.y))
            .asLine(roundToUiCoord(tick.x2), roundToUiCoord(tick.y));
    }

    _lastAimTargetState       = normalizeAimTargetState(_input.aimTargetState);
    _autoAimTrackDynamicDrawn = true;
    _autoAimTrackDynamicDirty = false;
}

/**
 * @brief 绘制四个自瞄模式图标。
 */
void RefereeHudUi::drawAutoAimIconsDynamicGraphics(UiRendererSrvc& renderer) {
    if (_autoAimIconsDynamicDrawn && !_autoAimIconsDynamicDirty)
        return;

    const uint8_t activeState = normalizeAimModeState(_input.aimModeState);

    if (!_autoAimIconsDynamicDrawn) {
        for (const auto& icon : kAutoAimIcons) {
            drawAutoAimIcon(renderer, icon, GraphicOption::Add, activeState);
        }
    } else {
        for (const auto& icon : kAutoAimIcons) {
            if (icon.state == activeState || icon.state == _lastAimModeState) {
                drawAutoAimIcon(renderer, icon, GraphicOption::Update, activeState);
            }
        }
    }

    _lastAimModeState         = activeState;
    _autoAimIconsDynamicDrawn = true;
    _autoAimIconsDynamicDirty = false;
}

/**
 * @brief 绘制轮腿车体和三档腿长刻度的静态图形。
 */
void RefereeHudUi::drawWheelLegStaticGraphics(UiRendererSrvc& renderer) {
    if (_wheelLegStaticDrawn)
        return;

    for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(kWheelLegHullLines) / sizeof(kWheelLegHullLines[0])); ++i) {
        const auto& hullLine = kWheelLegHullLines[i];
        const PointF start   = wheelLegLocal(hullLine.x1, hullLine.y1);
        const PointF end     = wheelLegLocal(hullLine.x2, hullLine.y2);
        auto name            = graphicName(kWheelLegGroup, kWheelLegBodyHull, i);
        auto graphic         = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(hullLine.color)
            .width(2)
            .start(roundToUiCoord(start.x), roundToUiCoord(start.y))
            .asLine(roundToUiCoord(end.x), roundToUiCoord(end.y));
    }

    {
        const PointF start = wheelLegLocal(-54.0f, 18.0f);
        const PointF end   = wheelLegLocal(-6.0f, 18.0f);
        auto name          = graphicName(kWheelLegGroup, kWheelLegBodyRef, 0);
        auto graphic       = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(UiColor::Cyan)
            .width(1)
            .start(roundToUiCoord(start.x), roundToUiCoord(start.y))
            .asLine(roundToUiCoord(end.x), roundToUiCoord(end.y));
    }
    {
        const PointF start = wheelLegLocal(6.0f, 18.0f);
        const PointF end   = wheelLegLocal(48.0f, 18.0f);
        auto name          = graphicName(kWheelLegGroup, kWheelLegBodyRef, 1);
        auto graphic       = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(UiColor::Orange)
            .width(1)
            .start(roundToUiCoord(start.x), roundToUiCoord(start.y))
            .asLine(roundToUiCoord(end.x), roundToUiCoord(end.y));
    }
    {
        const PointF center = wheelLegLocal(0.0f, 0.0f);
        auto name           = graphicName(kWheelLegGroup, kWheelLegBodyRef, 2);
        auto graphic        = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(UiColor::White)
            .width(2)
            .start(roundToUiCoord(center.x), roundToUiCoord(center.y))
            .asCircle(4);
    }

    for (uint8_t i = 0; i < 3; ++i) {
        const float y      = kWheelLegHipY - kWheelLegDistanceMarkRatios[i] * kWheelLegUpperLinkLength;
        const float innerX = kWheelLegHipX - 28.0f * kWheelLegScale;
        const float outerX = kWheelLegHipX - 48.0f * kWheelLegScale;
        auto name          = graphicName(kWheelLegGroup, kWheelLegTicks, i);
        auto graphic       = renderer.draw(name.bytes);
        graphic.layer(0)
            .color(i == 1 ? UiColor::Cyan : UiColor::White)
            .width(1)
            .start(roundToUiCoord(innerX), roundToUiCoord(y))
            .asLine(roundToUiCoord(outerX), roundToUiCoord(y));
    }

    _wheelLegStaticDrawn = true;
}

/**
 * @brief 绘制左右腿动态姿态。
 */
void RefereeHudUi::drawWheelLegDynamicGraphics(UiRendererSrvc& renderer) {
    if (_wheelLegDynamicDrawn && !_wheelLegDynamicDirty)
        return;

    const GraphicOption option = _wheelLegDynamicDrawn ? GraphicOption::Update : GraphicOption::Add;
    const WheelLegPose poses[] = {
        leftWheelLegPose(_input),
        rightWheelLegPose(_input),
    };

    for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(kWheelLegConfigs) / sizeof(kWheelLegConfigs[0])); ++i) {
        const auto& leg                 = kWheelLegConfigs[i];
        const WheelLegGeometry geometry = wheelLegGeometry(poses[i], leg);
        const UiColor legColor          = leg.color;
        {
            auto name    = graphicName(kWheelLegGroup, kWheelLegUpperLink, leg.id);
            auto graphic = renderer.draw(name.bytes, option);
            graphic.layer(1)
                .color(legColor)
                .width(5)
                .start(roundToUiCoord(geometry.hip.x), roundToUiCoord(geometry.hip.y))
                .asLine(roundToUiCoord(geometry.knee.x), roundToUiCoord(geometry.knee.y));
        }
        {
            auto name    = graphicName(kWheelLegGroup, kWheelLegLowerLink, leg.id);
            auto graphic = renderer.draw(name.bytes, option);
            graphic.layer(1)
                .color(legColor)
                .width(5)
                .start(roundToUiCoord(geometry.knee.x), roundToUiCoord(geometry.knee.y))
                .asLine(roundToUiCoord(geometry.wheel.x), roundToUiCoord(geometry.wheel.y));
        }
        {
            auto name    = graphicName(kWheelLegGroup, kWheelLegWheel, leg.id);
            auto graphic = renderer.draw(name.bytes, option);
            graphic.layer(1)
                .color(legColor)
                .width(3)
                .start(roundToUiCoord(geometry.wheel.x), roundToUiCoord(geometry.wheel.y))
                .asCircle(9);
        }
    }

    _wheelLegDynamicDrawn = true;
    _wheelLegDynamicDirty = false;
}
