#include "program.h"
#include "gpio.h"
#include "tim.h"
#include "adc.h"

#define ADC_BUFFER_LENGTH        2U
#define ADC_BUF_VOUT_INDEX       0U
#define ADC_BUF_VDS_INDEX        1U

#define ADC_FULL_SCALE_COUNTS    4095U
#define ADC_VREF_MV              3300U
#define ADC_RAW_LPF_SHIFT        4U
#define ADC_CAL_SCALE_DEN        10000U

/* 分压还原比例：
   Vout = Vout_pin * 16      (150k:10k)
   Vds  = Vds_pin  * 115/15  (100k:15k) */
#define VOUT_DIVIDER_NUM         16U
#define VOUT_DIVIDER_DEN         1U
#define VDS_DIVIDER_NUM          115U
#define VDS_DIVIDER_DEN          15U
/* 三点标定系数 / 3-point fit:
   Vout raw: 1830/1522/1358 -> 23.390/19.473/17.384V
   Vds  raw: 3830/3186/2845 -> 23.390/19.473/17.384V */
#define VOUT_CAL_SLOPE_NUM       127237U
#define VOUT_CAL_OFFSET_NUM      1061722U
#define VDS_CAL_SLOPE_NUM        60955U
#define VDS_CAL_OFFSET_NUM       464546U

volatile uint16_t adc_buffer[ADC_BUFFER_LENGTH] = {0U, 0U};
volatile uint16_t vout_adc_raw = 0U;
volatile uint16_t vds_adc_raw = 0U;
volatile uint16_t vout_adc_raw_lpf = 0U;
volatile uint16_t vds_adc_raw_lpf = 0U;
volatile uint32_t vout_pin_mv = 0U;
volatile uint32_t vds_pin_mv = 0U;
volatile uint32_t vout_mv = 0U;
volatile uint32_t vds_mv = 0U;
volatile uint32_t vout_mv_lpf = 0U;
volatile uint32_t vds_mv_lpf = 0U;
volatile boost_voltage_debug_t boost_voltage_debug = {0U, 0U, 0};

/* 调试计数器改为32位，避免2 kHz节拍下很快回卷。 */
volatile uint32_t tim14_cnt = 0U;
volatile uint32_t adc1_cnt = 0U;
volatile uint32_t adc_start_busy_cnt = 0U;

/* 主循环里看的Boost占空比语义固定为“下管有效占空比” [permille]。 */
volatile uint16_t boost_duty_permille = 0U;
volatile uint16_t light_dac_output_permille = 0U;
volatile uint16_t light_dac_compare_counts = 0U;

/* 中断里只做采样搬运，状态机仍放在主循环，避免ADC回调被业务逻辑拖长。 */
static volatile uint8_t adc_dma_busy = 0U;
static volatile uint8_t adc_data_ready = 0U;
static uint8_t adc_raw_lpf_inited = 0U;
static uint32_t vout_adc_raw_lpf_q8 = 0U;
static uint32_t vds_adc_raw_lpf_q8 = 0U;

/* ADC码值[LSB]转分压点电压[mV]；坚持整数运算，避开F030的软件浮点开销。 */
static uint32_t adc_raw_to_pin_mv(uint16_t raw)
{
    return (((uint32_t)raw * ADC_VREF_MV) + (ADC_FULL_SCALE_COUNTS / 2U)) /
           ADC_FULL_SCALE_COUNTS;
}

/* 按分压比还原真实电压[mV]；保持32位整数范围，给后续闭环留安全余量。 */
/* 线性拟合标定直接输出工程量[mV]，补偿分压和基准误差，方便无闭环阶段先把读数对准。 */
static uint32_t adc_raw_to_calibrated_mv(uint16_t raw, uint32_t slope_num, uint32_t offset_num)
{
    return ((((uint32_t)raw * slope_num) + offset_num) + (ADC_CAL_SCALE_DEN / 2U)) /
           ADC_CAL_SCALE_DEN;
}

/* raw低通只服务调试标定；Q8保留亚LSB分辨率，避免小信号拟合时滤波值台阶太粗。 */
static uint16_t adc_raw_lpf_step(uint16_t raw, uint32_t *state_q8)
{
    uint32_t raw_q8;

    raw_q8 = ((uint32_t)raw) << 8;

    if (raw_q8 >= *state_q8) {
        *state_q8 += (raw_q8 - *state_q8) >> ADC_RAW_LPF_SHIFT;
    } else {
        *state_q8 -= (*state_q8 - raw_q8) >> ADC_RAW_LPF_SHIFT;
    }

    return (uint16_t)((*state_q8 + 0x80U) >> 8);
}

/* 应用目标下管有效占空比[permille]；前提：TIM1互补PWM已启动。 */
void boost_set_low_side_duty_permille(uint16_t duty_permille)
{
    uint32_t tim1_counts;
    uint32_t low_side_counts;

    if (duty_permille > 1000U) {
        /* 防过调制，避免异常控制量直接冲击驱动器。 */
        duty_permille = 1000U;
    }

    boost_duty_permille = duty_permille;
    tim1_counts = (uint32_t)__HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;
    low_side_counts = ((uint32_t)boost_duty_permille * tim1_counts + 500U) / 1000U;

    /* 项目约定：Boost有效占空比看下管；当前PWM硬件是以上管比较值生成，所以这里直接反相。 */
    __HAL_TIM_SET_COMPARE(&htim1,
                          TIM_CHANNEL_3,
                          (uint16_t)(tim1_counts - low_side_counts));
}

