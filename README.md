# Photography Fill Light

> 面向 `60W-80W` 单色温 COB 的低成本摄影补光灯控制板  
> `STM32F030F4P6TR` + `Boost + 模拟恒流` + `EC11`

这是一个强调“低成本、可复刻、工程逻辑清楚”的摄影补光灯开源项目。  
当前版本不是成品灯，而是一套已经跑通核心链路的工程样机: `EC11` 交互、软启动、Boost 母线控制、风扇联动、基础保护框架和仿真验证都已经具备，适合继续往可复刻开源项目推进。

之所以选择线性整流，目的主要是：

1.防止电流控制的boost右半平面极点，降低控制难度

2.防频闪

3.为未来的共阳极COB灯珠提供方案验证，方便未来改成双色温无极调节

## 1. 项目速览

| 项目    | 当前版本                           |
| ----- | ------------------------------ |
| 项目名称  | `Photography Fill Light`       |
| 目标功率段 | `60W-80W`                      |
| 光源类型  | 单色温 `COB LED`                  |
| 主控    | `STM32F030F4P6TR`              |
| 调光方式  | `EC11` 编码器旋钮 + 按压开关            |
| 功率架构  | `Boost + 模拟恒流`                 |
| 电流参考  | `TIM3 PWM + RC` 伪 DAC          |
| 固件架构  | 裸机状态机 + `ADC + DMA + PWM + PI` |
| 项目定位  | 优先把亮度稳定、低闪烁、易调试、低成本这几件事做好      |

## 2. 实物图

<table>
  <tr>
    <td align="center">
      <img src="docs/images/实物图1.jpg" alt="工程样机实拍" width="460">
    </td>
    <td align="center">
      <img src="docs/images/实物图2.jpg" alt="侧视结构图" width="460">
    </td>
  </tr>
  <tr>
    <td align="center">工程样机实拍</td>
    <td align="center">侧视 / 结构视角</td>
  </tr>
</table>

从现阶段样机可以看到，这个项目的重点不是先把外观做满，而是先把控制板、电源链路、散热路径和调光逻辑打通。对开源项目来说，这样反而更有价值: 电路结构真实、问题边界清楚、后续更容易被别人接着做。

## 3. 这个方案为什么值得开源

- 不是“MCU 直接低频 PWM 斩波 LED”，而是 `Boost + 模拟恒流` 分层控制，更适合视频和拍照场景的连续光输出。
- 控制链路和功率链路是拆开的，出问题更容易定位，适合个人开发者复刻和继续调试。
- 关键量都尽量做成“可测量”的工程变量，比如 `VOUT`、`Q5 drain-to-gnd`、动态余量目标、风扇联动状态。
- 样机已经配套了设计手册、动态余量说明、热设计估算和仿真模型，不只是丢一份原理图。

## 4. 低成本亮点

这套设计最有意思的地方，不是单纯把器件换便宜，而是用架构把成本压下去。

- `TIM3_CH2 + PWM + RC` 直接生成电流参考，不额外上 DAC 芯片，少一颗器件、少一段调试链路。
- 主控选 `STM32F030F4P6TR`，并坚持整数控制，避免为了浮点计算上更高档 MCU。
- `Boost` 负责慢速给母线“补刚好够用的电压”，模拟恒流级负责快速稳流，不需要一开始就上更复杂的大闭环方案。
- `EC11 + 裸机状态机` 就能完成亮度调节、开关机和基本控制节拍，结构简单，适合量产前样机和开源复刻。

按 [`docs/补光灯设计手册.pdf`](docs/补光灯设计手册.pdf) 在 `2026-03-22` 的当前估算，成本拆分如下:

| 项目              | 估算成本      |
| --------------- | --------- |
| 核心电子 BOM 小计     | `13.71 元` |
| 灯珠、结构与散热 BOM 小计 | `25.00 元` |

这个拆分很能说明问题: 真正值得开源复刻的“控制板部分”成本并不高，整机大头反而在供电、附件和结构件上。  
换句话说，这个项目的设计亮点之一，就是把最核心的控制能力尽量压进了低成本电子 BOM 里。

