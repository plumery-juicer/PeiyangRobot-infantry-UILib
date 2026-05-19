# referee-hud-ui

RoboMaster 裁判系统 HUD 绘制与发送队列库。

本库包含裁判系统 UI 协议类型、CRC 计算、渲染队列服务，以及当前步兵 HUD 的业务 UI。库本身不创建线程，
也不直接访问板级外设。线程创建、调度周期、机器人状态采样、硬件发送函数都由调用者负责。

## 环境要求

- C++17 或更新版本
- FreeRTOS Queue API
- 调用者提供的数据包发送回调
- 调用者提供的发送帧缓冲区

## 库组件

- `UiRendererSrvc`：负责图形命令入队、裁判系统交互帧打包、通过注入的回调发送数据。保留链式绘制接口：

```cpp
renderer.draw(name, GraphicOption::Add)
    .layer(0)
    .color(UiColor::Cyan)
    .width(2)
    .start(100, 100)
    .asLine(200, 100);
```

- `RefereeHudUi`：将 `RefereeHudInput` 快照转换成当前 HUD 布局，内部处理静态图形 ADD、动态图形 ADD/UPDATE、
  脏标记过滤以及整屏重置。
- `RefereeHudInput`：统一的 HUD 输入快照，包含电容电压、四个开关、自瞄状态和双轮腿姿态。
- `RefereeHudSpec`：HUD 输入归一化和轮腿姿态辅助计算函数。
- `referee-hud-protocol.h`：RoboMaster 裁判系统 UI 协议枚举和 packed payload 类型。
- `referee-hud-crc.h`：打包交互帧时使用的 CRC8/CRC16 工具。

## CMake 接入

将库目录加入工程，并链接别名目标：

```cmake
add_subdirectory(path/to/referee-hud-ui)
target_link_libraries(app PRIVATE referee_hud::ui)
```

库导出 `cxx_std_17`。主工程仍然可以使用更高的 C++ 标准。

## FreeRTOS 接入

库通过可覆盖宏引用 FreeRTOS 头文件：

```cpp
#ifndef REFEREE_HUD_FREERTOS_HEADER
#define REFEREE_HUD_FREERTOS_HEADER "FreeRTOS.h"
#endif

#ifndef REFEREE_HUD_FREERTOS_QUEUE_HEADER
#define REFEREE_HUD_FREERTOS_QUEUE_HEADER "queue.h"
#endif

#include REFEREE_HUD_FREERTOS_HEADER
#include REFEREE_HUD_FREERTOS_QUEUE_HEADER
```

库不写死本地 FreeRTOS 路径。根据主工程情况选择下面任意一种方式。

### 已有 FreeRTOS CMake Target

如果主工程中已经存在以下任一 target，`referee-hud-ui` 会自动链接：

- `FreeRTOS::Kernel`
- `freertos_kernel`
- `FreeRTOS`
- `freertos`

```cmake
add_subdirectory(path/to/referee-hud-ui)
target_link_libraries(app PRIVATE referee_hud::ui)
```

### 自定义 FreeRTOS Target 名称

```cmake
set(REFEREE_HUD_UI_FREERTOS_TARGET my_freertos_target CACHE STRING "" FORCE)
add_subdirectory(path/to/referee-hud-ui)
target_link_libraries(app PRIVATE referee_hud::ui)
```

### 没有 FreeRTOS Target

如果主工程没有封装 FreeRTOS target，可以直接传入 include 路径：

```cmake
set(REFEREE_HUD_UI_FREERTOS_INCLUDE_DIRS
    "${FREERTOS_DIR}/include;${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1;${PROJECT_CONFIG_DIR}"
    CACHE STRING "" FORCE
)
add_subdirectory(path/to/referee-hud-ui)
target_link_libraries(app PRIVATE referee_hud::ui)
```

这种方式只解决头文件路径问题。FreeRTOS 的源码编译和链接仍由主工程负责。

