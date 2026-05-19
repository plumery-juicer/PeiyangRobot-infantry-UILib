# referee-hud-ui

RoboMaster 裁判系统 HUD 绘制库。这个库的目标是让其他项目可以快速复用当前步兵 UI：把库作为
submodule 拉进工程，接入 CMake，提供 FreeRTOS Queue、发送回调和外部发送缓冲区，就可以开始绘制。

库本身不创建线程，不直接访问 HAL、UART、DMA 或项目内的 Blackboard。调用者负责线程调度、数据采样和
最终硬件发送。

## 5 分钟快速导入

### 1. 添加 Git 子模块

在目标工程根目录执行：

```bash
git submodule add git@github.com:MekCraftLi/PeiyangRobot-infantry-UILib.git ThirdParty/referee-hud-ui
git submodule update --init --recursive
```

如果工程已经有 `ThirdParty/` 或 `Solution/ThirdParty/` 目录，也可以把路径改成自己的第三方库目录：

```bash
git submodule add git@github.com:MekCraftLi/PeiyangRobot-infantry-UILib.git Solution/ThirdParty/referee-hud-ui
```

克隆已有工程时，使用：

```bash
git clone --recursive <your-project>
```

或者克隆后执行：

```bash
git submodule update --init --recursive
```

### 2. 在 CMake 中加入库

最小接入：

```cmake
add_subdirectory(ThirdParty/referee-hud-ui)
target_link_libraries(your_firmware_target PRIVATE referee_hud::ui)
```

如果库放在 `Solution/ThirdParty/referee-hud-ui`：

```cmake
add_subdirectory(Solution/ThirdParty/referee-hud-ui)
target_link_libraries(your_firmware_target PRIVATE referee_hud::ui)
```

库导出 `cxx_std_17`。主工程可以继续使用 C++17、C++20 或 C++23。

### 3. 处理 FreeRTOS 头文件路径

如果主工程已经有以下任一 CMake target，库会自动链接：

- `FreeRTOS::Kernel`
- `freertos_kernel`
- `FreeRTOS`
- `freertos`

如果没有 FreeRTOS target，先把 FreeRTOS include 路径传给库：

```cmake
set(REFEREE_HUD_UI_FREERTOS_INCLUDE_DIRS
    "${FREERTOS_DIR}/include;${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1;${PROJECT_CONFIG_DIR}"
    CACHE STRING "" FORCE
)

add_subdirectory(ThirdParty/referee-hud-ui)
target_link_libraries(your_firmware_target PRIVATE referee_hud::ui)
```

如果你的 FreeRTOS 头文件必须用带路径的 include，也可以传宏：

```cmake
set(REFEREE_HUD_UI_FREERTOS_HEADER
    "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
    CACHE STRING "" FORCE
)
set(REFEREE_HUD_UI_FREERTOS_QUEUE_HEADER
    "Middlewares/Third_Party/FreeRTOS/Source/include/queue.h"
    CACHE STRING "" FORCE
)

add_subdirectory(ThirdParty/referee-hud-ui)
target_link_libraries(your_firmware_target PRIVATE referee_hud::ui)
```

### 4. 写一个板级发送适配层

库只需要一个“发送完整裁判系统交互帧”的函数。UART、DMA、互斥、发送完成判断都由主工程自己处理。

```cpp
#include "ui-renderer-srvc.h"

static uint8_t refereeUiTxBuffer[256];

static bool transmitRefereeUiPacket(const uint8_t* data, uint16_t length, void* context) {
    (void)context;

    // 替换成你的工程自己的裁判系统串口发送函数。
    // 如果这里启动 DMA 后立即返回，需要确保 refereeUiTxBuffer 在 DMA 完成前不会被再次覆盖。
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
```

`senderId` 直接写入交互帧头，`receiverId` 由库自动生成为 `senderId + 0x0100`。

### 5. 在线程中运行 renderer

库不会创建线程。调用者需要创建一个任务周期性调用 `renderer.run()`。

```cpp
void refereeUiRendererTask(void*) {
    if (!renderer.init()) {
        // 初始化失败：检查发送回调、txBuffer、txBufferSize、queueDepth 和 FreeRTOS queue heap。
        return;
    }

    for (;;) {
        renderer.run();
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
```

`run()` 每次最多发送一包图形，包大小按裁判系统支持的 1、2、5、7 图形自动选择。

### 6. 周期性喂入 HUD 数据

如果要使用当前步兵 HUD，创建 `RefereeHudUi`，周期性填充 `RefereeHudInput` 后调用 `draw()`。

