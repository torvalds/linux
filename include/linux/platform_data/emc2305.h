/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_PLATFORM_DATA_EMC2305__
#define __LINUX_PLATFORM_DATA_EMC2305__

#define EMC2305_PWM_MAX	5

/**
 * struct emc2305_platform_data - EMC2305 driver platform data
 * @max_state: maximum cooling state of the cooling device;
 * @pwm_num: number of active channels;
 * @pwm_separate: separate PWM settings for every channel;
 * @pwm_min: array of minimum PWM per channel;
 */
struct emc2305_platform_data {
	u8 max_state;
	u8 pwm_num;
	bool pwm_separate;
	u8 pwm_min[EMC2305_PWM_MAX];
};

#endif
