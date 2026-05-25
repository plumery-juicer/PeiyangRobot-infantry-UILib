#include "ui-renderer-srvc.h"

#include "referee-hud-crc.h"

UiRendererSrvc::UiRendererSrvc(const Config& config)
    : _packetTransmitCallback(config.packetTransmit), _packetTransmitContext(config.packetTransmitContext),
      _senderId(config.senderId), _queueDepth(config.queueDepth), _txBuffer(config.txBuffer),
      _txBufferSize(config.txBufferSize) {}

bool UiRendererSrvc::init() {
    /* Renderer 依赖外部发送函数和外部发送缓冲区。这里直接失败，避免后续 run()
     * 静默丢包或写入空指针。 */
    if (_packetTransmitCallback == nullptr || _txBuffer == nullptr || _txBufferSize == 0 || _queueDepth == 0) {
        return false;
    }

    /* init() 可以重复调用。已有队列时只清空队列，保持对象生命周期由调用者控制。 */
    if (_renderQueue == nullptr) {
        _renderQueue = xQueueCreate(_queueDepth, sizeof(RMInteractionFigurePayload));
    } else {
        xQueueReset(_renderQueue);
    }

    _seqCounter = 0;
    return _renderQueue != nullptr;
}

void UiRendererSrvc::run() {
    if (_renderQueue == nullptr) {
        return;
    }

    const UBaseType_t waitingCount = uxQueueMessagesWaiting(_renderQueue);
    if (waitingCount == 0) {
        return;
    }

    RMInteractionFigurePayload peekPayload {};
    if (xQueuePeek(_renderQueue, &peekPayload, 0) != pdTRUE) {
        return;
    }

    /* clearGraphic(All) 需要先清空图形队列，再发送删除全部图形包。为了让删除请求仍然
     * 走同一条发送链路，队列中使用 0xFF/0xFF 作为内部哨兵 payload。 */
    if (peekPayload.graphicName[0] == 0xFF && peekPayload.graphicName[1] == 0xFF) {
        xQueueReceive(_renderQueue, &peekPayload, 0);

        const RMInteractionLayerDeletePayload delData {GraphicDelMode::All, 0};
        sendCustomPacket(static_cast<uint16_t>(RMSubCmdId::DeleteLayer), &delData, sizeof(delData));
        return;
    }

    uint8_t batchSize = 0;
    RMSubCmdId subCmdId = RMSubCmdId::RenderOne;
    /* 裁判系统图形包只支持 1/2/5/7 四种批量大小。每次 run() 取当前队列中
     * 能组成的最大合法包，减少发送次数，同时不等待未来图形。 */
    if (waitingCount >= 7) {
        batchSize = 7;
        subCmdId = RMSubCmdId::RenderSeven;
    } else if (waitingCount >= 5) {
        batchSize = 5;
        subCmdId = RMSubCmdId::RenderFive;
    } else if (waitingCount >= 2) {
        batchSize = 2;
        subCmdId = RMSubCmdId::RenderTwo;
    } else {
        batchSize = 1;
        subCmdId = RMSubCmdId::RenderOne;
    }

    RMInteractionFigurePayload payloadBatch[7] {};
    for (uint8_t i = 0; i < batchSize; ++i) {
        xQueueReceive(_renderQueue, &payloadBatch[i], 0);
    }

    sendCustomPacket(static_cast<uint16_t>(subCmdId), payloadBatch,
                     static_cast<uint16_t>(sizeof(RMInteractionFigurePayload) * batchSize));
}

UiRendererSrvc::GraphicProxy::GraphicProxy(UiRendererSrvc& renderer, const uint8_t name[3], GraphicOption opt)
    : _renderer(&renderer) {
    std::memset(&_payload, 0, sizeof(_payload));
    _payload.graphicName[0] = name[0];
    _payload.graphicName[1] = name[1];
    _payload.graphicName[2] = name[2];
    _payload.opt = static_cast<uint32_t>(opt);
    _payload.color = static_cast<uint32_t>(UiColor::Main);
    _payload.width = 2;
    _payload.layer = 0;
}

UiRendererSrvc::GraphicProxy::GraphicProxy(GraphicProxy&& other) noexcept
    : _renderer(other._renderer), _payload(other._payload),
      _commitOnDestruct(std::exchange(other._commitOnDestruct, false)) {}

