function build_pwm_rc_opamp_cc_model(output_dir)
% Build a Simulink model for: MCU PWM -> RC DAC -> op-amp -> MOSFET CC loop
%
% Based on project/instruction parameters:
% - PWM frequency: 48 kHz (TIM3 @ 48MHz, ARR=999, PSC=0)
% - RC filter: 10 kOhm + 1 uF
% - Current sense resistor: 0.1 Ohm
% - Target Vds around 0.8 V (headroom concept)

if nargin < 1 || isempty(output_dir)
    output_dir = fileparts(mfilename('fullpath'));
end

model = 'pwm_rc_opamp_cc_model';

if bdIsLoaded(model)
    close_system(model, 0);
end

new_system(model);
open_system(model);

set_param(model, ...
    'StopTime', '0.02', ...
    'Solver', 'ode23tb', ...
    'MaxStep', '1e-6', ...
    'RelTol', '1e-4');

mws = get_param(model, 'ModelWorkspace');
assignin(mws, 'PWM_FREQ', 48000);
assignin(mws, 'VDD', 3.3);
assignin(mws, 'R_FILT', 10e3);
assignin(mws, 'C_FILT', 1e-6);
assignin(mws, 'R_SENSE', 0.1);
assignin(mws, 'K_OPAMP', 300);
assignin(mws, 'F_OPAMP', 2e4);
assignin(mws, 'VTH', 2.2);
assignin(mws, 'K_MOS', 1.2);
assignin(mws, 'I_LIMIT', 3.0);
assignin(mws, 'TAU_I', 2e-4);
assignin(mws, 'VBOOST', 24);
assignin(mws, 'VLED', 20);
assignin(mws, 'RLOAD', 1.0);

% PWM generation
add_block('simulink/Sources/Step', [model '/DutyCmd'], ...
    'Time', '0.01', 'Before', '0.35', 'After', '0.55', ...
    'Position', [60 100 100 130]);

add_block('simulink/Sources/Repeating Sequence', [model '/Carrier'], ...
    'rep_seq_t', '[0 1/PWM_FREQ]', 'rep_seq_y', '[0 1]', ...
    'Position', [60 170 140 210]);

add_block('simulink/Logic and Bit Operations/Relational Operator', [model '/PWMCompare'], ...
    'Operator', '<', 'Position', [190 125 235 195]);

add_block('simulink/Signal Attributes/Data Type Conversion', [model '/BoolToDouble'], ...
    'OutDataTypeStr', 'double', ...
    'Position', [270 145 360 175]);

add_block('simulink/Math Operations/Gain', [model '/PWMtoVolt'], ...
    'Gain', 'VDD', 'Position', [400 145 470 175]);

% Passive RC filter to emulate analog DAC
add_block('simulink/Continuous/Transfer Fcn', [model '/RC_DAC'], ...
    'Numerator', '1', 'Denominator', '[R_FILT*C_FILT 1]', ...
    'Position', [520 140 630 180]);

% Current feedback path: Iled -> Vsense
add_block('simulink/Math Operations/Gain', [model '/SenseGain'], ...
    'Gain', 'R_SENSE', 'Position', [1040 265 1110 295]);

% Op-amp loop: error = Vref - Vsense
add_block('simulink/Math Operations/Add', [model '/ErrorSum'], ...
    'Inputs', '+-', 'Position', [700 160 730 220]);

add_block('simulink/Math Operations/Gain', [model '/OpAmpGain'], ...
    'Gain', 'K_OPAMP', 'Position', [770 175 850 205]);

add_block('simulink/Continuous/Transfer Fcn', [model '/OpAmpPole'], ...
    'Numerator', '1', 'Denominator', '[1/(2*pi*F_OPAMP) 1]', ...
    'Position', [890 170 1020 210]);

add_block('simulink/Discontinuities/Saturation', [model '/GateSat'], ...
    'LowerLimit', '0', 'UpperLimit', '10', ...
    'Position', [1050 170 1120 210]);

% MOSFET approximation: Id ~ K*(Vg - Vth), with compliance clamp
add_block('simulink/Math Operations/Add', [model '/VovSum'], ...
    'Inputs', '+-', 'Position', [1160 170 1190 220]);

add_block('simulink/Sources/Constant', [model '/VthConst'], ...
    'Value', 'VTH', 'Position', [1120 230 1170 260]);

add_block('simulink/Discontinuities/Saturation', [model '/VovClamp'], ...
    'LowerLimit', '0', 'UpperLimit', '10', ...
    'Position', [1220 175 1290 215]);

add_block('simulink/Math Operations/Gain', [model '/MosTransconductance'], ...
    'Gain', 'K_MOS', 'Position', [1320 180 1420 210]);

add_block('simulink/Sources/Constant', [model '/ILimit'], ...
    'Value', 'I_LIMIT', 'Position', [1320 235 1380 265]);

add_block('simulink/Math Operations/MinMax', [model '/CurrentClamp'], ...
    'Function', 'min', 'Inputs', '2', ...
    'Position', [1460 188 1525 242]);

add_block('simulink/Continuous/Transfer Fcn', [model '/CurrentDynamics'], ...
    'Numerator', '1', 'Denominator', '[TAU_I 1]', ...
    'Position', [1560 190 1680 230]);

