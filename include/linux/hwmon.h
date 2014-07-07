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

struct device;
struct attribute_group;

struct device *hwmon_device_register(struct device *dev);
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups);
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				       void *drvdata,
				       const struct attribute_group **groups);

void hwmon_device_unregister(struct device *dev);
void devm_hwmon_device_unregister(struct device *dev);

#endif