## 5. 关键设计亮点

### 5.1 `Boost + 模拟恒流` 的分层控制

当前方案不是直接用 MCU 去粗暴切 LED，而是:

1. `MP1907 + 外部 MOS + 33uH` 构成 `Boost`
2. `TLV2372 + Q5 + 0.1R` 构成模拟恒流级
3. `TIM3_CH2` 通过 `PWM + RC` 生成电流参考

这样做的核心收益是:

- LED 看到的是更接近连续的电流
- 视频拍摄更容易避开明显闪烁
- 数字控制和模拟稳流各管一层，调试难度更低

### 5.2 动态余量控制

这个项目不是让 `Q5` 长期硬吃大压差，而是让 `Boost` 只给线性恒流级留下“最小可调余量”。

关键关系:

```text
Vsense = Iled x 0.1 ohm
Vds_true = Vdrain_gnd - Vsense
```

也就是说，真正决定 `Q5` 是否还在可调区的，不是单独的 `Vdrain_gnd`，而是 `Vds_true`。

当前文档里已经给出了动态余量思路:

- 低亮度时，多留一点余量，先保稳定
- 高亮度时，把 `Q5` 尽量压到接近全开，减少线性区损耗和发热
- 但不会把 `Q5` 直接硬顶死，避免模拟恒流环失去调节权

这部分可继续参考:

- [`docs/CURRENT_OPERATION_MANUAL.md`](docs/CURRENT_OPERATION_MANUAL.md)
- [`docs/DYNAMIC_HEADROOM_STRATEGY.md`](docs/DYNAMIC_HEADROOM_STRATEGY.md)

### 5.3 整数控制优化

`STM32F030F4P6TR` 没有硬件 FPU，所以当前固件坚持整数实现，避免软件浮点带来的资源浪费。

- `ADC + DMA` 负责采样
- `PI` 控制器采用定点实现
- 亮度、电压和调试量统一按工程量输出

这让项目在低成本 MCU 上依然能把控制节拍、采样链路和交互逻辑跑顺。

## 6. 运行验证

具体验证视频可以看小红书：自制补光灯小test http://xhslink.com/o/AVei6cEGQPr 复制后打开【小红书】查看笔记！

<table>
  <tr>
    <td align="center">
      <img src="docs/images/热成像图.jpg" alt="热成像图" width="420">
    </td>
    <td align="center">
      <img src="docs/images/仿真运行图.png" alt="仿真运行图" width="420">
    </td>
  </tr>
  <tr>
    <td align="center">热成像记录</td>
    <td align="center">仿真运行截图</td>
  </tr>
</table>

<p align="center">
  <img src="docs/images/运行图.jpg" alt="样机运行图" width="300">
</p>
<p align="center">样机运行图</p>

- 当前已经有热成像记录，截图中最高温约 `49.2°C`，但这只是当时工况下的阶段性记录，不等于最终热设计结论。
- [`hot_design/thermal_report.md`](hot_design/thermal_report.md) 目前仍是估算版，不是最终热签核。
- 现有热设计估算建议: 如果继续维持 `60W-80W` 功率级，主散热器建议做到 **`<= 0.35 C/W` 且有强制风冷**。
- 仿真模型已用于辅助分析启动过程、余量变化和线性恒流级行为，便于后续继续迭代控制参数。

## 7. 当前已经实现的功能

- 单色温亮度调节
- `EC11` 旋钮调光，长按开关机
- `TIM3 PWM + RC` 伪 DAC 输出
- `ADC + DMA` 采样 `VOUT / VDS`
- 裸机状态机: `STANDBY / SOFT_START / RUNNING / FAULT`
- Boost 动态母线目标控制
- 过压保护框架
- 风扇联动控制

## 8. 当前版本的边界

作为一个适合继续开源迭代的工程样机，当前版本也有明确边界:

- 还不是最终整机形态，机械结构、外壳和量产散热还可以继续打磨
- 过流、过温、开路等保护逻辑还值得继续补齐
- 效率、照度、噪声和长时间热稳定性还需要更多实测数据
- 热设计文档目前是估算版，后续应逐步替换成实测数据