```cpp
#include "referee-hud-ui.h"

static RefereeHudUi hudUi;

void refereeHudProducerTask(void*) {
    hudUi.reset(renderer);

    for (;;) {
        RefereeHudInput input {};
        input.capVoltage = readCapVoltage();
        input.capEnabled = readCapSwitch();
        input.capError = readCapError();

        input.turboEnabled = readTurboSwitch();
        input.feederEnabled = readFeederSwitch();
        input.spinEnabled = readSpinSwitch();

        input.aimModeState = readAimMode();
        input.aimTargetState = readAimTargetState();

        input.leftLegThighAngleDeg = readLeftLegThighAngleDeg();
        input.leftLegHipWheelDistance = readLeftLegHipWheelDistance();
        input.rightLegThighAngleDeg = readRightLegThighAngleDeg();
        input.rightLegHipWheelDistance = readRightLegHipWheelDistance();

        hudUi.draw(renderer, input);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

如果只有三档腿长状态，没有真实双腿姿态数据，可以用库内 helper 填充：

```cpp
input.legLengthState = RefereeHudSpec::normalizeLegLengthState(readLegState());
RefereeHudSpec::fillDualLegPoseFromState(input);
```

到这里，其他工程已经可以完成最小接入。

## 构造环境与推荐参数

库内真正需要显式构造的是 `UiRendererSrvc`。`RefereeHudUi` 使用默认构造函数，不需要传入参数。
`RefereeHudProducer` 和 `RefereeHudRenderer` 是推荐的任务职责命名，不是库内类型，任务是否创建由主工程决定。

构造 `UiRendererSrvc` 前需要准备：

- FreeRTOS Queue API 已可用，`FreeRTOS.h` 和 `queue.h` 能被 include。
- 一个生命周期长于 `UiRendererSrvc` 的外部发送缓冲区。
- 一个发送完整裁判系统交互帧的回调函数。
- 明确本机器人裁判系统 `senderId`。
- 明确调用 `renderer.run()` 的任务或调度位置。

构造函数参数：

```cpp
static UiRendererSrvc renderer({
    transmitRefereeUiPacket,
    nullptr,
    3,
    35,
    refereeUiTxBuffer,
    sizeof(refereeUiTxBuffer),
});
```

参数解释和推荐值：

| 字段 | 含义 | 推荐值 |
| --- | --- | --- |
| `packetTransmit` | 发送完整裁判系统交互帧的回调函数 | 必须提供，不能为 `nullptr` |
| `packetTransmitContext` | 传回发送回调的用户上下文 | 不需要上下文时填 `nullptr` |
| `senderId` | 写入交互帧头的发送机器人 ID | 使用本机器人裁判系统 ID；步兵常用 `3` |
| `queueDepth` | 图形 payload 的 FreeRTOS 队列深度 | `35` 起步，可缓存 5 组 7 图形包 |
| `txBuffer` | renderer 打包交互帧使用的外部缓冲区 | 静态或全局缓冲区，不要使用局部栈变量 |
| `txBufferSize` | `txBuffer` 字节长度 | `256` 字节起步 |

补充约束：

- `receiverId` 由库按 `senderId + 0x0100` 自动生成。
- `init()` 会创建 FreeRTOS 队列，建议在任务初始化阶段调用并检查返回值。
- 如果发送回调启动 DMA 后立即返回，`txBuffer` 在 DMA 完成前不能被下一次 `renderer.run()` 覆盖。
- 推荐 `renderer.run()` 周期为 `33ms`；图形较少或需要降低链路压力时可用 `50ms`。
- 使用当前 HUD 业务层时，推荐 `RefereeHudUi::draw()` 周期为 `50ms`。

## 最小文件清单

其他项目通常只需要写两个本地文件：

- `referee-hud-renderer-adapter.cpp`
  - 持有外部发送缓冲区
  - 构造 `UiRendererSrvc`
  - 注入 UART/DMA 发送回调
  - 在线程中周期调用 `renderer.run()`
- `referee-hud-producer.cpp`
  - 从本工程的数据源采样状态
  - 填充 `RefereeHudInput`
  - 调用 `RefereeHudUi::reset()` 和 `RefereeHudUi::draw()`

库内不需要知道你的工程使用 Blackboard、消息队列、全局变量还是通信包。

## 快速接入检查表

- `git submodule update --init --recursive` 已执行。
- CMake 已 `add_subdirectory(...)` 并链接 `referee_hud::ui`。
- FreeRTOS 的 `FreeRTOS.h` 和 `queue.h` 能被库 include 到。
- `UiRendererSrvc::Config::packetTransmit` 不为空。
- `UiRendererSrvc::Config::txBuffer` 不为空，`txBufferSize` 足够容纳裁判系统交互帧，建议至少 256 字节。
- `queueDepth` 大于 0。
- 调用过 `renderer.init()`，并检查返回值。
- 有 `RefereeHudRenderer` 或已有任务周期性调用 `renderer.run()`。
- 使用 `RefereeHudUi` 时，有 `RefereeHudProducer` 或已有任务周期性填充 `RefereeHudInput` 并调用 `hudUi.draw()`。
- 所有业务图形通过 `RefereeHudUi::draw()` 或 `renderer.draw()` 提交。
- 如果使用 DMA，确认发送缓冲区不会在 DMA 完成前被下一帧覆盖。

## 库组件说明

- `UiRendererSrvc`：图形命令队列和裁判系统交互帧打包器。
- `RefereeHudUi`：当前步兵 HUD 业务绘制组件。
- `RefereeHudInput`：统一的 HUD 输入快照。
- `RefereeHudInputSource`：可选输入源抽象接口。
- `RefereeHudSpec`：输入归一化和轮腿姿态辅助函数。
- `referee-hud-protocol.h`：裁判系统 UI 协议类型。
- `referee-hud-crc.h`：CRC8/CRC16 工具。

## Renderer API

`UiRendererSrvc::Config`：

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
- `packetTransmitContext`：用户上下文，会原样传回 `packetTransmit`。
- `senderId`：发送机器人 ID。
- `queueDepth`：图形 payload 的 FreeRTOS 队列深度。
- `txBuffer`：外部发送帧缓冲区。
- `txBufferSize`：外部发送帧缓冲区长度。

常用接口：

```cpp
bool init();
void run();
GraphicProxy draw(const uint8_t name[3], GraphicOption opt = GraphicOption::Add);
void clearGraphic(GraphicDelMode mode, const uint8_t* graphicName = nullptr);
```

## 原始图形绘制

链式绘制接口保留原来的使用方式。代理对象离开作用域时，图形命令会提交到 renderer 队列。

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

更新图形时使用同一个三字节名称：

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

所有图形必须先 ADD，再 UPDATE。`RefereeHudUi` 内部已经维护了当前 HUD 布局的 ADD/UPDATE 状态。

清屏：

```cpp
renderer.clearGraphic(GraphicDelMode::All);
```

## HUD 输入字段

`RefereeHudInput` 是当前 HUD 的统一输入：

```cpp
struct RefereeHudInput {
    float capVoltage;
    bool capEnabled;
    bool capError;
    bool resetRequested;
    bool turboEnabled;
    bool feederEnabled;
    bool spinEnabled;
    uint8_t legLengthState;
    uint8_t aimModeState;
    uint8_t aimTargetState;
    float leftLegThighAngleDeg;
    float leftLegHipWheelDistance;
    float rightLegThighAngleDeg;
    float rightLegHipWheelDistance;
};
```

轮腿 UI 中的连杆长度是固定的。腿部姿态通过大腿角度和胯关节到轮心的距离变化，而不是通过缩放连杆长度变化。

`aimTargetState` 建议使用：

```cpp
static_cast<uint8_t>(RefereeHudAimTarget::None)
static_cast<uint8_t>(RefereeHudAimTarget::Locked)
static_cast<uint8_t>(RefereeHudAimTarget::Fire)
```

## 输入源抽象

如果项目希望把真实数据源和 UI 绘制解耦，可以实现 `RefereeHudInputSource`：

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

## 当前仓库中的接入参考

本仓库的 Chassis 目标可以作为接入参考：

- `Solution/Application/referee-hud-renderer-app.cpp`
  - 持有外部 DMA 可用发送缓冲区。
  - 注入 `HAL_UART_Transmit_DMA()`。
  - 周期调用 `UiRendererSrvc::run()`。
- `Solution/Application/ui-maker-app.cpp`
  - 从 Blackboard 或仿真输入中采样 HUD 输入。
  - 需要重置时调用 `RefereeHudUi::reset()`。
  - 周期调用 `RefereeHudUi::draw()`。

这些文件只是本工程的适配层，不属于库的必要依赖。其他工程只需要实现自己的适配层即可。

## 更新子模块

使用者更新 UI 库：

```bash
git submodule update --remote ThirdParty/referee-hud-ui
```

如果库路径不同，将命令中的路径替换为实际路径。
