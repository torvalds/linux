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

/**
 * struct platform_profile_ops - platform profile operations
 * @probe: Callback to setup choices available to the new class device. These
 *	   choices will only be enforced when setting a new profile, not when
 *	   getting the current one.
 * @hidden_choices: Callback to setup choices that are not visible to the user
 *		    but can be set by the driver.
 * @profile_get: Callback that will be called when showing the current platform
 *		 profile in sysfs.
 * @profile_set: Callback that will be called when storing a new platform
 *		 profile in sysfs.
 */
struct platform_profile_ops {
	int (*probe)(void *drvdata, unsigned long *choices);
	int (*hidden_choices)(void *drvdata, unsigned long *choices);
	int (*profile_get)(struct device *dev, enum platform_profile_option *profile);
	int (*profile_set)(struct device *dev, enum platform_profile_option profile);
};

struct device *platform_profile_register(struct device *dev, const char *name,
					 void *drvdata,
					 const struct platform_profile_ops *ops);
void platform_profile_remove(struct device *dev);
struct device *devm_platform_profile_register(struct device *dev, const char *name,
					      void *drvdata,
					      const struct platform_profile_ops *ops);
int platform_profile_cycle(void);
void platform_profile_notify(struct device *dev);

#endif  /*_PLATFORM_PROFILE_H_*/
