/*
 * TI/National Semiconductor LP3943 Device
 *
 * Copyright 2013 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __MFD_LP3943_H__
#define __MFD_LP3943_H__

#include <linux/gpio.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

/* Registers */
#define LP3943_REG_GPIO_A		0x00
#define LP3943_REG_GPIO_B		0x01
#define LP3943_REG_PRESCALE0		0x02
#define LP3943_REG_PWM0			0x03
#define LP3943_REG_PRESCALE1		0x04
#define LP3943_REG_PWM1			0x05
#define LP3943_REG_MUX0			0x06
#define LP3943_REG_MUX1			0x07
#define LP3943_REG_MUX2			0x08
#define LP3943_REG_MUX3			0x09

/* Bit description for LP3943_REG_MUX0 ~ 3 */
#define LP3943_GPIO_IN			0x00
#define LP3943_GPIO_OUT_HIGH		0x00
#define LP3943_GPIO_OUT_LOW		0x01
#define LP3943_DIM_PWM0			0x02
#define LP3943_DIM_PWM1			0x03

#define LP3943_NUM_PWMS			2

enum lp3943_pwm_output {
	LP3943_PWM_OUT0,
	LP3943_PWM_OUT1,
	LP3943_PWM_OUT2,
	LP3943_PWM_OUT3,
	LP3943_PWM_OUT4,
	LP3943_PWM_OUT5,
	LP3943_PWM_OUT6,
	LP3943_PWM_OUT7,
	LP3943_PWM_OUT8,
	LP3943_PWM_OUT9,
	LP3943_PWM_OUT10,
	LP3943_PWM_OUT11,
	LP3943_PWM_OUT12,
	LP3943_PWM_OUT13,
	LP3943_PWM_OUT14,
	LP3943_PWM_OUT15,
};

/*
 * struct lp3943_pwm_map
 * @output: Output pins which are mapped to each PWM channel
 * @num_outputs: Number of outputs
 */
struct lp3943_pwm_map {
	enum lp3943_pwm_output *output;
	int num_outputs;
};

/*
 * struct lp3943_platform_data
 * @pwms: Output channel definitions for PWM channel 0 and 1
 */
struct lp3943_platform_data {
	struct lp3943_pwm_map *pwms[LP3943_NUM_PWMS];
};

/*
 * struct lp3943_reg_cfg
 * @reg: Register address
 * @mask: Register bit mask to be updated
 * @shift: Register bit shift
 */
struct lp3943_reg_cfg {
	u8 reg;
	u8 mask;
	u8 shift;
};

/*
 * struct lp3943
 * @dev: Parent device pointer
 * @regmap: Used for I2C communication on accessing registers
 * @pdata: LP3943 platform specific data
 * @mux_cfg: Register configuration for pin MUX
 * @pin_used: Bit mask for output pin used.
 *	      This bitmask is used for pin assignment management.
 *	      1 = pin used, 0 = available.
 *	      Only LSB 16 bits are used, but it is unsigned long type
 *	      for atomic bitwise operations.
 */
struct lp3943 {
	struct device *dev;
	struct regmap *regmap;
	struct lp3943_platform_data *pdata;
	const struct lp3943_reg_cfg *mux_cfg;
	unsigned long pin_used;
};

int lp3943_read_byte(struct lp3943 *lp3943, u8 reg, u8 *read);
int lp3943_write_byte(struct lp3943 *lp3943, u8 reg, u8 data);
int lp3943_update_bits(struct lp3943 *lp3943, u8 reg, u8 mask, u8 data);
#endif
