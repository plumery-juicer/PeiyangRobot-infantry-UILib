/**
 *******************************************************************************
 * @file    referee-hud-ui.h
 * @brief   裁判系统 HUD 业务 UI 组件接口
 *******************************************************************************
 * @attention
 * 本文件只描述业务 UI 的输入快照和绘制组件，不直接读取底盘/云台业务数据。
 * 调用者应在主体工程中把 Blackboard、通信包或仿真信号转换为 RefereeHudInput，
 * 再交给 RefereeHudUi 使用 UiRendererSrvc 输出到裁判系统。
 *******************************************************************************
 */

#ifndef INFANTRY_REFEREE_HUD_UI_H
#define INFANTRY_REFEREE_HUD_UI_H

/* ------- include -----------------------------------------------------------*/
#include "ui-renderer-srvc.h"

#include <cmath>
#include <cstdint>

/* ------- enum --------------------------------------------------------------*/

/**
 * @brief 自瞄目标识别/开火状态。
 */
enum class RefereeHudAimTarget : uint8_t {
    None   = 0, ///< 未识别到目标。
    Locked = 1, ///< 已识别并锁定目标。
    Fire   = 2, ///< 已发起开火指令。
};

/* ------- data interface ----------------------------------------------------*/

/**
 * @brief HUD 绘制使用的统一输入快照。
 *
 * 所有 UI 业务输入都先汇总为该结构，再由 RefereeHudUi 计算静态/动态图形。
 * 这样 UI 组件可以脱离具体工程的数据来源，方便硬件、仿真和 HTML 预览复用同一套语义。
 */
struct RefereeHudInput {
    float capVoltage = 0.0f; ///< 超级电容电压，单位 V。
    bool capEnabled = false; ///< 电容开关状态。
    bool capError = false; ///< 电容低压或错误状态。
    bool resetRequested = false; ///< 请求清屏并重新 ADD 所有图形。
    bool turboEnabled = false; ///< 极速模式开关状态。
    bool feederEnabled = false; ///< 发弹机构开关状态。
    bool spinEnabled = false; ///< 底盘自转模式开关状态。
    uint8_t legLengthState = 0; ///< 兼容三档腿长状态输入，0/1/2 分别对应短/中/长。
    uint8_t aimModeState = 0; ///< 自瞄模式，0=车辆，1=前哨站，2=能量机关 A，3=能量机关 B。
    uint8_t aimTargetState = static_cast<uint8_t>(RefereeHudAimTarget::None); ///< 自瞄目标状态。
    float leftLegThighAngleDeg = 41.0f; ///< 左腿大腿相对车体水平基准向下的夹角，单位 deg。
    float leftLegHipWheelDistance = 135.0f / 105.0f; ///< 左腿胯关节到轮心距离 / 大腿长度。
    float rightLegThighAngleDeg = 41.0f; ///< 右腿大腿相对车体水平基准向下的夹角，单位 deg。
    float rightLegHipWheelDistance = 135.0f / 105.0f; ///< 右腿胯关节到轮心距离 / 大腿长度。
};

/**
 * @brief HUD 输入源抽象接口。
 *
 * 主体工程可以实现该接口，将真实通信数据、Blackboard 数据或仿真信号转换为 RefereeHudInput。
 */
class RefereeHudInputSource {
  public:
    virtual ~RefereeHudInputSource() = default;

    /**
     * @brief 采样一帧 HUD 输入。
     * @param dt 距离上一帧的时间，单位 s。
     * @return 当前 UI 输入快照。
     */
    virtual RefereeHudInput sample(float dt) = 0;
};

/* ------- specification helpers --------------------------------------------*/

/**
 * @brief HUD 组件的公开规格参数和输入归一化工具。
 */
