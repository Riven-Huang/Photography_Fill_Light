# STM32 PWM-RC-运放-MOS 恒流模型使用与调试说明

## 1. 模型目的
- 用 `STM32F030F4P6` 的 PWM（仿真中等效为脉冲电压源）经 RC 滤波得到模拟参考电压。
- 参考电压送入 `TLV2372IDR`（SPICE 子电路）形成误差放大。
- 运放驱动 `IRFP260NPBF`（SPICE 子电路）工作在线性区，配合采样电阻形成恒流环。

该模型是器件级 Simscape Electrical 网络，不是传函/平均值等效模型。

## 2. 关键文件
- 构建脚本: `build_spice_device_level_simscape_model.m`
- 主模型: `pwm_rc_tlv2372_irfp260npbf_simscape.slx`
- 自定义库: `spiceparts_lib.slx`
- 自定义组件包: `+spiceparts/`
- 运放 SPICE: `models/tlv2372_sloc063/TLV2372_PSPICE_AIO/TLV2372.LIB`
- MOS SPICE: `models/IRFP260NPBF_subckt.lib`

## 3. 模型拓扑解读
信号链路：

`VPWM_STM32 -> R_FILT_10k + C_FILT_1u -> TLV2372(+)-> TLV2372输出 -> R_GATE_10 -> IRFP260NPBF Gate`

反馈链路：

`IRFP260NPBF Source -> R_SENSE_0p1 -> GND`

并将 `Source` 电压反馈到 `TLV2372(-)`，构成负反馈恒流控制。

功率链路：

`VBUS_24V -> R_LOAD_10R -> I_LED_Sensor -> IRFP260NPBF Drain -> IRFP260NPBF Source -> R_SENSE_0p1 -> GND`

## 4. 默认参数与含义
- PWM 频率：`PER = 20.833e-6 s`（约 48 kHz）
- PWM 高电平：`3.3 V`
- PWM 脉宽：`PW = 1.0e-6 s`（占空比约 4.8%）
- PWM 延时：`TD = 2e-3 s`（前 2 ms 不开关，便于初始收敛）
- RC：`10 kOhm + 1 uF`
- 采样电阻：`0.1 Ohm`
- 总线电压：`24 V`
- 运放供电：`12 V`
- 栅极泄放：`100 kOhm`（Gate 到 Source）

近似目标关系（稳态）：

`I_led ~= V_ref / R_sense`

例如当前占空比约 4.8%，理论 `V_ref ~= 0.158 V`，电流目标约 `1.58 A`（受负载和压降限制）。

## 5. 运行方式
在 MATLAB 执行：

```matlab
cd('C:/Users/banzang/Desktop/lightlightlight/simulink');
build_spice_device_level_simscape_model('C:/Users/banzang/Desktop/lightlightlight/simulink');
out = sim('pwm_rc_tlv2372_irfp260npbf_simscape');
```

如果只看波形，打开模型后运行即可，在 `Scope` 观察 5 路信号。

## 6. Scope 波形通道定义
`Mux` 到 `Scope` 的顺序固定为：

1. `V_pwm`（PWM 原始波形）
2. `V_ref`（RC 滤波后参考）
3. `V_ds`（MOS 管压降）
4. `V_sense`（采样电阻上端电压）
5. `I_led`（回路电流）

判读建议：
- `0~2 ms`：PWM 尚未启动（因 `TD=2ms`），环路处于静态偏置。
- `2 ms` 之后：`V_ref` 上升并趋于稳态，`V_sense` 跟踪 `V_ref`，`I_led` 按比例建立。
- 若 `V_ds` 接近 0 且 `I_led`达不到目标，说明压差不够或负载过重，已接近掉出恒流区。

## 7. 调参与映射到实物
最常调参数：

- 改目标电流：
  - 调 `VPWM_STM32.PW`（占空比）
  - 或调 `R_SENSE_0p1`
- 改动态响应：
  - 调 `R_FILT_10k` / `C_FILT_1u`
  - 调 `R_GATE_10`
- 改工况边界：
  - 调 `VBUS_24V`
  - 调 `R_LOAD_10R`

与 MCU 实物映射：
- `VPWM_STM32` 对应 STM32 的 TIM3 PWM 输出（你板级可映射 CH1/CH2/CH4 任一）。
- 模型里只关心 PWM 电平、频率、占空比，不绑定具体 GPIO 引脚名。

## 8. 常见问题与排查
### 8.1 初始条件不收敛
现脚本已经做了收敛增强配置，核心是：
- `SolverCfg.DoDC = off`
- `UseLocalSolver = on`，`NE_BACKWARD_EULER_ADVANCER`
- `ResolveIndetEquations = on`
- `TLV2372` 开启寄生：`specifyParasiticValues = yes`

如果你手改后再次不收敛，先恢复以上设置。

### 8.2 `spiceparts_lib` shadowing 警告
这是路径上同名库导致，通常不影响运行。建议：
- 统一从 `simulink` 目录启动 MATLAB。
- 保证使用当前目录下的 `spiceparts_lib.slx` 与 `+spiceparts`。

### 8.3 找不到 SPICE 文件
脚本启动会检查：
- `models/.../TLV2372.LIB`
- `models/IRFP260NPBF_subckt.lib`

若缺失会直接报错，补齐后重跑构建脚本。

### 8.4 波形异常（全高/全低/电流不动）
按顺序检查：
1. `V_pwm` 是否正常脉冲（注意前 2 ms 延时）
2. `V_ref` 是否有 RC 充放电曲线
3. `V_sense` 是否跟踪 `V_ref`
4. `V_ds` 是否有足够裕量（非长期贴地）
5. `R_sense` 与目标电流是否匹配

## 9. 推荐调试流程
1. 先只看 `V_pwm` 与 `V_ref`，确认“PWM->DAC”链路正常。
2. 再看 `V_sense` 是否跟 `V_ref` 一致，确认反馈闭环方向正确。
3. 最后看 `I_led` 与 `V_ds`，判断是否进入恒流区且有压差裕量。
4. 每次只改一个参数，记录前后波形对比。

---
如果后续你提供厂家官方 `IRFP260NPBF` SPICE `.lib`，可直接替换 `models/IRFP260NPBF_subckt.lib` 再重建模型，以提升器件一致性。
