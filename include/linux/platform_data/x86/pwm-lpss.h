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
	 * NOTE:
	 * Intel Broxton, Apollo Lake, and Gemini Lake have different programming flow.
	 *
	 * Initial Enable or First Activation
	 * 1. Program the base unit and on time divisor values.
	 * 2. Set the software update bit.
	 * 3. Poll in a loop on the PWMCTRL bit until software update bit is cleared.+
	 * 4. Enable the PWM output by setting PWM Enable.
	 * 5. Repeat the above steps for the next PWM Module.
	 *
	 * Dynamic update while PWM is Enabled
	 * 1. Program the base unit and on-time divisor values.
	 * 2. Set the software update bit.
	 * 3. Repeat the above steps for the next PWM module.
	 *
	 * + After setting PWMCTRL register's SW update bit, hardware automatically
	 * deasserts the SW update bit after a brief delay. It was observed that
	 * setting of PWM enable is typically done via read-modify-write of the PWMCTRL
	 * register. If there is no/little delay between setting software update bit
	 * and setting enable bit via read-modify-write, it is possible that the read
	 * could return with software enable as 1. In that case, the last write to set
	 * enable to 1 could also set sw_update to 1. If this happens, sw_update gets
	 * stuck and the driver code can hang as it explicitly waits for sw_update bit
	 * to be 0 after setting the enable bit to 1. To avoid this race condition,
	 * SW should poll on the software update bit to make sure that it is 0 before
	 * doing the read-modify-write to set the enable bit to 1.
	 *
	 * Also, we noted that if sw_update bit was set in step #1 above then when it
	 * is set again in step #2, sw_update bit never gets cleared and the flow hangs.
	 * As such, we need to make sure that sw_update bit is 0 when doing step #1.
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
