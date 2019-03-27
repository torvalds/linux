/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#ifdef AH_SUPPORT_AR5312

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ar5312/ar5312.h"
#include "ar5312/ar5312reg.h"
#include "ar5312/ar5312phy.h"

#define	AR_NUM_GPIO	6		/* 6 GPIO pins */
#define	AR5312_GPIOD_MASK	0x0000002F	/* GPIO data reg r/w mask */

/*
 * Configure GPIO Output lines
 */
HAL_BOOL
ar5312GpioCfgOutput(struct ath_hal *ah, uint32_t gpio, HAL_GPIO_MUX_TYPE type)
{
	uint32_t gpioOffset = (AR5312_GPIO_BASE - ((uint32_t) ah->ah_sh));

	HALASSERT(gpio < AR_NUM_GPIO);

	OS_REG_WRITE(ah, gpioOffset+AR5312_GPIOCR,
		  (OS_REG_READ(ah, gpioOffset+AR5312_GPIOCR) &~ AR_GPIOCR_CR_A(gpio))
		| AR_GPIOCR_CR_A(gpio));

	return AH_TRUE;
}

/*
 * Configure GPIO Input lines
 */
HAL_BOOL
ar5312GpioCfgInput(struct ath_hal *ah, uint32_t gpio)
{
	uint32_t gpioOffset = (AR5312_GPIO_BASE - ((uint32_t) ah->ah_sh));

	HALASSERT(gpio < AR_NUM_GPIO);

	OS_REG_WRITE(ah, gpioOffset+AR5312_GPIOCR,
		  (OS_REG_READ(ah, gpioOffset+AR5312_GPIOCR) &~ AR_GPIOCR_CR_A(gpio))
		| AR_GPIOCR_CR_N(gpio));

	return AH_TRUE;
}

/*
 * Once configured for I/O - set output lines
 */
HAL_BOOL
ar5312GpioSet(struct ath_hal *ah, uint32_t gpio, uint32_t val)
{
	uint32_t reg;
        uint32_t gpioOffset = (AR5312_GPIO_BASE - ((uint32_t) ah->ah_sh));

	HALASSERT(gpio < AR_NUM_GPIO);

	reg =  OS_REG_READ(ah, gpioOffset+AR5312_GPIODO);
	reg &= ~(1 << gpio);
	reg |= (val&1) << gpio;

	OS_REG_WRITE(ah, gpioOffset+AR5312_GPIODO, reg);
	return AH_TRUE;
}

/*
 * Once configured for I/O - get input lines
 */
uint32_t
ar5312GpioGet(struct ath_hal *ah, uint32_t gpio)
{
	uint32_t gpioOffset = (AR5312_GPIO_BASE - ((uint32_t) ah->ah_sh));

	if (gpio < AR_NUM_GPIO) {
		uint32_t val = OS_REG_READ(ah, gpioOffset+AR5312_GPIODI);
		val = ((val & AR5312_GPIOD_MASK) >> gpio) & 0x1;
		return val;
	} else {
		return 0xffffffff;
	}
}

/*
 * Set the GPIO Interrupt
 */
void
ar5312GpioSetIntr(struct ath_hal *ah, u_int gpio, uint32_t ilevel)
{
	uint32_t val;
        uint32_t gpioOffset = (AR5312_GPIO_BASE - ((uint32_t) ah->ah_sh));

	/* XXX bounds check gpio */
	val = OS_REG_READ(ah, gpioOffset+AR5312_GPIOCR);
	val &= ~(AR_GPIOCR_CR_A(gpio) |
		 AR_GPIOCR_INT_MASK | AR_GPIOCR_INT_ENA | AR_GPIOCR_INT_SEL);
	val |= AR_GPIOCR_CR_N(gpio) | AR_GPIOCR_INT(gpio) | AR_GPIOCR_INT_ENA;
	if (ilevel)
		val |= AR_GPIOCR_INT_SELH;	/* interrupt on pin high */
	else
		val |= AR_GPIOCR_INT_SELL;	/* interrupt on pin low */

	/* Don't need to change anything for low level interrupt. */
	OS_REG_WRITE(ah, gpioOffset+AR5312_GPIOCR, val);

	/* Change the interrupt mask. */
	(void) ar5212SetInterrupts(ah, AH5212(ah)->ah_maskReg | HAL_INT_GPIO);
}


#endif /* AH_SUPPORT_AR5312 */