namespace RefereeHudSpec {
constexpr float kPeriodSeconds = 0.050f; ///< 默认 UI 更新周期，单位 s。

/// 电容电压颜色分段阈值，单位 V。
constexpr float kVoltageStage1 = 7.0f;
constexpr float kVoltageStage2 = 15.0f;
constexpr float kVoltageStage3 = 22.0f;
constexpr float kVoltageStage4 = 26.0f;

/// 轮腿机构原始连杆长度。105:125 对应真实大腿/小腿 210:250 的比例。
constexpr float kWheelLegUpperLinkRaw = 105.0f;
constexpr float kWheelLegLowerLinkRaw = 125.0f;
/// 轮心到胯关节距离与大腿长度的三档参考比例。
constexpr float kWheelLegDistanceMinRatio = 95.0f / kWheelLegUpperLinkRaw;
constexpr float kWheelLegDistanceMidRatio = 135.0f / kWheelLegUpperLinkRaw;
constexpr float kWheelLegDistanceMaxRatio = 175.0f / kWheelLegUpperLinkRaw;
constexpr float kPi = 3.14159265358979323846f;

/**
 * @brief 将浮点值钳制到指定区间。
 */
inline float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

inline uint8_t normalizeLegLengthState(uint8_t state) { return static_cast<uint8_t>(state % 3); }

inline uint8_t normalizeAimModeState(uint8_t state) { return static_cast<uint8_t>(state % 4); }

inline uint8_t normalizeAimTargetState(uint8_t state) {
    const auto fire = static_cast<uint8_t>(RefereeHudAimTarget::Fire);
    if (state >= fire)
        return fire;
    return state;
}

/**
 * @brief 根据胯关节到轮心距离反解腿部参考角。
 *
 * 该函数只用于从三档腿长输入生成稳定的测试/默认姿态。连续绘制时应优先直接传入
 * thigh angle 和 hip-wheel distance ratio，不通过缩放连杆伪造腿长。
 *
 * @param distanceRatio 胯关节到轮心距离 / 大腿长度。
 * @return 大腿相对车体水平基准向下的参考角，单位 deg。
 */
inline float wheelLegAngleForDistanceRatio(float distanceRatio) {
    const float minDistance = std::fabs(kWheelLegLowerLinkRaw - kWheelLegUpperLinkRaw) + 1.0f;
    const float maxDistance = kWheelLegLowerLinkRaw + kWheelLegUpperLinkRaw - 1.0f;
    const float distance = clampFloat(distanceRatio * kWheelLegUpperLinkRaw, minDistance, maxDistance);
    const float along = (distance * distance + kWheelLegUpperLinkRaw * kWheelLegUpperLinkRaw -
                         kWheelLegLowerLinkRaw * kWheelLegLowerLinkRaw) /
                        (2.0f * distance);
    const float alongRatio = clampFloat(along / kWheelLegUpperLinkRaw, -1.0f, 1.0f);
    return 90.0f - std::acos(alongRatio) * 180.0f / kPi;
}

inline float wheelLegAngleForDistance(float distanceRatio) { return wheelLegAngleForDistanceRatio(distanceRatio); }

/**
 * @brief 根据三档腿长状态填充左右腿姿态。
 * @param input 需要补全腿部姿态字段的输入快照。
 */
inline void fillDualLegPoseFromState(RefereeHudInput& input) {
    constexpr float marks[] = {
        kWheelLegDistanceMinRatio,
        kWheelLegDistanceMidRatio,
        kWheelLegDistanceMaxRatio,
    };
    const float distanceRatio = marks[normalizeLegLengthState(input.legLengthState)];
    const float angle = wheelLegAngleForDistanceRatio(distanceRatio);
    input.leftLegThighAngleDeg = angle;
    input.leftLegHipWheelDistance = distanceRatio;
    input.rightLegThighAngleDeg = angle;
    input.rightLegHipWheelDistance = distanceRatio;
}
} // namespace RefereeHudSpec

/* ------- class prototypes --------------------------------------------------*/

