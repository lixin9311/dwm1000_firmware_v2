/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c
  * @author  MCD Application Team
  * @version V3.4.0
  * @date    10/15/2010
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @copy
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2010 STMicroelectronics</center></h2>
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "deca_device_api.h"
#include "stm32f10x.h"
#include "deca_types.h"
#include "port.h"
#include "instance.h"

/* Tick timer count. */
volatile unsigned long time32_incr;
uint8 usart_status;
uint8 usart_rx_buffer[USART_BUFFER_LEN];
uint16 usart_index;
void SysTick_Handler(void)
{
    time32_incr++;
}

void EXTI0_IRQHandler(void) {
  // printf2("int\r\n");
  do{
    dwt_isr();
  }while(GPIO_ReadInputDataBit(DECAIRQ_GPIO, DECAIRQ) == 1);
  EXTI_ClearITPendingBit(DECAIRQ_EXTI);
  // printf2("int out\r\n");
}

void TIM4_IRQHandler(void) {
	if (TIM_GetITStatus(TIM4 , TIM_IT_Update) != RESET) {
		TIM_ClearITPendingBit(TIM4 , TIM_FLAG_Update);
		usart_status = 2;
		TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);
		TIM_SetCounter(TIM4, 0x0000);
		TIM_Cmd(TIM4, DISABLE);
		usart_handle();
	}
}

void TIM2_IRQHandler(void) {
  static uint8 id;
	if(TIM_GetITStatus(TIM2 , TIM_IT_Update) != RESET) {
    TIM_ClearITPendingBit(TIM2 , TIM_FLAG_Update);
    tdoa_beacon(id);
    if (id == 0xff) {
      id = 0;
    } else {
      id++;
    }
	}
}

void enable_auto_beacon(void) {
  TIM_ClearFlag(TIM2, TIM_FLAG_Update);
  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
  TIM_Cmd(TIM2, ENABLE);
}

void disable_auto_beacon(void) {
  TIM_ClearITPendingBit(TIM2 , TIM_FLAG_Update);
  TIM_ITConfig(TIM2, TIM_IT_Update, DISABLE);
  TIM_SetCounter(TIM2, 0x0000);
  TIM_Cmd(TIM2, DISABLE);
}

void USART1_IRQHandler(void) {
	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
		if (usart_status == 0) {
			usart_status = 1;

			TIM_ClearFlag(TIM4, TIM_FLAG_Update);
			TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
			TIM_Cmd(TIM4, ENABLE);

			usart_rx_buffer[usart_index++] = USART1->DR;
		} else if (usart_status == 1) {
			TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);
			TIM_SetCounter(TIM4, 0x0000);
			TIM_ClearFlag(TIM4, TIM_FLAG_Update);
			TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

			usart_rx_buffer[usart_index++] = USART1->DR;
			if (usart_index == USART_BUFFER_LEN) {
				TIM4_IRQHandler();
			}
		}
	}
}

/******************* (C) COPYRIGHT 2010 STMicroelectronics *****END OF FILE****/
