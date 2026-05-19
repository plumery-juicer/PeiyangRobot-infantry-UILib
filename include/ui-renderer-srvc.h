#ifndef REFEREE_HUD_UI_RENDERER_SRVC_H
#define REFEREE_HUD_UI_RENDERER_SRVC_H

#ifndef REFEREE_HUD_FREERTOS_HEADER
#define REFEREE_HUD_FREERTOS_HEADER "FreeRTOS.h"
#endif

#ifndef REFEREE_HUD_FREERTOS_QUEUE_HEADER
#define REFEREE_HUD_FREERTOS_QUEUE_HEADER "queue.h"
#endif

#include REFEREE_HUD_FREERTOS_HEADER
#include REFEREE_HUD_FREERTOS_QUEUE_HEADER
#include "referee-hud-protocol.h"

#include <cstdint>
#include <cstring>
#include <utility>

class UiRendererSrvc {
  public:
    using PacketTransmitCallback = bool (*)(const uint8_t* data, uint16_t length, void* context);

    struct Config {
        PacketTransmitCallback packetTransmit = nullptr;
        void* packetTransmitContext = nullptr;
        uint16_t senderId = 3;
        uint16_t queueDepth = 35;
        uint8_t* txBuffer = nullptr;
        uint16_t txBufferSize = 0;
    };

    explicit UiRendererSrvc(const Config& config);

    bool init();
    void run();

    class GraphicProxy {
      public:
        GraphicProxy(UiRendererSrvc& renderer, const uint8_t name[3], GraphicOption opt = GraphicOption::Add);
        GraphicProxy(const GraphicProxy&) = delete;
        GraphicProxy& operator=(const GraphicProxy&) = delete;
        GraphicProxy(GraphicProxy&& other) noexcept;
        ~GraphicProxy();

        GraphicProxy& color(UiColor color);
        GraphicProxy& layer(uint8_t layer);
        GraphicProxy& width(uint32_t width);
        GraphicProxy& start(uint32_t x, uint32_t y);

        GraphicProxy& asLine(uint16_t endX, uint16_t endY);
        GraphicProxy& asRectangle(uint16_t endX, uint16_t endY);
        GraphicProxy& asCircle(uint16_t radius);
        GraphicProxy& asEllipse(uint16_t xSemiAxis, uint16_t ySemiAxis);
        GraphicProxy& asArc(uint16_t startAngle, uint16_t endAngle, uint16_t xSemiAxis, uint16_t ySemiAxis);
        GraphicProxy& asFloat(float value, uint16_t fontSize = 20);
        GraphicProxy& asInt(int32_t value, uint16_t fontSize = 20);

        void abort();

      private:
        UiRendererSrvc* _renderer = nullptr;
        RMInteractionFigurePayload _payload {};
        bool _commitOnDestruct = true;
    };

    GraphicProxy draw(const uint8_t name[3], GraphicOption opt = GraphicOption::Add);

    void sendCustomPacket(uint16_t subCmdId, const void* payloadData, uint16_t payloadLen);

    void drawLine(const GraphicProperties& props, uint16_t endX, uint16_t endY);
    void drawRectangle(const GraphicProperties& props, uint16_t endX, uint16_t endY);
    void drawCircle(const GraphicProperties& props, uint16_t radius);
    void drawEllipse(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY);
    void drawArc(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY, uint16_t startAngle,
                 uint16_t endAngle);
    void drawFloat(const GraphicProperties& props, uint16_t fontSize, float value);
    void drawInt(const GraphicProperties& props, uint16_t fontSize, int32_t value);
    void clearGraphic(GraphicDelMode mode, const uint8_t* graphicName = nullptr);

  private:
    QueueHandle_t _renderQueue = nullptr;
    PacketTransmitCallback _packetTransmitCallback = nullptr;
    void* _packetTransmitContext = nullptr;
    uint16_t _senderId = 3;
    uint16_t _queueDepth = 35;
    uint8_t* _txBuffer = nullptr;
    uint16_t _txBufferSize = 0;
    uint8_t _seqCounter = 0;

    static void _applyProperties(RMInteractionFigurePayload& payload, const GraphicProperties& props, GraphicType type);
    void _submitToPipeline(const RMInteractionFigurePayload& payload);
};

#endif
