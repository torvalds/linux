/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Platform profile sysfs interface
 *
 * See Documentation/userspace-api/sysfs-platform_profile.rst for more
 * information.
 */

#ifndef _PLATFORM_PROFILE_H_
#define _PLATFORM_PROFILE_H_

#include <linux/device.h>
#include <linux/bitops.h>

/*
 * If more options are added please update profile_names array in
 * platform_profile.c and sysfs-platform_profile documentation.
 */

enum platform_profile_option {
	PLATFORM_PROFILE_LOW_POWER,
	PLATFORM_PROFILE_COOL,
	PLATFORM_PROFILE_QUIET,
	PLATFORM_PROFILE_BALANCED,
	PLATFORM_PROFILE_BALANCED_PERFORMANCE,
	PLATFORM_PROFILE_PERFORMANCE,
	PLATFORM_PROFILE_CUSTOM,
	PLATFORM_PROFILE_LAST, /*must always be last */
};

struct platform_profile_handler {
	const char *name;
	struct device *dev;
	struct device class_dev;
	int minor;
	unsigned long choices[BITS_TO_LONGS(PLATFORM_PROFILE_LAST)];
	int (*profile_get)(struct platform_profile_handler *pprof,
				enum platform_profile_option *profile);
	int (*profile_set)(struct platform_profile_handler *pprof,
				enum platform_profile_option profile);
};

int platform_profile_register(struct platform_profile_handler *pprof);
int platform_profile_remove(struct platform_profile_handler *pprof);
int devm_platform_profile_register(struct platform_profile_handler *pprof);
int platform_profile_cycle(void);
void platform_profile_notify(struct platform_profile_handler *pprof);

#endif  /*_PLATFORM_PROFILE_H_*/
