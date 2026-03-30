# Thermal Report

Generated: `2026-03-27 15:32:34`

## 1. Purpose

This folder contains a **placeholder thermal estimate** for the current `Photography Fill Light` hardware concept.

It is not a final thermal sign-off. It is only used to:

- estimate the main heatsink thermal load,
- estimate LED/Q5 temperature risk,
- give a first-pass target for heatsink-to-ambient thermal resistance,
- mark which values must be replaced by real measurements.

## 2. Placeholder Inputs That Must Be Replaced

- TODO: Replace full_power_current_a with measured LED current.
- TODO: Replace led_forward_voltage_v with measured COB voltage at full power.
- TODO: Replace led_optical_efficiency with real LED datasheet or integrating sphere result.
- TODO: Replace led_r_jc_c_per_w and led_r_cs_c_per_w with the real COB datasheet and TIM stack value.
- TODO: Replace q5_drain_to_gnd_target_v with measured steady-state value from current firmware.
- TODO: Replace boost_efficiency with measured board efficiency.
- TODO: Replace ambient_temp_list_c with the real product target environment.

## 3. Summary

| Item | Value | Comment |
| --- | --- | --- |
| Full-power current | 2.00 A | Placeholder, TODO verify by measurement |
| COB forward voltage | 36.42 V | Placeholder from local docs |
| LED electrical power | 72.84 W | Vf x I |
| Sense voltage | 0.20 V | I x 0.1 ohm |
| Q5 true Vds | 0.30 V | Drain-to-GND target minus source sense |
| Q5 loss | 0.60 W | I x Vds_true |
| LED heat | 54.63 W | Assume optical efficiency = 25% |
| Boost loss | 6.39 W | Assume boost efficiency = 92% |
| Main heatsink load | 55.23 W | LED heat + Q5 loss |
| Total internal heat | 63.12 W | Heatsink load + boost loss + aux power |


## 4. Required Heatsink Target

| Ambient | Required Rth_sa | Comment |
| --- | --- | --- |
| 25 C | 0.57 C/W | To keep LED junction <= 100 C |
| 35 C | 0.39 C/W | To keep LED junction <= 100 C |
| 40 C | 0.30 C/W | To keep LED junction <= 100 C |


**Current design recommendation:** target the main heatsink at **<= 0.35 C/W with forced airflow** if the project remains in the `60W-80W` power class.

## 5. Scenario Sweep

| Ambient | Rth_sa | Sink Temp | LED Tj | Q5 Tj | LED Status |
| --- | --- | --- | --- | --- | --- |
| 25 C | 1.00 C/W | 80.2 C | 123.9 C | 80.7 C | FAIL |
| 25 C | 0.70 C/W | 63.7 C | 107.4 C | 64.1 C | FAIL |
| 25 C | 0.50 C/W | 52.6 C | 96.3 C | 53.0 C | PASS |
| 25 C | 0.35 C/W | 44.3 C | 88.0 C | 44.8 C | PASS |
| 25 C | 0.25 C/W | 38.8 C | 82.5 C | 39.2 C | PASS |
| 35 C | 1.00 C/W | 90.2 C | 133.9 C | 90.7 C | FAIL |
| 35 C | 0.70 C/W | 73.7 C | 117.4 C | 74.1 C | FAIL |
| 35 C | 0.50 C/W | 62.6 C | 106.3 C | 63.0 C | FAIL |
| 35 C | 0.35 C/W | 54.3 C | 98.0 C | 54.8 C | PASS |
| 35 C | 0.25 C/W | 48.8 C | 92.5 C | 49.2 C | PASS |
| 40 C | 1.00 C/W | 95.2 C | 138.9 C | 95.7 C | FAIL |
| 40 C | 0.70 C/W | 78.7 C | 122.4 C | 79.1 C | FAIL |
| 40 C | 0.50 C/W | 67.6 C | 111.3 C | 68.0 C | FAIL |
| 40 C | 0.35 C/W | 59.3 C | 103.0 C | 59.8 C | FAIL |
| 40 C | 0.25 C/W | 53.8 C | 97.5 C | 54.2 C | PASS |


## 6. Core Equations

- `P_led_elec = V_led x I_led`
- `V_sense = I_led x R_sense`
- `Vds_true = V_drain_to_gnd_target - V_sense`
- `P_q5 = Vds_true x I_led`
- `P_led_heat = P_led_elec x (1 - optical_efficiency)`
- `P_boost_loss = P_boost_out / eta_boost - P_boost_out`
- `T_sink = T_ambient + P_heatsink x Rth_sa`
- `Tj_led = T_sink + P_led_heat x (Rth_jc_led + Rth_cs_led)`
- `Tj_q5 = T_sink + P_q5 x (Rth_jc_q5 + Rth_cs_q5)`

## 7. Engineering Notes

- The current firmware strategy tries to keep `Q5` close to low headroom at high power, so **Q5 should not be the main heatsink load** in normal operation.
- The LED heat dominates the heatsink design.
- Boost loss is still important, but it mainly affects PCB hot spots and airflow planning rather than the COB heatsink size directly.
- If measured `Q5 drain-to-GND` is higher than this placeholder, update the input file immediately. Q5 loss will rise fast.

## 8. Output Files

- `thermal_budget.csv`: component heat budget
- `thermal_led_tj_vs_rsa.svg`: LED junction temperature trend versus heatsink thermal resistance
