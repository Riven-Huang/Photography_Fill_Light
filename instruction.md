# 补光灯驱动控制系统 (COB LED Driver Firmware Specification)

## 1. 项目概述
[cite_start]本项目是一个基于 STM32F030F4P6TR 微控制器的双通道大功率 COB 补光灯控制板 [cite: 1]。系统采用“前级升压 (Boost) + 后级线性恒流 (Linear CC)”的混合架构，驱动共阳极 COB 灯珠。散热系统采用 775 散热器主动散热。

固件的核心任务是：
1. 解析 EC11 旋钮输入，设置目标亮度。
2. 输出 PWM 控制 TLV2372 运放，实现纯直流（无频闪）恒流调光。
3. **（核心难点）** 通过 ADC 实时采集恒流管的漏极电压 ($V_{ds}$)，并动态输出 PWM 注入 MP3910A 的 FB 引脚，调节 Boost 输出电压 (VOUT)，实现**动态余量控制 (Dynamic Headroom Tracking)**，将恒流管的热耗散降到最低。
4. 输出 PWM 控制 775 散热风扇转速。

## 2. 硬件架构与引脚映射 (Pinout Mapping)

| 引脚名称 (STM32) | 功能类型 | 信号网络名称 | 硬件描述与作用 |
| :--- | :--- | :--- | :--- |
| **PA0** | ADC (IN0) | `GNDA_V_D` | [cite_start]采集通道 A 恒流 MOS (IRF540N) 的漏极电压 ($V_{ds}$) [cite: 10, 104, 123]。 |
| **PA1** | ADC (IN1) | `GNDB_V_D` | [cite_start]采集通道 B 恒流 MOS (IRF540N) 的漏极电压 ($V_{ds}$) [cite: 11, 105, 123]。 |
| **PA2** | PWM Out | `FAN_CTRL` | [cite_start]控制 775 散热风扇的转速 (建议频率 25kHz) [cite: 12, 43]。 |
| **PA3** | GPIO_EXTI | `EC11_A` | [cite_start]旋转编码器 A 相 [cite: 13, 37]。 |
| **PA4** | GPIO_EXTI | `EC11_B` | [cite_start]旋转编码器 B 相 [cite: 14, 36]。 |
| **PA5** | GPIO_Input| `EC11_D` | [cite_start]旋转编码器按键输入 [cite: 40]。 |
| **PA6** | PWM Out | `GNDA_CUR_CTRL` | [cite_start]经 RC 滤波后输入运放同相端，设定通道 A 的恒定电流 [cite: 29, 75]。 |
| **PA7** | PWM Out | `GNDB_CUR_CTRL` | [cite_start]经 RC 滤波后输入运放同相端，设定通道 A 的恒定电流 [cite: 29, 75]。 |
| **PB1** | PWM Out | `MP3910A_FB` | [cite_start]经 RC 滤波后注入 Boost 芯片 MP3910A 的 FB 引脚，动态调节系统供电 VOUT [cite: 63, 71, 112]。 |

*(注：CITE有误，你读到这一段时候，你给按照原理图改一下)*

## 3. 核心控制逻辑与控制环 (Control Loops)

系统存在两个互相配合的控制环路：

### 3.1 内部恒流环 (Inner CC Loop) - 纯硬件闭环，软件给基准
* [cite_start]**机制**：由 TLV2372 运放 [cite: 54] [cite_start]驱动 IRF540N MOS 管 [cite: 123] 构成的线性恒流源。
* [cite_start]**软件任务**：软件仅需改变 `GNDA_CUR_CTRL` 和 `GNDB_CUR_CTRL` 的 PWM 占空比 [cite: 50, 75]。占空比越高，RC 滤波后的直流基准电压越高，输出的恒定电流越大。
* **特性**：PWM 频率建议较高（如 >10kHz），以减小 RC 滤波后的纹波，保证纯直流调光的无频闪特性。

### 3.2 外部电压追踪环 (Outer Voltage Tracking Loop) - 软件闭环
* **背景**：为了防止线性工作的 IRF540N 过热烧毁（Spirito 效应），需要使其漏源极电压 ($V_{ds}$) 保持在一个刚好能维持恒流的极低值（例如目标值设定为 $V_{target} = 0.8V$）。
* [cite_start]**测量**：由于两路共阳极 (VOUT) [cite: 81][cite_start]，双通道的电流或 LED 正向压降 ($V_f$) 可能不同，因此 `GNDA_V_D` 和 `GNDB_V_D` 会有差异 [cite: 104, 105]。软件必须同时读取两者的 ADC 值，并取**最小值**作为控制变量：$V_{min} = \min(V_{ds\_A}, V_{ds\_B})$。
* **控制（反向逻辑，请特别注意）**：
  * [cite_start]使用 PID 算法计算出 `MP3910A_FB` 的 PWM 占空比 [cite: 71, 112]。
  * **逻辑反转**：增大 `MP3910A_FB` 的 PWM 占空比 -> FB 引脚注入电压升高 -> MP3910A 认为输出过高 -> **降低 VOUT 输出电压** -> $V_{min}$ 降低。
  * **调节目标**：调整 FB PWM 占空比，使得计算出的 $V_{min}$ 趋近并稳定在目标值 (如 0.8V)。如果 $V_{min} < 0.8V$，说明面临脱离恒流区的风险，需要**减小** FB PWM 占空比以抬升 VOUT；如果 $V_{min} > 0.8V$，说明 MOS 管功耗过大，需要**增大** FB PWM 占空比以降低 VOUT。

## 4. 关键安全与保护机制 (Safety Constraints)

1. **开机启动顺序 (Soft-Start)**：
   * [cite_start]系统上电时，`MP3910A_FB` 的 PWM 必须先输出高占空比（甚至 100%），强制压低 VOUT 电压，防止冷启动时瞬间输出高压过冲击穿 LED 或 MOS 管 [cite: 71, 81, 112]。
   * [cite_start]然后缓慢开启恒流 PWM (`GNDA_CUR_CTRL`, `GNDB_CUR_CTRL`) [cite: 50, 75]。
   * 最后再启动电压追踪 PID，缓慢降低 FB PWM 占空比，将 VOUT 抬升到工作电压。
2. [cite_start]**过热保护 (Thermal Protection)**：如果 $V_{min}$ 长期无法降至安全范围内（例如超过 3V 持续 2 秒），极易导致 IRF540N [cite: 123] 烧毁。此时必须立即关闭恒流输出（占空比设为 0）并全速开启风扇，系统进入故障状态。
3. [cite_start]**ADC 采样滤波**：由于开关电源可能存在噪声，对 `GNDA_V_D` 和 `GNDB_V_D` 的 ADC 采样必须使用软件滤波（如滑动平均滤波或中值滤波），避免 PID 环路因噪声产生剧烈震荡 [cite: 104, 105]。