/* DAC参考占空比用permille表示，避免F030在调光路径里做软件浮点。 */
void light_set_dac_duty_permille(uint16_t duty_permille)
{
    uint32_t tim3_counts;
    uint32_t compare_counts;

    if (duty_permille > 1000U) {
        /* 限幅到100%，防止RC-DAC参考点被写出物理范围。 */
        duty_permille = 1000U;
    }

    light_dac_output_permille = duty_permille;
    tim3_counts = (uint32_t)__HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
    compare_counts = ((uint32_t)light_dac_output_permille * tim3_counts + 500U) / 1000U;

    if (compare_counts > 0xFFFFU) {
        compare_counts = 0xFFFFU;
    }

    light_dac_compare_counts = (uint16_t)compare_counts;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, light_dac_compare_counts);
}

/* 把一整组ADC结果更新成mV工程量；前提：DMA已完成两路扫描。 */
static void adc_update_measurements(void)
{
    vout_adc_raw = adc_buffer[ADC_BUF_VOUT_INDEX];
    vds_adc_raw = adc_buffer[ADC_BUF_VDS_INDEX];

    if (adc_raw_lpf_inited == 0U) {
        /* 首次采样直接对齐当前raw，避免调试时把滤波启动过渡误当成真实输入响应。 */
        adc_raw_lpf_inited = 1U;
        vout_adc_raw_lpf_q8 = ((uint32_t)vout_adc_raw) << 8;
        vds_adc_raw_lpf_q8 = ((uint32_t)vds_adc_raw) << 8;
        vout_adc_raw_lpf = vout_adc_raw;
        vds_adc_raw_lpf = vds_adc_raw;
    } else {
        vout_adc_raw_lpf = adc_raw_lpf_step(vout_adc_raw, &vout_adc_raw_lpf_q8);
        vds_adc_raw_lpf = adc_raw_lpf_step(vds_adc_raw, &vds_adc_raw_lpf_q8);
    }

    vout_pin_mv = adc_raw_to_pin_mv(vout_adc_raw);
    vds_pin_mv = adc_raw_to_pin_mv(vds_adc_raw);

    vout_mv = adc_raw_to_calibrated_mv(vout_adc_raw,
                                       VOUT_CAL_SLOPE_NUM,
                                       VOUT_CAL_OFFSET_NUM);
    vds_mv = adc_raw_to_calibrated_mv(vds_adc_raw,
                                      VDS_CAL_SLOPE_NUM,
                                      VDS_CAL_OFFSET_NUM);
    vout_mv_lpf = adc_raw_to_calibrated_mv(vout_adc_raw_lpf,
                                           VOUT_CAL_SLOPE_NUM,
                                           VOUT_CAL_OFFSET_NUM);
    vds_mv_lpf = adc_raw_to_calibrated_mv(vds_adc_raw_lpf,
                                          VDS_CAL_SLOPE_NUM,
                                          VDS_CAL_OFFSET_NUM);

    /* Debug镜像统一用mV，positive delta means Vout > Vds，便于直接观察Boost余量。 */
    /* Debug镜像跟随闭环反馈[mV]，这样你在调试器里看到的就是PI真正看到的量。 */
    boost_voltage_debug.vout_mv = vout_mv_lpf;
    boost_voltage_debug.vds_mv = vds_mv_lpf;
    boost_voltage_debug.vout_minus_vds_mv = (int32_t)vout_mv_lpf - (int32_t)vds_mv_lpf;
}

/* 安全拉起功率级相关外设；调用前必须已经完成GPIO/ADC/DMA/TIM初始化。 */
void hardware_config(void)
{
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_RESET);

    HAL_TIM_Base_Start_IT(&htim14);

    /* PWM先启动但保持零输出，保证状态机真正接管前功率级仍处于待机。 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
    boost_set_low_side_duty_permille(0U);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    light_set_dac_duty_permille(0U);
}

/* 主循环里的控制任务入口；只在有新ADC数据时驱动一次状态机。 */
void program_task(void)
{
    if (adc_data_ready != 0U) {
        adc_data_ready = 0U;
        State_M();
    }
}

/* 周期采样触发；前提：TIM14已配置并使能中断。 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM14) {
        tim14_cnt++;

        if (adc_dma_busy == 0U) {
            if (HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc_buffer, ADC_BUFFER_LENGTH) == HAL_OK) {
                adc_dma_busy = 1U;
            } else {
                /* 留下软件痕迹，便于排查DMA忙冲突而不是静默丢采样。 */
                adc_start_busy_cnt++;
            }
        }
    }
}

/* ADC完成回调；前提：ADC1 DMA为Normal模式且固定两路扫描。 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        adc1_cnt++;
        adc_update_measurements();
        adc_data_ready = 1U;
        HAL_ADC_Stop_DMA(hadc);
        adc_dma_busy = 0U;
    }
}

//
// End of File
//