% Compliance / observables
add_block('simulink/Math Operations/Gain', [model '/LoadDrop'], ...
    'Gain', 'RLOAD', 'Position', [1040 335 1110 365]);

add_block('simulink/Sources/Constant', [model '/VboostConst'], ...
    'Value', 'VBOOST', 'Position', [1210 320 1270 350]);

add_block('simulink/Sources/Constant', [model '/VledConst'], ...
    'Value', 'VLED', 'Position', [1210 365 1270 395]);

add_block('simulink/Math Operations/Add', [model '/VdsSum'], ...
    'Inputs', '+---', 'Position', [1310 330 1360 400]);

add_block('simulink/Discontinuities/Saturation', [model '/VdsClamp'], ...
    'LowerLimit', '0', 'UpperLimit', 'inf', ...
    'Position', [1400 350 1460 380]);

% Scope signals
add_block('simulink/Signal Routing/Mux', [model '/ScopeMux'], ...
    'Inputs', '5', 'Position', [1760 90 1780 250]);

add_block('simulink/Sinks/Scope', [model '/Scope'], ...
    'Position', [1820 130 1890 210]);

% Optional workspace logging
add_block('simulink/Sinks/To Workspace', [model '/Iled_ws'], ...
    'VariableName', 'sim_Iled', 'SaveFormat', 'Structure With Time', ...
    'Position', [1760 290 1870 320]);

add_block('simulink/Sinks/To Workspace', [model '/Vref_ws'], ...
    'VariableName', 'sim_Vref', 'SaveFormat', 'Structure With Time', ...
    'Position', [680 80 790 110]);

add_block('simulink/Sinks/To Workspace', [model '/Vds_ws'], ...
    'VariableName', 'sim_Vds', 'SaveFormat', 'Structure With Time', ...
    'Position', [1490 350 1600 380]);

% Wiring
add_line(model, 'DutyCmd/1', 'PWMCompare/2', 'autorouting', 'on');
add_line(model, 'Carrier/1', 'PWMCompare/1', 'autorouting', 'on');
add_line(model, 'PWMCompare/1', 'BoolToDouble/1', 'autorouting', 'on');
add_line(model, 'BoolToDouble/1', 'PWMtoVolt/1', 'autorouting', 'on');
add_line(model, 'PWMtoVolt/1', 'RC_DAC/1', 'autorouting', 'on');

add_line(model, 'RC_DAC/1', 'ErrorSum/1', 'autorouting', 'on');
add_line(model, 'RC_DAC/1', 'Vref_ws/1', 'autorouting', 'on');

add_line(model, 'SenseGain/1', 'ErrorSum/2', 'autorouting', 'on');
add_line(model, 'ErrorSum/1', 'OpAmpGain/1', 'autorouting', 'on');
add_line(model, 'OpAmpGain/1', 'OpAmpPole/1', 'autorouting', 'on');
add_line(model, 'OpAmpPole/1', 'GateSat/1', 'autorouting', 'on');

add_line(model, 'GateSat/1', 'VovSum/1', 'autorouting', 'on');
add_line(model, 'VthConst/1', 'VovSum/2', 'autorouting', 'on');
add_line(model, 'VovSum/1', 'VovClamp/1', 'autorouting', 'on');
add_line(model, 'VovClamp/1', 'MosTransconductance/1', 'autorouting', 'on');

add_line(model, 'MosTransconductance/1', 'CurrentClamp/1', 'autorouting', 'on');
add_line(model, 'ILimit/1', 'CurrentClamp/2', 'autorouting', 'on');
add_line(model, 'CurrentClamp/1', 'CurrentDynamics/1', 'autorouting', 'on');

add_line(model, 'CurrentDynamics/1', 'SenseGain/1', 'autorouting', 'on');
add_line(model, 'CurrentDynamics/1', 'LoadDrop/1', 'autorouting', 'on');
add_line(model, 'CurrentDynamics/1', 'Iled_ws/1', 'autorouting', 'on');

add_line(model, 'VboostConst/1', 'VdsSum/1', 'autorouting', 'on');
add_line(model, 'VledConst/1', 'VdsSum/2', 'autorouting', 'on');
add_line(model, 'LoadDrop/1', 'VdsSum/3', 'autorouting', 'on');
add_line(model, 'SenseGain/1', 'VdsSum/4', 'autorouting', 'on');
add_line(model, 'VdsSum/1', 'VdsClamp/1', 'autorouting', 'on');
add_line(model, 'VdsClamp/1', 'Vds_ws/1', 'autorouting', 'on');

add_line(model, 'PWMtoVolt/1', 'ScopeMux/1', 'autorouting', 'on');
add_line(model, 'RC_DAC/1', 'ScopeMux/2', 'autorouting', 'on');
add_line(model, 'SenseGain/1', 'ScopeMux/3', 'autorouting', 'on');
add_line(model, 'CurrentDynamics/1', 'ScopeMux/4', 'autorouting', 'on');
add_line(model, 'VdsClamp/1', 'ScopeMux/5', 'autorouting', 'on');
add_line(model, 'ScopeMux/1', 'Scope/1', 'autorouting', 'on');

set_param([model '/Scope'], 'NumInputPorts', '1');

save_path = fullfile(output_dir, [model '.slx']);
save_system(model, save_path);

fprintf('Generated model: %s\n', save_path);
end
