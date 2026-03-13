function build_spice_device_level_simscape_model(output_dir)
% Build a device-level Simscape Electrical model:
% STM32 PWM -> RC filter DAC -> TLV2372 (SPICE) -> IRFP260NPBF (SPICE) current sink

if nargin < 1 || isempty(output_dir)
    output_dir = fileparts(mfilename('fullpath'));
end

script_dir = fileparts(mfilename('fullpath'));
models_dir = fullfile(script_dir, 'models');

ensure_spiceparts_library(models_dir);
addpath(models_dir);

mdl = 'pwm_rc_tlv2372_irfp260npbf_simscape';
if bdIsLoaded(mdl)
    close_system(mdl, 0);
end

new_system(mdl);
open_system(mdl);

set_param(mdl, ...
    'StopTime', '0.02', ...
    'Solver', 'ode23t', ...
    'MaxStep', '1e-6', ...
    'RelTol', '1e-4');

load_system('ee_lib');
load_system('nesl_utility');
load_system(fullfile(models_dir, 'spiceparts_lib.slx'));

% Resolve library blocks robustly by display name
blkSolver = find_block_by_name('nesl_utility', 'Solver Configuration', '');
blkPS2SL  = find_block_by_name('nesl_utility', 'PS-Simulink Converter', '');
blkRes    = find_block_by_name('ee_lib', 'Resistor', '/Passive/');
blkCap    = find_block_by_name('ee_lib', 'Capacitor', '/Passive/');
blkVsrc   = find_block_by_name('ee_lib', 'Voltage Source', '/Sources/');
blkVPulse = find_block_by_name('ee_lib', 'Pulse Voltage Source', '/Sources/');
blkIsens  = find_block_by_name('ee_lib', 'Current Sensor', '');
blkVsens  = find_block_by_name('ee_lib', 'Voltage Sensor', '');
blkGnd    = find_block_by_name('ee_lib', 'Electrical Reference', '');

% Core electrical blocks
add_block(blkSolver, [mdl '/SolverCfg'], 'Position', [70 560 150 610]);
add_block(blkGnd, [mdl '/GND'], 'Position', [250 610 300 660]);
set_param([mdl '/SolverCfg'], ...
    'DoDC', 'off', ...
    'UseLocalSolver', 'on', ...
    'LocalSolverChoice', 'NE_BACKWARD_EULER_ADVANCER', ...
    'LocalSolverSampleTime', '1e-7', ...
    'ResolveIndetEquations', 'on');

add_block(blkVPulse, [mdl '/VPWM_STM32'], 'Position', [80 120 180 200]);
add_block(blkRes, [mdl '/R_FILT_10k'], 'Position', [250 120 340 190]);
add_block(blkCap, [mdl '/C_FILT_1u'], 'Position', [420 160 510 230]);

add_block('spiceparts_lib/TLV2372', [mdl '/TLV2372IDR'], 'Position', [620 110 760 260]);
add_block(blkRes, [mdl '/R_GATE_10'], 'Position', [820 190 910 260]);
add_block(blkRes, [mdl '/R_GS_BLEED_100k'], 'Position', [930 310 1020 380]);
add_block('spiceparts_lib/IRFP260NPBF', [mdl '/IRFP260NPBF'], 'Position', [980 160 1140 300]);
add_block(blkRes, [mdl '/R_SENSE_0p1'], 'Position', [1190 300 1280 370]);

add_block(blkVsrc, [mdl '/VBUS_24V'], 'Position', [980 420 1070 500]);
add_block(blkRes, [mdl '/R_LOAD_10R'], 'Position', [1140 430 1240 500]);

add_block(blkVsrc, [mdl '/VOP_12V'], 'Position', [620 360 710 440]);
add_block(blkRes, [mdl '/R_VOP_SER_1'], 'Position', [760 360 850 430]);

add_block(blkIsens, [mdl '/I_LED_Sensor'], 'Position', [1290 430 1400 510]);
add_block(blkVsens, [mdl '/V_PWM_Sensor'], 'Position', [90 260 200 340]);
add_block(blkVsens, [mdl '/V_REF_Sensor'], 'Position', [430 260 540 340]);
add_block(blkVsens, [mdl '/V_DS_Sensor'], 'Position', [1010 20 1120 100]);
add_block(blkVsens, [mdl '/V_SENSE_Sensor'], 'Position', [1210 20 1320 100]);

% Converters and scope
add_block(blkPS2SL, [mdl '/PS2SL_PWM'], 'Position', [250 260 340 320]);
add_block(blkPS2SL, [mdl '/PS2SL_VREF'], 'Position', [590 260 680 320]);
add_block(blkPS2SL, [mdl '/PS2SL_VDS'], 'Position', [1160 20 1250 80]);
add_block(blkPS2SL, [mdl '/PS2SL_VSENSE'], 'Position', [1360 20 1450 80]);
add_block(blkPS2SL, [mdl '/PS2SL_ILED'], 'Position', [1450 430 1540 490]);