UiRendererSrvc::GraphicProxy::~GraphicProxy() {
    /* 链式绘制依赖 RAII 提交：局部作用域结束时，完整 payload 自动进入队列。 */
    if (_commitOnDestruct && _renderer != nullptr) {
        _renderer->_submitToPipeline(_payload);
    }
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::color(UiColor color) {
    _payload.color = static_cast<uint32_t>(color);
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::layer(uint8_t layer) {
    _payload.layer = layer;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::width(uint32_t width) {
    _payload.width = width;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::start(uint32_t x, uint32_t y) {
    _payload.startX = x;
    _payload.startY = y;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asLine(uint16_t endX, uint16_t endY) {
    _payload.type = static_cast<uint32_t>(GraphicType::Line);
    _payload.endX = endX;
    _payload.endY = endY;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asRectangle(uint16_t endX, uint16_t endY) {
    _payload.type = static_cast<uint32_t>(GraphicType::Rectangle);
    _payload.endX = endX;
    _payload.endY = endY;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asCircle(uint16_t radius) {
    _payload.type = static_cast<uint32_t>(GraphicType::Circle);
    _payload.param3 = radius;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asEllipse(uint16_t xSemiAxis, uint16_t ySemiAxis) {
    _payload.type = static_cast<uint32_t>(GraphicType::Ellipse);
    _payload.endX = xSemiAxis;
    _payload.endY = ySemiAxis;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asArc(uint16_t startAngle, uint16_t endAngle,
                                                                  uint16_t xSemiAxis, uint16_t ySemiAxis) {
    _payload.type = static_cast<uint32_t>(GraphicType::Arc);
    _payload.param1 = startAngle;
    _payload.param2 = endAngle;
    _payload.endX = xSemiAxis;
    _payload.endY = ySemiAxis;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asFloat(float value, uint16_t fontSize) {
    _payload.type = static_cast<uint32_t>(GraphicType::Float);
    _payload.param1 = fontSize;
    /* 裁判系统浮点图形以 value * 1000 的整数形式拆入 param3/endX/endY。 */
    const int32_t scaledValue = static_cast<int32_t>(value * 1000.0f);
    _payload.param3 = scaledValue & 0x3FF;
    _payload.endX = (scaledValue >> 10) & 0x7FF;
    _payload.endY = (scaledValue >> 21) & 0x7FF;
    return *this;
}

UiRendererSrvc::GraphicProxy& UiRendererSrvc::GraphicProxy::asInt(int32_t value, uint16_t fontSize) {
    _payload.type = static_cast<uint32_t>(GraphicType::Int);
    _payload.param1 = fontSize;
    _payload.param3 = value & 0x3FF;
    _payload.endX = (value >> 10) & 0x7FF;
    _payload.endY = (value >> 21) & 0x7FF;
    return *this;
}

void UiRendererSrvc::GraphicProxy::abort() { _commitOnDestruct = false; }

UiRendererSrvc::GraphicProxy UiRendererSrvc::draw(const uint8_t name[3], GraphicOption opt) {
    return GraphicProxy(*this, name, opt);
}

void UiRendererSrvc::sendCustomPacket(uint16_t subCmdId, const void* payloadData, uint16_t payloadLen) {
    if (payloadData == nullptr || _packetTransmitCallback == nullptr || _txBuffer == nullptr || _txBufferSize == 0) {
        return;
    }

    const uint16_t dataSegmentLen = static_cast<uint16_t>(sizeof(RmInteractiveHeader) + payloadLen);
    const uint16_t frameTotalLength = static_cast<uint16_t>(sizeof(RmFrameHeader) + sizeof(uint16_t) + dataSegmentLen +
                                                            sizeof(uint16_t));
    if (frameTotalLength > _txBufferSize) {
        return;
    }

    uint16_t offset = 0;

    RmFrameHeader header {};
    header.sof = 0xA5;
    header.dataLength = dataSegmentLen;
    header.seq = _seqCounter++;
    /* 帧头 CRC8 覆盖 sof/dataLength/seq/crc8，appendCrc8 会填充最后一个字节。 */
    std::memcpy(&_txBuffer[offset], &header, sizeof(header) - sizeof(header.crc8));
    RefereeHudCrc::appendCrc8(_txBuffer, sizeof(header));
    offset += sizeof(header);

    const uint16_t cmdId = 0x0301;
    std::memcpy(&_txBuffer[offset], &cmdId, sizeof(cmdId));
    offset += sizeof(cmdId);

    RmInteractiveHeader interactHeader {};
    interactHeader.subCmdId = subCmdId;
    interactHeader.senderId = _senderId;
    /* 选手端客户端 ID 按机器人 ID + 0x0100 生成，调用者只需传入 senderId。 */
    interactHeader.receiverId = static_cast<uint16_t>(_senderId + 0x0100U);
    std::memcpy(&_txBuffer[offset], &interactHeader, sizeof(interactHeader));
    offset += sizeof(interactHeader);

    std::memcpy(&_txBuffer[offset], payloadData, payloadLen);
    offset += payloadLen;

    RefereeHudCrc::appendCrc16(_txBuffer, frameTotalLength);
    _packetTransmitCallback(_txBuffer, frameTotalLength, _packetTransmitContext);
}

void UiRendererSrvc::_applyProperties(RMInteractionFigurePayload& payload, const GraphicProperties& props,
                                      GraphicType type) {
    std::memcpy(payload.graphicName, props.name, sizeof(payload.graphicName));
    payload.opt = static_cast<uint32_t>(props.action);
    payload.type = static_cast<uint32_t>(type);
    payload.layer = props.layer;
    payload.color = static_cast<uint32_t>(props.color);
    payload.width = props.lineWidth;
    payload.startX = props.startX;
    payload.startY = props.startY;
    payload.param1 = 0;
    payload.param2 = 0;
    payload.param3 = 0;
    payload.endX = 0;
    payload.endY = 0;
}

void UiRendererSrvc::_submitToPipeline(const RMInteractionFigurePayload& payload) {
    if (_renderQueue != nullptr) {
        /* 队列满时阻塞等待，保证 ADD/UPDATE 顺序不被丢弃。调用者应给 renderer
         * 任务足够高的运行频率，避免生产端长期阻塞。 */
        xQueueSend(_renderQueue, &payload, portMAX_DELAY);
    }
}

void UiRendererSrvc::drawLine(const GraphicProperties& props, uint16_t endX, uint16_t endY) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Line);
    payload.endX = endX;
    payload.endY = endY;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawRectangle(const GraphicProperties& props, uint16_t endX, uint16_t endY) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Rectangle);
    payload.endX = endX;
    payload.endY = endY;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawCircle(const GraphicProperties& props, uint16_t radius) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Circle);
    payload.param3 = radius;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawEllipse(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Ellipse);
    payload.endX = radiusX;
    payload.endY = radiusY;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawArc(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY, uint16_t startAngle,
                             uint16_t endAngle) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Arc);
    payload.param1 = startAngle;
    payload.param2 = endAngle;
    payload.endX = radiusX;
    payload.endY = radiusY;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawFloat(const GraphicProperties& props, uint16_t fontSize, float value) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Float);
    payload.param1 = fontSize;
    const uint32_t rawValue = static_cast<uint32_t>(static_cast<int32_t>(value * 1000.0f));
    payload.param3 = rawValue & 0x3FF;
    payload.endX = (rawValue >> 10) & 0x7FF;
    payload.endY = (rawValue >> 21) & 0x7FF;
    _submitToPipeline(payload);
}

void UiRendererSrvc::drawInt(const GraphicProperties& props, uint16_t fontSize, int32_t value) {
    RMInteractionFigurePayload payload {};
    _applyProperties(payload, props, GraphicType::Int);
    payload.param1 = fontSize;
    payload.param3 = value & 0x3FF;
    payload.endX = (value >> 10) & 0x7FF;
    payload.endY = (value >> 21) & 0x7FF;
    _submitToPipeline(payload);
}

void UiRendererSrvc::clearGraphic(GraphicDelMode mode, const uint8_t* graphicName) {
    RMInteractionFigurePayload payload {};
    payload.opt = static_cast<uint32_t>(GraphicOption::Delete);

    switch (mode) {
        case GraphicDelMode::Name:
            if (graphicName != nullptr) {
                std::memcpy(payload.graphicName, graphicName, sizeof(payload.graphicName));
            }
            break;

        case GraphicDelMode::Layer:
            payload.color = static_cast<uint32_t>(UiColor::Del);
            payload.layer = graphicName != nullptr ? graphicName[0] : 0;
            break;

        case GraphicDelMode::All:
            if (_renderQueue == nullptr) {
                return;
            }
            /* 删除全部图形时先丢弃未发送的旧图形，避免清屏后又刷新出旧状态。 */
            xQueueReset(_renderQueue);
            payload.graphicName[0] = 0xFF;
            payload.graphicName[1] = 0xFF;
            payload.graphicName[2] = 0xFF;
            _submitToPipeline(payload);
            return;

        default:
            break;
    }

    _submitToPipeline(payload);
}
