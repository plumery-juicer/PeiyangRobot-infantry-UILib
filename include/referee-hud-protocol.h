/**
 *******************************************************************************
 * @file    referee-hud-protocol.h
 * @brief   RoboMaster 裁判系统 UI 交互协议的轻量类型定义。
 *******************************************************************************
 * @attention
 * 本文件只保留 HUD 库实际需要的协议字段。结构体必须保持紧凑布局，
 * 修改字段顺序或位宽会直接影响发给裁判系统的数据包格式。
 *******************************************************************************
 */

#ifndef REFEREE_HUD_PROTOCOL_H
#define REFEREE_HUD_PROTOCOL_H

#include <cstdint>

/**
 * @brief 裁判系统交互子命令 ID。
 */
enum class RMSubCmdId : uint16_t {
    DeleteLayer = 0x0100, ///< 删除图层或图形。
    RenderOne = 0x0101,   ///< 一次发送 1 个图形。
    RenderTwo = 0x0102,   ///< 一次发送 2 个图形。
    RenderFive = 0x0103,  ///< 一次发送 5 个图形。
    RenderSeven = 0x0104, ///< 一次发送 7 个图形。
    RenderChar = 0x0110,  ///< 字符串绘制，当前 HUD 业务未使用。
};

/**
 * @brief 裁判系统图形类型编码。
 */
enum class GraphicType : uint8_t {
    Line = 0,      ///< 直线。
    Rectangle = 1, ///< 矩形。
    Circle = 2,    ///< 圆。
    Ellipse = 3,   ///< 椭圆。
    Arc = 4,       ///< 圆弧或椭圆弧。
    Float = 5,     ///< 浮点数字。
    Int = 6,       ///< 整数数字。
    String = 7,    ///< 字符串。
};

/**
 * @brief 裁判系统图形删除模式。
 */
enum class GraphicDelMode : uint8_t {
    Null = 0,  ///< 不删除。
    Layer = 1, ///< 删除指定图层。
    All = 2,   ///< 删除客户端全部图形。
    Name = 3,  ///< 删除指定图形名。
};

/**
 * @brief 裁判系统图形操作类型。
 */
enum class GraphicOption : uint8_t {
    Null = 0,   ///< 空操作。
    Add = 1,    ///< 新增图形；所有图形必须先 Add 再 Update。
    Update = 2, ///< 更新同名图形。
    Delete = 3, ///< 删除图形。
};

/**
 * @brief 裁判系统 UI 颜色编码。
 *
 * Main 会根据本方阵营在客户端显示为红或蓝，其余颜色为固定色。
 */
enum class UiColor : uint8_t {
    Main = 0,   ///< 阵营色，红/蓝自适应。
    Yellow = 1, ///< 黄色。
    Green = 2,  ///< 绿色。
    Orange = 3, ///< 橙色。
    Purple = 4, ///< 紫色。
    Pink = 5,   ///< 粉色。
    Cyan = 6,   ///< 青色。
    Black = 7,  ///< 黑色。
    White = 8,  ///< 白色。
    Del = 9,    ///< 删除相关特殊颜色值。
};

#pragma pack(push, 1)

/**
 * @brief 单个裁判系统 UI 图形 payload。
 *
 * 所有坐标和尺寸字段都使用裁判系统的整数 UI 坐标。param1/param2/param3
 * 会按图形类型复用，例如圆弧角度、圆半径或数字值分片。
 */
struct RMInteractionFigurePayload {
    uint8_t graphicName[3]; ///< 三字节图形名，同名图形用 Update 修改。
    uint32_t opt : 3;       ///< GraphicOption。
    uint32_t type : 3;      ///< GraphicType。
    uint32_t layer : 4;     ///< 图层号。
    uint32_t color : 4;     ///< UiColor。
    uint32_t param1 : 9;    ///< 类型相关参数 1。
    uint32_t param2 : 9;    ///< 类型相关参数 2。
    uint32_t width : 10;    ///< 线宽或字符线宽。
    uint32_t startX : 11;   ///< 起点/中心点 x。
    uint32_t startY : 11;   ///< 起点/中心点 y。
    uint32_t param3 : 10;   ///< 类型相关参数 3。
    uint32_t endX : 11;     ///< 终点 x、半轴 x 或数字值分片。
    uint32_t endY : 11;     ///< 终点 y、半轴 y 或数字值分片。
};

/**
 * @brief 删除图层/全部图形 payload。
 */
struct RMInteractionLayerDeletePayload {
    GraphicDelMode delMode; ///< 删除模式。
    uint8_t layer;          ///< 删除指定图层时使用；删除全部时为 0。
};

/**
 * @brief 裁判系统通用帧头。
 */
struct RmFrameHeader {
    uint8_t sof;          ///< 固定帧头 0xA5。
    uint16_t dataLength;  ///< cmdId 后数据段长度。
    uint8_t seq;          ///< 帧序号。
    uint8_t crc8;         ///< 帧头 CRC8。
};

/**
 * @brief 裁判系统交互数据头。
 */
struct RmInteractiveHeader {
    uint16_t subCmdId;   ///< RMSubCmdId。
    uint16_t senderId;   ///< 发送机器人 ID。
    uint16_t receiverId; ///< 接收客户端 ID，HUD 库按 senderId + 0x0100 生成。
};

#pragma pack(pop)

static_assert(sizeof(RMInteractionFigurePayload) == 15, "RMInteractionFigurePayload must be 15 bytes");
static_assert(sizeof(RMInteractionLayerDeletePayload) == 2, "RMInteractionLayerDeletePayload must be 2 bytes");
static_assert(sizeof(RmFrameHeader) == 5, "RmFrameHeader must be 5 bytes");
static_assert(sizeof(RmInteractiveHeader) == 6, "RmInteractiveHeader must be 6 bytes");

/**
 * @brief 直接绘制 API 共用的基础图形属性。
 */
struct GraphicProperties {
    uint8_t name[3];       ///< 三字节图形名。
    GraphicOption action;  ///< Add/Update/Delete。
    uint8_t layer;         ///< 图层号。
    UiColor color;         ///< 图形颜色。
    uint16_t lineWidth;    ///< 线宽或字符线宽。
    uint16_t startX;       ///< 起点/中心点 x。
    uint16_t startY;       ///< 起点/中心点 y。
};

#endif