add_block('simulink/Signal Routing/Mux', [mdl '/Mux'], 'Inputs', '5', ...
    'Position', [1610 190 1630 360]);
add_block('simulink/Sinks/Scope', [mdl '/Scope'], 'Position', [1680 240 1755 320]);

% Parameters
set_param([mdl '/VPWM_STM32'], ...
    'V1', '0', ...
    'V2', '3.3', ...
    'TD', '2e-3', ...
    'TR', '50e-9', ...
    'TF', '50e-9', ...
    'PW', '1.0e-6', ...
    'PER', '20.833e-6');

set_param([mdl '/R_FILT_10k'], 'R', '10e3');
set_param([mdl '/C_FILT_1u'], 'c', '1e-6');
set_param([mdl '/R_GATE_10'], 'R', '10');
set_param([mdl '/R_GS_BLEED_100k'], 'R', '100e3');
set_param([mdl '/R_SENSE_0p1'], 'R', '0.1');
set_param([mdl '/R_LOAD_10R'], 'R', '10');
set_param([mdl '/R_VOP_SER_1'], 'R', '1');

set_param([mdl '/VBUS_24V'], 'dc_voltage', '24');
set_param([mdl '/VOP_12V'], 'dc_voltage', '12');
set_param([mdl '/TLV2372IDR'], ...
    'specifyParasiticValues', 'ee.enum.include.yes', ...
    'capacitorSeriesResistance', '1e-3', ...
    'inductorParallelConductance', '1e-6');

set_param([mdl '/PS2SL_PWM'], 'Unit', 'V');
set_param([mdl '/PS2SL_VREF'], 'Unit', 'V');
set_param([mdl '/PS2SL_VDS'], 'Unit', 'V');
set_param([mdl '/PS2SL_VSENSE'], 'Unit', 'V');
set_param([mdl '/PS2SL_ILED'], 'Unit', 'A');

% Helper lambdas
l = @(b,idx) get_param([mdl '/' b], 'PortHandles').LConn(idx);
r = @(b,idx) get_param([mdl '/' b], 'PortHandles').RConn(idx);
o = @(b,idx) get_param([mdl '/' b], 'PortHandles').Outport(idx);
i = @(b,idx) get_param([mdl '/' b], 'PortHandles').Inport(idx);

% Ground / solver / supplies
add_line(mdl, r('SolverCfg',1), l('GND',1), 'autorouting', 'on');
add_line(mdl, r('VPWM_STM32',1), l('GND',1), 'autorouting', 'on');
add_line(mdl, r('VBUS_24V',1), l('GND',1), 'autorouting', 'on');
add_line(mdl, r('VOP_12V',1), l('GND',1), 'autorouting', 'on');

% PWM -> RC DAC -> opamp + input
add_line(mdl, l('VPWM_STM32',1), l('R_FILT_10k',1), 'autorouting', 'on');
add_line(mdl, r('R_FILT_10k',1), l('C_FILT_1u',1), 'autorouting', 'on');
add_line(mdl, r('C_FILT_1u',1), l('GND',1), 'autorouting', 'on');
add_line(mdl, r('R_FILT_10k',1), l('TLV2372IDR',1), 'autorouting', 'on');

% Opamp supply and output to MOS gate
add_line(mdl, l('VOP_12V',1), l('R_VOP_SER_1',1), 'autorouting', 'on');
add_line(mdl, r('R_VOP_SER_1',1), l('TLV2372IDR',3), 'autorouting', 'on');
add_line(mdl, l('GND',1), l('TLV2372IDR',4), 'autorouting', 'on');
add_line(mdl, l('TLV2372IDR',5), l('R_GATE_10',1), 'autorouting', 'on');
add_line(mdl, r('R_GATE_10',1), l('IRFP260NPBF',2), 'autorouting', 'on');
add_line(mdl, r('R_GATE_10',1), l('R_GS_BLEED_100k',1), 'autorouting', 'on');
add_line(mdl, r('R_GS_BLEED_100k',1), l('IRFP260NPBF',3), 'autorouting', 'on');

% Current sink path: 24V -> RLOAD -> I sensor -> MOS drain -> MOS source -> Rsense -> GND
add_line(mdl, l('VBUS_24V',1), l('R_LOAD_10R',1), 'autorouting', 'on');
add_line(mdl, r('R_LOAD_10R',1), l('I_LED_Sensor',1), 'autorouting', 'on');
add_line(mdl, r('I_LED_Sensor',2), l('IRFP260NPBF',1), 'autorouting', 'on');
add_line(mdl, l('IRFP260NPBF',3), l('R_SENSE_0p1',1), 'autorouting', 'on');
add_line(mdl, r('R_SENSE_0p1',1), l('GND',1), 'autorouting', 'on');

