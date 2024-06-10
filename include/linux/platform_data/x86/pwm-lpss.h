/* SPDX-License-Identifier: GPL-2.0-only */
/* Intel Low Power Subsystem PWM controller driver */

#ifndef __PLATFORM_DATA_X86_PWM_LPSS_H
#define __PLATFORM_DATA_X86_PWM_LPSS_H

#include <linux/types.h>

struct device;

struct pwm_lpss_chip;

struct pwm_lpss_boardinfo {
	unsigned long clk_rate;
	unsigned int npwm;
	unsigned long base_unit_bits;
	/*
	 * Some versions of the IP may stuck in the state machine if enable
	 * bit is not set, and hence update bit will show busy status till
	 * the reset. For the rest it may be otherwise.
	 */
	bool bypass;
	/*
	 * On some devices the _PS0/_PS3 AML code of the GPU (GFX0) device
	 * messes with the PWM0 controllers state,
	 */
	bool other_devices_aml_touches_pwm_regs;
};

struct pwm_chip *devm_pwm_lpss_probe(struct device *dev, void __iomem *base,
				     const struct pwm_lpss_boardinfo *info);

#endif	/* __PLATFORM_DATA_X86_PWM_LPSS_H */
