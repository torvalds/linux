/*
    hwmon.h - part of lm_sensors, Linux kernel modules for hardware monitoring

    This file declares helper functions for the sysfs class "hwmon",
    for use by sensors drivers.

    Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#ifndef _HWMON_H_
#define _HWMON_H_

#include <linux/device.h>

struct device *hwmon_device_register(struct device *dev);

void hwmon_device_unregister(struct device *dev);

struct hwmon_property;

/*
 * Register property: add the sysfs entry for the hwmon framework
 *		     so that the hwmon property can be accessed with
 *		     hwmon_get_value()/hwmon_set_value().
 * Unregister property: the reverse.
 *
 * Note that register/unregister property functions do not touch
 * sysfs itself. The user should call sysfs_create/update/merge/...
 * themselves.
 */
extern struct hwmon_property *hwmon_register_property(struct device *hwmon,
					const struct device_attribute *attr);
extern int hwmon_unregister_property(struct device *hwmon,
				     struct hwmon_property *);
extern int hwmon_register_properties(struct device *hwmon,
				     const struct attribute_group *attrs);
extern int hwmon_unregister_properties(struct device *hwmon,
				       const struct attribute_group *attrs);

/* Note that hwmon_device_unregister does the same anyway */
extern void hwmon_unregister_all_properties(struct device *hwmon);

extern struct device *hwmon_find_device(struct device *dev);
extern struct device *hwmon_find_device_name(char *devname);

extern struct hwmon_property *hwmon_get_property(struct device *hwmon,
						 const char *name);
extern int hwmon_get_value(struct device *hwmon, struct hwmon_property * prop,
			   int *value);
extern int hwmon_set_value(struct device *hwmon, struct hwmon_property * prop,
			   int value);

/* Scale user input to sensible values */
static inline int SENSORS_LIMIT(long value, long low, long high)
{
	if (value < low)
		return low;
	else if (value > high)
		return high;
	else
		return value;
}

#endif