/**
 * @brief 裁判系统 HUD 业务绘制组件。
 *
 * RefereeHudUi 负责根据 RefereeHudInput 生成裁判系统图形，不负责发送队列、串口 DMA
 * 或 1/2/5/7 打包。这些渲染细节仍由 UiRendererSrvc 处理。
 */
class RefereeHudUi {
  public:
    static constexpr float kPeriodSeconds = RefereeHudSpec::kPeriodSeconds;

    /**
     * @brief 清除客户端 UI 并重置所有 ADD/UPDATE 状态。
     * @param renderer 裁判 UI 渲染服务。
     */
    void reset(UiRendererSrvc& renderer);

    /**
     * @brief 绘制完整 HUD。
     *
     * 首次调用会 ADD 静态图形和动态图形，后续调用只 UPDATE 变化的动态图形。
     *
     * @param renderer 裁判 UI 渲染服务。
     * @param input 当前 HUD 输入快照。
     */
    void draw(UiRendererSrvc& renderer, const RefereeHudInput& input);

    /**
     * @brief 只绘制静态图形。
     */
    void drawStatic(UiRendererSrvc& renderer);

    /**
     * @brief 只绘制动态/状态图形。
     */
    void drawDynamic(UiRendererSrvc& renderer, const RefereeHudInput& input);

  private:
    /* ------- state cache ----------------------------------------------------*/

    RefereeHudInput _input {};

    /* 静态图形 ADD 状态。 */
    bool _capVoltageStaticDrawn = false;
    bool _schoolEmblemStaticDrawn = false;
    bool _wheelLegStaticDrawn = false;
    bool _bottomFrameStaticDrawn = false;
    bool _autoAimFrameStaticDrawn = false;

    /* 动态图形 ADD/UPDATE 与脏标记状态。 */
    bool _capVoltageDynamicDrawn = false;
    bool _wheelLegDynamicDrawn = false;
    bool _wheelLegDynamicDirty = true;
    bool _switchDeckDynamicDrawn = false;
    bool _switchDeckDynamicDirty = true;
    bool _autoAimTrackDynamicDrawn = false;
    bool _autoAimTrackDynamicDirty = true;
    bool _autoAimIconsDynamicDrawn = false;
    bool _autoAimIconsDynamicDirty = true;

    /* 上一帧输入缓存，用于减少不必要的 UPDATE 图形。 */
    uint8_t _lastLegLengthState = 0xFF;
    uint8_t _lastAimModeState = 0xFF;
    uint8_t _lastAimTargetState = 0xFF;
    bool _lastCapSwitchState = false;
    bool _lastTurboSwitchState = false;
    bool _lastFeederSwitchState = false;
    bool _lastSpinSwitchState = false;
    float _lastLeftLegThighAngleDeg = -1000.0f;
    float _lastLeftLegHipWheelDistance = -1000.0f;
    float _lastRightLegThighAngleDeg = -1000.0f;
    float _lastRightLegHipWheelDistance = -1000.0f;

    /* ------- internal draw helpers ----------------------------------------*/

    void updateDynamicDirtyState(const RefereeHudInput& input);
    void drawDynamicGraphics(UiRendererSrvc& renderer);
    void drawStaticGraphics(UiRendererSrvc& renderer);
    void drawSchoolEmblemStaticGraphics(UiRendererSrvc& renderer);
    void drawBottomFrameStaticGraphics(UiRendererSrvc& renderer);
    void drawSwitchDeckDynamicGraphics(UiRendererSrvc& renderer);
    void drawAutoAimFrameStaticGraphics(UiRendererSrvc& renderer);
    void drawAutoAimTrackDynamicGraphics(UiRendererSrvc& renderer);
    void drawAutoAimIconsDynamicGraphics(UiRendererSrvc& renderer);
    void drawWheelLegDynamicGraphics(UiRendererSrvc& renderer);
    void drawWheelLegStaticGraphics(UiRendererSrvc& renderer);
};

#endif