## 9. 硬件架构

```mermaid
flowchart LR
    VIN[VIN Input] --> U1[MP4420 -> 12V]
    U1 --> U3[MP4420 -> 5V]
    U3 --> U2[MP20051 -> 3.3V]
    VIN --> BOOST[MP1907 + MOS + 33uH Boost]
    BOOST --> VOUT[VOUT Bus]
    VOUT --> CC[TLV2372 + Q5 + R14 Linear CC]
    CC --> COB[Single CCT COB LED]
    MCU[STM32F030F4P6] --> PWM[TIM3 PWM + RC]
    PWM --> CC
    MCU --> ADC[ADC VOUT/VDS]
    ADC --> MCU
    MCU --> EC11[EC11]
    MCU --> FAN[FAN_CTRL]
```

这个架构里每一层的分工很明确:

- `Boost` 负责母线电压
- 模拟恒流级负责 LED 电流
- MCU 负责目标值、软启动、保护和人机交互

## 10. 仓库结构

```text
.
├─ circurit/                 原理图、Gerber
├─ datasheet/                器件规格书、网表
├─ docs/                     设计手册、策略文档、图片资源
├─ hot_design/               热设计估算脚本与结果
├─ program/
│  ├─ Core/                  CubeMX 初始化代码
│  ├─ app/                   状态机、EC11、PI、控制逻辑
│  ├─ Drivers/               HAL / CMSIS
│  └─ MDK-ARM/               Keil 工程与编译输出
└─ simulation/               仿真模型
```

建议优先看:

- [`docs/补光灯设计手册.pdf`](docs/补光灯设计手册.pdf)
- [`docs/CURRENT_OPERATION_MANUAL.md`](docs/CURRENT_OPERATION_MANUAL.md)
- [`docs/DYNAMIC_HEADROOM_STRATEGY.md`](docs/DYNAMIC_HEADROOM_STRATEGY.md)
- [`hot_design/thermal_report.md`](hot_design/thermal_report.md)
- [`program/app/state_machine.c`](program/app/state_machine.c)

## 11. 快速开始

### 11.1 开发环境

- IDE: `Keil MDK-ARM`
- 工程文件: `program/MDK-ARM/Fill_Light.uvprojx`
- CubeMX 工程: `program/Fill_Light.ioc`

### 11.2 编译步骤

1. 打开 `program/MDK-ARM/Fill_Light.uvprojx`
2. 选择目标 `Fill_Light`
3. 确认器件包 `Keil.STM32F0xx_DFP`
4. 编译输出:
   - `program/MDK-ARM/Fill_Light/Fill_Light.axf`
   - `program/MDK-ARM/Fill_Light/Fill_Light.hex`

### 11.3 上电运行

- 上电后默认待机
- 长按 `EC11` 进入运行
- 旋转 `EC11` 调节亮度
- 系统会先软启动，再进入正常运行

## 12. MPS 器件支持

本项目当前使用的 MPS 器件主要包括:

| 模块        | 器件型号         | 说明            |
| --------- | ------------ | ------------- |
| 12V/5V 电源 | `MP4420`     | 降压芯片，给运放和驱动供电 |
| 3.3V 电源   | `MP20051`    | LDO，MCU 供电    |
| Boost 驱动  | `MP1907GQ-Z` | 驱动外部 MOS 和电感  |
| 风扇开关      | `AO3400A`    | 低边风扇驱动        |

如果需要申请样片，可参考:

- [MPS 大学计划](https://www.monolithicpower.cn/cn/support/mps-cn-university.html)
- [MPSNOW](https://www.monolithicpower.cn/cn/support/mps-now.html)

申请备注可填写: `Photography-Fill-Light`

## 13. 开源说明

本项目当前按 **GNU General Public License v3.0 (GPL 3.0)** 进行开源发布，仓库根目录已补充正式的 [`LICENSE`](LICENSE) 文件。  
如果后续发布到立创广场或其他开源平台，建议保持 README 与许可证说明一致，避免后续授权边界不清。
