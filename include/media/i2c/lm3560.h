/*
 * include/media/i2c/lm3560.h
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * Contact: Daniel Jeong <gshark.jeong@gmail.com>
 *			Ldd-Mlp <ldd-mlp@list.ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef __LM3560_H__
#define __LM3560_H__

#include <media/v4l2-subdev.h>

#define LM3560_NAME	"lm3560"
#define LM3560_I2C_ADDR	(0x53)

/*  FLASH Brightness
 *	min 62500uA, step 62500uA, max 1000000uA
 */
#define LM3560_FLASH_BRT_MIN 62500
#define LM3560_FLASH_BRT_STEP 62500
#define LM3560_FLASH_BRT_MAX 1000000
#define LM3560_FLASH_BRT_uA_TO_REG(a)	\
	((a) < LM3560_FLASH_BRT_MIN ? 0 :	\
	 (((a) - LM3560_FLASH_BRT_MIN) / LM3560_FLASH_BRT_STEP))
#define LM3560_FLASH_BRT_REG_TO_uA(a)		\
	((a) * LM3560_FLASH_BRT_STEP + LM3560_FLASH_BRT_MIN)

/*  FLASH TIMEOUT DURATION
 *	min 32ms, step 32ms, max 1024ms
 */
#define LM3560_FLASH_TOUT_MIN 32
#define LM3560_FLASH_TOUT_STEP 32
#define LM3560_FLASH_TOUT_MAX 1024
#define LM3560_FLASH_TOUT_ms_TO_REG(a)	\
	((a) < LM3560_FLASH_TOUT_MIN ? 0 :	\
	 (((a) - LM3560_FLASH_TOUT_MIN) / LM3560_FLASH_TOUT_STEP))
#define LM3560_FLASH_TOUT_REG_TO_ms(a)		\
	((a) * LM3560_FLASH_TOUT_STEP + LM3560_FLASH_TOUT_MIN)

/*  TORCH BRT
 *	min 31250uA, step 31250uA, max 250000uA
 */
#define LM3560_TORCH_BRT_MIN 31250
#define LM3560_TORCH_BRT_STEP 31250
#define LM3560_TORCH_BRT_MAX 250000
#define LM3560_TORCH_BRT_uA_TO_REG(a)	\
	((a) < LM3560_TORCH_BRT_MIN ? 0 :	\
	 (((a) - LM3560_TORCH_BRT_MIN) / LM3560_TORCH_BRT_STEP))
#define LM3560_TORCH_BRT_REG_TO_uA(a)		\
	((a) * LM3560_TORCH_BRT_STEP + LM3560_TORCH_BRT_MIN)

enum lm3560_led_id {
	LM3560_LED0 = 0,
	LM3560_LED1,
	LM3560_LED_MAX
};

enum lm3560_peak_current {
	LM3560_PEAK_1600mA = 0x00,
	LM3560_PEAK_2300mA = 0x20,
	LM3560_PEAK_3000mA = 0x40,
	LM3560_PEAK_3600mA = 0x60
};

/* struct lm3560_platform_data
 *
 * @peak :  peak current
 * @max_flash_timeout: flash timeout
 * @max_flash_brt: flash mode led brightness
 * @max_torch_brt: torch mode led brightness
 */
struct lm3560_platform_data {
	enum lm3560_peak_current peak;

	u32 max_flash_timeout;
	u32 max_flash_brt[LM3560_LED_MAX];
	u32 max_torch_brt[LM3560_LED_MAX];
};

#endif /* __LM3560_H__ */
