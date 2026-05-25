/**
 *******************************************************************************
 * @file    ui-renderer-srvc.h
 * @brief   裁判系统 UI 图形命令队列与交互帧打包服务。
 *******************************************************************************
 * @attention
 * 本服务只依赖 FreeRTOS Queue 和调用者注入的发送回调。库内不创建线程，
 * 不直接访问 UART、DMA、HAL 或板级外设。调用者需要自行决定 run() 的调用周期。
 *******************************************************************************
 */

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

/**
 * @brief 裁判系统 UI 渲染服务。
 *
 * 业务绘制代码通过 draw() 或 drawLine()/drawArc() 等接口提交单个图形 payload。
 * Renderer 内部使用 FreeRTOS Queue 缓存图形，并在 run() 中按照裁判系统支持的
 * 1/2/5/7 图形包自动打包发送。
 */
class UiRendererSrvc {
  public:
    /**
     * @brief 完整裁判交互帧发送回调。
     * @param data 已带帧头、命令字、交互头和 CRC 的完整帧缓冲区。
     * @param length data 有效字节数。
     * @param context 调用者在 Config 中传入的用户上下文。
     * @return true 表示调用者接受本次发送请求；库当前不根据返回值重试。
     */
    using PacketTransmitCallback = bool (*)(const uint8_t* data, uint16_t length, void* context);

    /**
     * @brief Renderer 构造参数。
     *
     * 所有字段都由调用者注入，库不会分配发送缓冲区，也不会假设具体串口实现。
     */
    struct Config {
        /// 完整帧发送回调，必须非空。
        PacketTransmitCallback packetTransmit = nullptr;
        /// 透传给 packetTransmit 的用户上下文，不需要时可为 nullptr。
        void* packetTransmitContext = nullptr;
        /// 裁判系统交互帧 senderId；receiverId 由库自动生成为 senderId + 0x0100。
        uint16_t senderId = 3;
        /// FreeRTOS 图形 payload 队列深度，单位为“单个 RMInteractionFigurePayload”。
        uint16_t queueDepth = 35;
        /// 打包交互帧使用的外部缓冲区，生命周期必须长于 Renderer。
        uint8_t* txBuffer = nullptr;
        /// txBuffer 总字节数，需能容纳最大 7 图形包的完整裁判交互帧。
        uint16_t txBufferSize = 0;
    };

    /**
     * @brief 构造 Renderer，但不创建 FreeRTOS 队列。
     * @param config 发送回调、队列深度、发送缓冲区和裁判系统 ID。
     */
    explicit UiRendererSrvc(const Config& config);

    /**
     * @brief 创建或重置内部图形队列。
     * @return true 表示配置有效且队列创建成功。
     */
    bool init();

    /**
     * @brief 发送队列中的一批图形。
     *
     * 每次最多发送一个裁判交互包。调用者通常以 33ms 或 50ms 周期调用该函数。
     */
    void run();

    /**
     * @brief 链式图形构造代理。
     *
     * 代理析构时自动把 payload 提交到 Renderer 队列，因此推荐用局部作用域包住一次绘制。
     * 如需中途取消本次绘制，调用 abort()。
     */
    class GraphicProxy {
      public:
        /**
         * @brief 创建一个图形 payload 构造代理。
         * @param renderer 接收最终 payload 的 Renderer。
         * @param name 裁判系统三字节图形名，同名图形后续可用 Update 修改。
         * @param opt ADD/UPDATE/DELETE 等裁判系统图形操作。
         */
        GraphicProxy(UiRendererSrvc& renderer, const uint8_t name[3], GraphicOption opt = GraphicOption::Add);
        GraphicProxy(const GraphicProxy&) = delete;
        GraphicProxy& operator=(const GraphicProxy&) = delete;
        GraphicProxy(GraphicProxy&& other) noexcept;
        ~GraphicProxy();

        ///< 设置图形颜色。
        GraphicProxy& color(UiColor color);
        ///< 设置图层，裁判系统有效范围通常为 0~9。
        GraphicProxy& layer(uint8_t layer);
        ///< 设置线宽或字符线宽。
        GraphicProxy& width(uint32_t width);
        ///< 设置起点或中心点坐标。
        GraphicProxy& start(uint32_t x, uint32_t y);

