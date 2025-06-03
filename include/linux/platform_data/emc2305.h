/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_PLATFORM_DATA_EMC2305__
#define __LINUX_PLATFORM_DATA_EMC2305__

#define EMC2305_PWM_MAX	5

/**
 * struct emc2305_platform_data - EMC2305 driver platform data
 * @max_state: maximum cooling state of the cooling device;
 * @pwm_num: number of active channels;
 * @pwm_output_mask: PWM output mask
 * @pwm_polarity_mask: PWM polarity mask
 * @pwm_separate: separate PWM settings for every channel;
 * @pwm_min: array of minimum PWM per channel;
 * @pwm_freq: array of PWM frequency per channel
 */
struct emc2305_platform_data {
	u8 max_state;
	u8 pwm_num;
	u8 pwm_output_mask;
	u8 pwm_polarity_mask;
	bool pwm_separate;
	u8 pwm_min[EMC2305_PWM_MAX];
	u16 pwm_freq[EMC2305_PWM_MAX];
};

#endif
