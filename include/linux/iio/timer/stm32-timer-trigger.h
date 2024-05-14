/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 */

#ifndef _STM32_TIMER_TRIGGER_H_
#define _STM32_TIMER_TRIGGER_H_

#define TIM1_TRGO	"tim1_trgo"
#define TIM1_TRGO2	"tim1_trgo2"
#define TIM1_CH1	"tim1_ch1"
#define TIM1_CH2	"tim1_ch2"
#define TIM1_CH3	"tim1_ch3"
#define TIM1_CH4	"tim1_ch4"

#define TIM2_TRGO	"tim2_trgo"
#define TIM2_CH1	"tim2_ch1"
#define TIM2_CH2	"tim2_ch2"
#define TIM2_CH3	"tim2_ch3"
#define TIM2_CH4	"tim2_ch4"

#define TIM3_TRGO	"tim3_trgo"
#define TIM3_CH1	"tim3_ch1"
#define TIM3_CH2	"tim3_ch2"
#define TIM3_CH3	"tim3_ch3"
#define TIM3_CH4	"tim3_ch4"

#define TIM4_TRGO	"tim4_trgo"
#define TIM4_CH1	"tim4_ch1"
#define TIM4_CH2	"tim4_ch2"
#define TIM4_CH3	"tim4_ch3"
#define TIM4_CH4	"tim4_ch4"

#define TIM5_TRGO	"tim5_trgo"
#define TIM5_CH1	"tim5_ch1"
#define TIM5_CH2	"tim5_ch2"
#define TIM5_CH3	"tim5_ch3"
#define TIM5_CH4	"tim5_ch4"

#define TIM6_TRGO	"tim6_trgo"

#define TIM7_TRGO	"tim7_trgo"

#define TIM8_TRGO	"tim8_trgo"
#define TIM8_TRGO2	"tim8_trgo2"
#define TIM8_CH1	"tim8_ch1"
#define TIM8_CH2	"tim8_ch2"
#define TIM8_CH3	"tim8_ch3"
#define TIM8_CH4	"tim8_ch4"

#define TIM9_TRGO	"tim9_trgo"
#define TIM9_CH1	"tim9_ch1"
#define TIM9_CH2	"tim9_ch2"

#define TIM10_OC1	"tim10_oc1"

#define TIM11_OC1	"tim11_oc1"

#define TIM12_TRGO	"tim12_trgo"
#define TIM12_CH1	"tim12_ch1"
#define TIM12_CH2	"tim12_ch2"

#define TIM13_OC1	"tim13_oc1"

#define TIM14_OC1	"tim14_oc1"

#define TIM15_TRGO	"tim15_trgo"

#define TIM16_OC1	"tim16_oc1"

#define TIM17_OC1	"tim17_oc1"

#if IS_REACHABLE(CONFIG_IIO_STM32_TIMER_TRIGGER)
bool is_stm32_timer_trigger(struct iio_trigger *trig);
#else
static inline bool is_stm32_timer_trigger(struct iio_trigger *trig)
{
#if IS_ENABLED(CONFIG_IIO_STM32_TIMER_TRIGGER)
	pr_warn_once("stm32-timer-trigger not linked in\n");
#endif
	return false;
}
#endif
#endif
