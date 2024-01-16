/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PLATFORM_DATA_POODLE_AUDIO
#define __LINUX_PLATFORM_DATA_POODLE_AUDIO

/* locomo is not a proper gpio driver, and uses its own api */
struct poodle_audio_platform_data {
	struct device	*locomo_dev;

	int		gpio_amp_on;
	int		gpio_mute_l;
	int		gpio_mute_r;
	int		gpio_232vcc_on;
	int		gpio_jk_b;
};

#endif
