#ifndef BSP_PROGRAM_H
#define BSP_PROGRAM_H

#include "stm32f0xx_hal.h"

#define BSP_ADC_CHANNEL_COUNT (2U)

void hardware_config(void);
extern uint16_t adc_buffer[BSP_ADC_CHANNEL_COUNT];

#endif // BSP_PROGRAM_H
//
// End of File
//
