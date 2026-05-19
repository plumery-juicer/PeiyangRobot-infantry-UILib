#ifndef REFEREE_HUD_PROTOCOL_H
#define REFEREE_HUD_PROTOCOL_H

#include <cstdint>

enum class RMSubCmdId : uint16_t {
    DeleteLayer = 0x0100,
    RenderOne = 0x0101,
    RenderTwo = 0x0102,
    RenderFive = 0x0103,
    RenderSeven = 0x0104,
    RenderChar = 0x0110,
};

enum class GraphicType : uint8_t {
    Line = 0,
    Rectangle = 1,
    Circle = 2,
    Ellipse = 3,
    Arc = 4,
    Float = 5,
    Int = 6,
    String = 7,
};

enum class GraphicDelMode : uint8_t {
    Null = 0,
    Layer = 1,
    All = 2,
    Name = 3,
};

enum class GraphicOption : uint8_t {
    Null = 0,
    Add = 1,
    Update = 2,
    Delete = 3,
};

enum class UiColor : uint8_t {
    Main = 0,
    Yellow = 1,
    Green = 2,
    Orange = 3,
    Purple = 4,
    Pink = 5,
    Cyan = 6,
    Black = 7,
    White = 8,
    Del = 9,
};

#pragma pack(push, 1)

struct RMInteractionFigurePayload {
    uint8_t graphicName[3];
    uint32_t opt : 3;
    uint32_t type : 3;
    uint32_t layer : 4;
    uint32_t color : 4;
    uint32_t param1 : 9;
    uint32_t param2 : 9;
    uint32_t width : 10;
    uint32_t startX : 11;
    uint32_t startY : 11;
    uint32_t param3 : 10;
    uint32_t endX : 11;
    uint32_t endY : 11;
};

struct RMInteractionLayerDeletePayload {
    GraphicDelMode delMode;
    uint8_t layer;
};

struct RmFrameHeader {
    uint8_t sof;
    uint16_t dataLength;
    uint8_t seq;
    uint8_t crc8;
};

struct RmInteractiveHeader {
    uint16_t subCmdId;
    uint16_t senderId;
    uint16_t receiverId;
};

#pragma pack(pop)

static_assert(sizeof(RMInteractionFigurePayload) == 15, "RMInteractionFigurePayload must be 15 bytes");
static_assert(sizeof(RMInteractionLayerDeletePayload) == 2, "RMInteractionLayerDeletePayload must be 2 bytes");
static_assert(sizeof(RmFrameHeader) == 5, "RmFrameHeader must be 5 bytes");
static_assert(sizeof(RmInteractiveHeader) == 6, "RmInteractiveHeader must be 6 bytes");

struct GraphicProperties {
    uint8_t name[3];
    GraphicOption action;
    uint8_t layer;
    UiColor color;
    uint16_t lineWidth;
    uint16_t startX;
    uint16_t startY;
};

#endif