% Feedback: Vsense to opamp - input
add_line(mdl, l('IRFP260NPBF',3), l('TLV2372IDR',2), 'autorouting', 'on');

% Sensors wiring
add_line(mdl, l('VPWM_STM32',1), l('V_PWM_Sensor',1), 'autorouting', 'on');
add_line(mdl, l('GND',1), r('V_PWM_Sensor',2), 'autorouting', 'on');

add_line(mdl, r('R_FILT_10k',1), l('V_REF_Sensor',1), 'autorouting', 'on');
add_line(mdl, l('GND',1), r('V_REF_Sensor',2), 'autorouting', 'on');

add_line(mdl, l('IRFP260NPBF',1), l('V_DS_Sensor',1), 'autorouting', 'on');
add_line(mdl, l('IRFP260NPBF',3), r('V_DS_Sensor',2), 'autorouting', 'on');

add_line(mdl, l('IRFP260NPBF',3), l('V_SENSE_Sensor',1), 'autorouting', 'on');
add_line(mdl, l('GND',1), r('V_SENSE_Sensor',2), 'autorouting', 'on');

% Sensor outputs -> converters -> mux -> scope
add_line(mdl, r('V_PWM_Sensor',1), l('PS2SL_PWM',1), 'autorouting', 'on');
add_line(mdl, r('V_REF_Sensor',1), l('PS2SL_VREF',1), 'autorouting', 'on');
add_line(mdl, r('V_DS_Sensor',1), l('PS2SL_VDS',1), 'autorouting', 'on');
add_line(mdl, r('V_SENSE_Sensor',1), l('PS2SL_VSENSE',1), 'autorouting', 'on');
add_line(mdl, r('I_LED_Sensor',1), l('PS2SL_ILED',1), 'autorouting', 'on');

add_line(mdl, o('PS2SL_PWM',1), i('Mux',1), 'autorouting', 'on');
add_line(mdl, o('PS2SL_VREF',1), i('Mux',2), 'autorouting', 'on');
add_line(mdl, o('PS2SL_VDS',1), i('Mux',3), 'autorouting', 'on');
add_line(mdl, o('PS2SL_VSENSE',1), i('Mux',4), 'autorouting', 'on');
add_line(mdl, o('PS2SL_ILED',1), i('Mux',5), 'autorouting', 'on');
add_line(mdl, o('Mux',1), i('Scope',1), 'autorouting', 'on');

save_system(mdl, fullfile(output_dir, [mdl '.slx']));
close_system(mdl);

% Export required custom Simscape library artifacts next to the model
copyfile(fullfile(models_dir, 'spiceparts_lib.slx'), fullfile(output_dir, 'spiceparts_lib.slx'), 'f');
dst_pkg = fullfile(output_dir, '+spiceparts');
if exist(dst_pkg, 'dir') == 7
    rmdir(dst_pkg, 's');
end
copyfile(fullfile(models_dir, '+spiceparts'), dst_pkg);

fprintf('Generated model: %s\\n', fullfile(output_dir, [mdl '.slx']));
end

function ensure_spiceparts_library(models_dir)
orig = pwd;
cleanupObj = onCleanup(@() cd(orig));
cd(models_dir);

if exist('+spiceparts', 'dir') ~= 7
    mkdir('+spiceparts');
end

tlv_file = fullfile(models_dir, 'tlv2372_sloc063', 'TLV2372_PSPICE_AIO', 'TLV2372.LIB');
mos_file = fullfile(models_dir, 'IRFP260NPBF_subckt.lib');

if exist(tlv_file, 'file') ~= 2
    error('Missing TLV2372 SPICE file: %s', tlv_file);
end
if exist(mos_file, 'file') ~= 2
    error('Missing IRFP260NPBF SPICE file: %s', mos_file);
end

% Regenerate components to keep in sync with model files
subcircuit2ssc(tlv_file, '+spiceparts', 'TLV2372');
subcircuit2ssc(mos_file, '+spiceparts', 'IRFP260NPBF');
ssc_build('spiceparts');
end

function blk = find_block_by_name(lib, name, mustContain)
blks = find_system(lib, 'LookUnderMasks', 'all', 'FollowLinks', 'on', 'Name', name);
if isempty(blks)
    error('Cannot find block "%s" in library %s', name, lib);
end
if isempty(mustContain)
    blk = blks{1};
    return;
end
for k = 1:numel(blks)
    if contains(blks{k}, mustContain)
        blk = blks{k};
        return;
    end
end
blk = blks{1};
end
