/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) STMicroelectronics 2017
 *
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>
 */

#ifndef _STM32_LPTIM_TRIGGER_H_
#define _STM32_LPTIM_TRIGGER_H_

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

#define LPTIM1_OUT	"lptim1_out"
#define LPTIM2_OUT	"lptim2_out"
#define LPTIM3_OUT	"lptim3_out"
#define LPTIM4_OUT	"lptim4_out"
#define LPTIM5_OUT	"lptim5_out"

#define LPTIM1_CH1	"lptim1_ch1"
#define LPTIM1_CH2	"lptim1_ch2"
#define LPTIM2_CH1	"lptim2_ch1"
#define LPTIM2_CH2	"lptim2_ch2"
#define LPTIM3_CH1	"lptim3_ch1"
#define LPTIM4_CH1	"lptim4_ch1"

#if IS_REACHABLE(CONFIG_IIO_STM32_LPTIMER_TRIGGER)
bool is_stm32_lptim_trigger(struct iio_trigger *trig);
#else
static inline bool is_stm32_lptim_trigger(struct iio_trigger *trig)
{
#if IS_ENABLED(CONFIG_IIO_STM32_LPTIMER_TRIGGER)
	pr_warn_once("stm32 lptim_trigger not linked in\n");
#endif
	return false;
}
#endif
#endif
