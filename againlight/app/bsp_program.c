#include "bsp_program.h"
#include "adc.h"
#include "tim.h"

uint16_t adc_buffer[BSP_ADC_CHANNEL_COUNT] = {0};
volatile uint32_t g_tim1_irq_count = 0;
volatile uint32_t g_adc_dma_tc_count = 0;
uint8_t test_fan = 0;
void hardware_config(void)
{

    HAL_ADCEx_Calibration_Start(&hadc);
    HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc_buffer, BSP_ADC_CHANNEL_COUNT);

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 500);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 250);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 750);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
	
    HAL_TIM_Base_Start_IT(&htim1);

}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        g_adc_dma_tc_count++;
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
			if(test_fan == 0){HAL_GPIO_WritePin(FAN_CTL_GPIO_Port,FAN_CTL_Pin,GPIO_PIN_RESET);}
			else{HAL_GPIO_WritePin(FAN_CTL_GPIO_Port,FAN_CTL_Pin,GPIO_PIN_SET);}
        g_tim1_irq_count++;
    }
}


//
// End of File
//