        ///< 将当前 payload 设置为直线，参数为终点坐标。
        GraphicProxy& asLine(uint16_t endX, uint16_t endY);
        ///< 将当前 payload 设置为矩形，参数为对角点坐标。
        GraphicProxy& asRectangle(uint16_t endX, uint16_t endY);
        ///< 将当前 payload 设置为圆，参数为半径。
        GraphicProxy& asCircle(uint16_t radius);
        ///< 将当前 payload 设置为椭圆，参数为 x/y 半轴。
        GraphicProxy& asEllipse(uint16_t xSemiAxis, uint16_t ySemiAxis);
        ///< 将当前 payload 设置为圆弧，角度单位为 deg，半轴单位为 UI 坐标。
        GraphicProxy& asArc(uint16_t startAngle, uint16_t endAngle, uint16_t xSemiAxis, uint16_t ySemiAxis);
        ///< 将当前 payload 设置为浮点数字，库按裁判系统协议放大 1000 倍编码。
        GraphicProxy& asFloat(float value, uint16_t fontSize = 20);
        ///< 将当前 payload 设置为整数数字。
        GraphicProxy& asInt(int32_t value, uint16_t fontSize = 20);

        /**
         * @brief 取消析构时自动提交。
         */
        void abort();

      private:
        UiRendererSrvc* _renderer = nullptr;        ///< 接收 payload 的 Renderer。
        RMInteractionFigurePayload _payload {};     ///< 正在构造的裁判系统图形 payload。
        bool _commitOnDestruct = true;              ///< true 时析构自动提交到队列。
    };

    /**
     * @brief 创建链式绘制代理。
     * @param name 三字节图形名。
     * @param opt 图形操作，默认 Add。
     * @return 可继续设置颜色、图层、坐标和图形类型的代理对象。
     */
    GraphicProxy draw(const uint8_t name[3], GraphicOption opt = GraphicOption::Add);

    /**
     * @brief 直接打包并发送一个裁判系统自定义交互 payload。
     * @param subCmdId 裁判系统交互子命令。
     * @param payloadData 子命令 payload 数据地址。
     * @param payloadLen 子命令 payload 字节数。
     */
    void sendCustomPacket(uint16_t subCmdId, const void* payloadData, uint16_t payloadLen);

    /** @brief 直接提交直线图形 payload。 */
    void drawLine(const GraphicProperties& props, uint16_t endX, uint16_t endY);
    /** @brief 直接提交矩形图形 payload。 */
    void drawRectangle(const GraphicProperties& props, uint16_t endX, uint16_t endY);
    /** @brief 直接提交圆形图形 payload。 */
    void drawCircle(const GraphicProperties& props, uint16_t radius);
    /** @brief 直接提交椭圆图形 payload。 */
    void drawEllipse(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY);
    /** @brief 直接提交圆弧图形 payload。 */
    void drawArc(const GraphicProperties& props, uint16_t radiusX, uint16_t radiusY, uint16_t startAngle,
                 uint16_t endAngle);
    /** @brief 直接提交浮点数字图形 payload。 */
    void drawFloat(const GraphicProperties& props, uint16_t fontSize, float value);
    /** @brief 直接提交整数数字图形 payload。 */
    void drawInt(const GraphicProperties& props, uint16_t fontSize, int32_t value);
    /**
     * @brief 清除客户端图形。
     * @param mode 按全部、图层或名称删除。
     * @param graphicName mode 为 Name 时传三字节图形名；mode 为 Layer 时使用 graphicName[0] 作为图层号。
     */
    void clearGraphic(GraphicDelMode mode, const uint8_t* graphicName = nullptr);

  private:
    QueueHandle_t _renderQueue = nullptr;                         ///< FreeRTOS 图形 payload 队列。
    PacketTransmitCallback _packetTransmitCallback = nullptr;      ///< 调用者提供的完整帧发送函数。
    void* _packetTransmitContext = nullptr;                        ///< 透传给发送函数的上下文。
    uint16_t _senderId = 3;                                        ///< 当前机器人裁判系统发送 ID。
    uint16_t _queueDepth = 35;                                     ///< 图形 payload 队列深度。
    uint8_t* _txBuffer = nullptr;                                  ///< 外部发送缓冲区。
    uint16_t _txBufferSize = 0;                                    ///< 外部发送缓冲区长度。
    uint8_t _seqCounter = 0;                                       ///< 裁判系统帧序号。

    /**
     * @brief 将通用图形属性写入具体图形 payload。
     */
    static void _applyProperties(RMInteractionFigurePayload& payload, const GraphicProperties& props, GraphicType type);

    /**
     * @brief 将单个图形 payload 提交到内部队列。
     */
    void _submitToPipeline(const RMInteractionFigurePayload& payload);
};

#endif