### 通过宏指定头文件路径

如果调用者希望直接用宏指定头文件：

```cmake
target_compile_definitions(app PRIVATE
    REFEREE_HUD_FREERTOS_HEADER=\"Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h\"
    REFEREE_HUD_FREERTOS_QUEUE_HEADER=\"Middlewares/Third_Party/FreeRTOS/Source/include/queue.h\"
)
```

也可以在 `add_subdirectory` 前设置库的 cache 变量：

```cmake
set(REFEREE_HUD_UI_FREERTOS_HEADER
    "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
    CACHE STRING "" FORCE
)
set(REFEREE_HUD_UI_FREERTOS_QUEUE_HEADER
    "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
    CACHE STRING "" FORCE
)
add_subdirectory(path/to/referee-hud-ui)
```

## Renderer 初始化

`UiRendererSrvc` 的运行时依赖全部通过 `UiRendererSrvc::Config` 注入：

```cpp
struct UiRendererSrvc::Config {
    PacketTransmitCallback packetTransmit;
    void* packetTransmitContext;
    uint16_t senderId;
    uint16_t queueDepth;
    uint8_t* txBuffer;
    uint16_t txBufferSize;
};
```

字段含义：

- `packetTransmit`：最终发送回调，参数是一整帧裁判系统交互数据。
- `packetTransmitContext`：可选的用户上下文，会原样传回 `packetTransmit`。
- `senderId`：写入交互帧头的发送机器人 ID。
- `queueDepth`：图形 payload 的 FreeRTOS 队列深度。
- `txBuffer`：外部发送帧缓冲区，renderer 打包帧时直接写入该缓冲区。
- `txBufferSize`：`txBuffer` 的字节长度。

`receiverId` 在库内按 `senderId + 0x0100` 自动生成。

示例：

```cpp
#include "ui-renderer-srvc.h"

static uint8_t refereeUiTxBuffer[256];

bool transmitRefereeUiPacket(const uint8_t* data, uint16_t length, void* context) {
    (void)context;
    return boardTransmitRefereePacket(data, length);
}

static UiRendererSrvc renderer({
    transmitRefereeUiPacket,
    nullptr,
    3,
    35,
    refereeUiTxBuffer,
    sizeof(refereeUiTxBuffer),
});

void uiRendererInit() {
    renderer.init();
}

void uiRendererTaskLoop() {
    renderer.run();
}
```

`init()` 在以下情况会返回 `false`：

- 发送回调为空
- 外部发送缓冲区为空
- 外部发送缓冲区长度为 0
- 队列深度为 0
- FreeRTOS 队列创建失败

`run()` 每次调用最多发送一组待发送图形。组包数量遵循裁判系统 UI 支持的 1、2、5、7 图形包。
调用者应在自己创建的线程或任务中周期性调用 `run()`，用于消费图形队列。

如果 `packetTransmit` 启动 DMA 后立即返回，调用者必须保证外部 `txBuffer` 在 DMA 完成前不会被下一次
`run()` 覆盖。可以通过限制 `run()` 调用时机、在板级适配层复制到另一个 DMA buffer、或使用发送完成标志来处理。

## 绘制原始图形

使用 `draw()` 进行链式绘制。代理对象离开作用域时，图形命令会自动提交到渲染队列。

```cpp
const uint8_t lineName[3] = {'L', 'N', '0'};

{
    auto line = renderer.draw(lineName, GraphicOption::Add);
    line.layer(0)
        .color(UiColor::White)
        .width(2)
        .start(100, 200)
        .asLine(240, 200);
}
```

更新已有图形时，使用相同的三字节名称，并将操作改为 `GraphicOption::Update`：

```cpp
{
    auto line = renderer.draw(lineName, GraphicOption::Update);
    line.layer(0)
        .color(UiColor::Green)
        .width(3)
        .start(100, 220)
        .asLine(260, 220);
}
```

