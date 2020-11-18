/* SPDX-License-Identifier: GPL-2.0 */
/*
 * STM32 Low-Power Timer parent driver.
 * Copyright (C) STMicroelectronics 2017
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>
 * Inspired by Benjamin Gaignard's stm32-timers driver
 */

#ifndef _LINUX_STM32_LPTIMER_H_
#define _LINUX_STM32_LPTIMER_H_

#include <linux/clk.h>
#include <linux/regmap.h>

#define STM32_LPTIM_ISR		0x00	/* Interrupt and Status Reg  */
#define STM32_LPTIM_ICR		0x04	/* Interrupt Clear Reg       */
#define STM32_LPTIM_IER		0x08	/* Interrupt Enable Reg      */
#define STM32_LPTIM_CFGR	0x0C	/* Configuration Reg         */
#define STM32_LPTIM_CR		0x10	/* Control Reg               */
#define STM32_LPTIM_CMP		0x14	/* Compare Reg               */
#define STM32_LPTIM_ARR		0x18	/* Autoreload Reg            */
#define STM32_LPTIM_CNT		0x1C	/* Counter Reg               */

/* STM32_LPTIM_ISR - bit fields */
#define STM32_LPTIM_CMPOK_ARROK		GENMASK(4, 3)
#define STM32_LPTIM_ARROK		BIT(4)
#define STM32_LPTIM_CMPOK		BIT(3)

/* STM32_LPTIM_ICR - bit fields */
#define STM32_LPTIM_ARRMCF		BIT(1)
#define STM32_LPTIM_CMPOKCF_ARROKCF	GENMASK(4, 3)

/* STM32_LPTIM_IER - bit flieds */
#define STM32_LPTIM_ARRMIE	BIT(1)

/* STM32_LPTIM_CR - bit fields */
#define STM32_LPTIM_CNTSTRT	BIT(2)
#define STM32_LPTIM_SNGSTRT	BIT(1)
#define STM32_LPTIM_ENABLE	BIT(0)

/* STM32_LPTIM_CFGR - bit fields */
#define STM32_LPTIM_ENC		BIT(24)
#define STM32_LPTIM_COUNTMODE	BIT(23)
#define STM32_LPTIM_WAVPOL	BIT(21)
#define STM32_LPTIM_PRESC	GENMASK(11, 9)
#define STM32_LPTIM_CKPOL	GENMASK(2, 1)

/* STM32_LPTIM_ARR */
#define STM32_LPTIM_MAX_ARR	0xFFFF

/**
 * struct stm32_lptimer - STM32 Low-Power Timer data assigned by parent device
 * @clk: clock reference for this instance
 * @regmap: register map reference for this instance
 * @has_encoder: indicates this Low-Power Timer supports encoder mode
 */
struct stm32_lptimer {
	struct clk *clk;
	struct regmap *regmap;
	bool has_encoder;
};

#endif
