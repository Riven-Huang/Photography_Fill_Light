# PWM-RC-OpAmp-MOS CC Simulation

## Files
- `build_pwm_rc_opamp_cc_model.m`: Auto-build script.
- `pwm_rc_opamp_cc_model.slx`: Generated Simulink model.

## Build / Rebuild
Run in MATLAB:

```matlab
cd('C:/Users/banzang/Desktop/lightlightlight/simulink');
build_pwm_rc_opamp_cc_model;
```

## What is modeled
- MCU PWM output (`TIM3` equivalent, 48 kHz, 0-3.3 V).
- Passive RC low-pass filter (`10 kOhm + 1 uF`) as analog DAC.
- Op-amp error amplifier (high gain + finite pole + output saturation).
- MOSFET current regulation approximation with source sense resistor (`0.1 Ohm`).
- Observables: `V_pwm`, `V_ref`, `V_sense`, `I_led`, `V_ds`.

## Parameter mapping (from instruction)
- `R_FILT=10e3`, `C_FILT=1e-6`
- `R_SENSE=0.1`
- PWM duty step from `0.35 -> 0.55` at `0.01 s` for response observation.
- `Vds` is monitored to reflect dynamic headroom behavior.
