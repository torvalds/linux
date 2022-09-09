/* SPDX-License-Identifier: GPL-2.0 */
// TI LMU Common Core
// Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/

#ifndef _TI_LMU_COMMON_H_
#define _TI_LMU_COMMON_H_

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

#define LMU_11BIT_LSB_MASK	(BIT(0) | BIT(1) | BIT(2))
#define LMU_11BIT_MSB_SHIFT	3

#define MAX_BRIGHTNESS_8BIT	255
#define MAX_BRIGHTNESS_11BIT	2047

struct ti_lmu_bank {
	struct regmap *regmap;

	int max_brightness;

	u8 lsb_brightness_reg;
	u8 msb_brightness_reg;

	u8 runtime_ramp_reg;
	u32 ramp_up_usec;
	u32 ramp_down_usec;
};

int ti_lmu_common_set_brightness(struct ti_lmu_bank *lmu_bank, int brightness);

int ti_lmu_common_set_ramp(struct ti_lmu_bank *lmu_bank);

int ti_lmu_common_get_ramp_params(struct device *dev,
				  struct fwnode_handle *child,
				  struct ti_lmu_bank *lmu_data);

int ti_lmu_common_get_brt_res(struct device *dev, struct fwnode_handle *child,
			      struct ti_lmu_bank *lmu_data);

#endif /* _TI_LMU_COMMON_H_ */