所有图形都必须先 ADD，再 UPDATE。`RefereeHudUi` 内部已经维护了当前 HUD 布局的 ADD/UPDATE 状态。

清屏示例：

```cpp
renderer.clearGraphic(GraphicDelMode::All);
```

## HUD 业务 UI 用法

使用当前步兵 HUD 时，优先使用 `RefereeHudUi`，不要在业务层手动拼每个图形。

```cpp
#include "referee-hud-ui.h"
#include "ui-renderer-srvc.h"

static RefereeHudUi hudUi;

void drawHudFrame() {
    RefereeHudInput input {};
    input.capVoltage = 23.5f;
    input.capEnabled = true;
    input.capError = false;
    input.turboEnabled = true;
    input.feederEnabled = false;
    input.spinEnabled = false;
    input.aimModeState = 0;
    input.aimTargetState = static_cast<uint8_t>(RefereeHudAimTarget::Locked);

    input.leftLegThighAngleDeg = 32.0f;
    input.leftLegHipWheelDistance = 120.0f;
    input.rightLegThighAngleDeg = 46.0f;
    input.rightLegHipWheelDistance = 150.0f;

    hudUi.draw(renderer, input);
}
```

推荐调用流程：

1. 使用发送回调和外部缓冲区构造 `UiRendererSrvc`。
2. 调用 `renderer.init()`。
3. 构造 `RefereeHudUi`。
4. UI 启动或操作手请求重置时，调用 `hudUi.reset(renderer)`。
5. 周期性采样机器人状态，填充 `RefereeHudInput`。
6. 在 UI 生产者任务中调用 `hudUi.draw(renderer, input)`。
7. 在 renderer 任务中周期性调用 `renderer.run()`，将队列中的图形发给裁判系统。

`RefereeHudUi::kPeriodSeconds` 当前为 `0.050f`，可作为 HUD 生产者任务的默认周期。

## 输入源抽象

库提供 `RefereeHudInputSource`，用于将真实通信数据、Blackboard 数据或仿真信号统一转换为
`RefereeHudInput`：

```cpp
class MyHudInputSource final : public RefereeHudInputSource {
  public:
    RefereeHudInput sample(float dt) override {
        (void)dt;

        RefereeHudInput input {};
        input.capVoltage = readCapVoltage();
        input.capEnabled = readCapSwitch();
        input.capError = readCapError();
        input.aimModeState = readAimMode();
        input.aimTargetState = readAimTargetState();

        input.legLengthState = RefereeHudSpec::normalizeLegLengthState(readLegState());
        RefereeHudSpec::fillDualLegPoseFromState(input);
        return input;
    }
};
```

`RefereeHudSpec::fillDualLegPoseFromState()` 适用于只有三档腿长状态的情况。如果调用者有真实的双腿姿态数据，
应直接填写以下字段：

- `leftLegThighAngleDeg`
- `leftLegHipWheelDistance`
- `rightLegThighAngleDeg`
- `rightLegHipWheelDistance`

轮腿 UI 中的连杆长度是固定的。腿部姿态通过大腿角度和胯关节到轮心的距离变化，而不是通过缩放连杆长度变化。

## 当前固件中的接入方式

在本仓库中，Chassis 目标分为两层接入这个库：

- `Solution/Application/referee-hud-renderer-app.cpp`
  - 持有外部 DMA 可用发送缓冲区
  - 通过 `UiRendererSrvc::Config` 注入 `HAL_UART_Transmit_DMA()`
  - 在 renderer 应用中周期调用 `UiRendererSrvc::run()`
- `Solution/Application/ui-maker-app.cpp`
  - 从 Blackboard 或仿真输入中采样 HUD 输入快照
  - 在需要重置时调用 `RefereeHudUi::reset()`
  - 周期调用 `RefereeHudUi::draw()`

库本身不依赖这些应用类。其他工程可以替换为自己的任务调度、数据源和板级发送适配层。
