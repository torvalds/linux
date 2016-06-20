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

#include <linux/bitops.h>

struct device;
struct attribute_group;

enum hwmon_sensor_types {
	hwmon_chip,
	hwmon_temp,
	hwmon_in,
	hwmon_curr,
	hwmon_power,
	hwmon_energy,
};

enum hwmon_chip_attributes {
	hwmon_chip_temp_reset_history,
	hwmon_chip_in_reset_history,
	hwmon_chip_register_tz,
	hwmon_chip_update_interval,
	hwmon_chip_alarms,
};

#define HWMON_C_TEMP_RESET_HISTORY	BIT(hwmon_chip_temp_reset_history)
#define HWMON_C_IN_RESET_HISTORY	BIT(hwmon_chip_in_reset_history)
#define HWMON_C_REGISTER_TZ		BIT(hwmon_chip_register_tz)
#define HWMON_C_UPDATE_INTERVAL		BIT(hwmon_chip_update_interval)
#define HWMON_C_ALARMS			BIT(hwmon_chip_alarms)

enum hwmon_temp_attributes {
	hwmon_temp_input = 0,
	hwmon_temp_type,
	hwmon_temp_lcrit,
	hwmon_temp_lcrit_hyst,
	hwmon_temp_min,
	hwmon_temp_min_hyst,
	hwmon_temp_max,
	hwmon_temp_max_hyst,
	hwmon_temp_crit,
	hwmon_temp_crit_hyst,
	hwmon_temp_emergency,
	hwmon_temp_emergency_hyst,
	hwmon_temp_alarm,
	hwmon_temp_lcrit_alarm,
	hwmon_temp_min_alarm,
	hwmon_temp_max_alarm,
	hwmon_temp_crit_alarm,
	hwmon_temp_emergency_alarm,
	hwmon_temp_fault,
	hwmon_temp_offset,
	hwmon_temp_label,
	hwmon_temp_lowest,
	hwmon_temp_highest,
	hwmon_temp_reset_history,
};

#define HWMON_T_INPUT		BIT(hwmon_temp_input)
#define HWMON_T_TYPE		BIT(hwmon_temp_type)
#define HWMON_T_LCRIT		BIT(hwmon_temp_lcrit)
#define HWMON_T_LCRIT_HYST	BIT(hwmon_temp_lcrit_hyst)
#define HWMON_T_MIN		BIT(hwmon_temp_min)
#define HWMON_T_MIN_HYST	BIT(hwmon_temp_min_hyst)
#define HWMON_T_MAX		BIT(hwmon_temp_max)
#define HWMON_T_MAX_HYST	BIT(hwmon_temp_max_hyst)
#define HWMON_T_CRIT		BIT(hwmon_temp_crit)
#define HWMON_T_CRIT_HYST	BIT(hwmon_temp_crit_hyst)
#define HWMON_T_EMERGENCY	BIT(hwmon_temp_emergency)
#define HWMON_T_EMERGENCY_HYST	BIT(hwmon_temp_emergency_hyst)
#define HWMON_T_MIN_ALARM	BIT(hwmon_temp_min_alarm)
#define HWMON_T_MAX_ALARM	BIT(hwmon_temp_max_alarm)
#define HWMON_T_CRIT_ALARM	BIT(hwmon_temp_crit_alarm)
#define HWMON_T_EMERGENCY_ALARM	BIT(hwmon_temp_emergency_alarm)
#define HWMON_T_FAULT		BIT(hwmon_temp_fault)
#define HWMON_T_OFFSET		BIT(hwmon_temp_offset)
#define HWMON_T_LABEL		BIT(hwmon_temp_label)
#define HWMON_T_LOWEST		BIT(hwmon_temp_lowest)
#define HWMON_T_HIGHEST		BIT(hwmon_temp_highest)
#define HWMON_T_RESET_HISTORY	BIT(hwmon_temp_reset_history)

enum hwmon_in_attributes {
	hwmon_in_input,
	hwmon_in_min,
	hwmon_in_max,
	hwmon_in_lcrit,
	hwmon_in_crit,
	hwmon_in_average,
	hwmon_in_lowest,
	hwmon_in_highest,
	hwmon_in_reset_history,
	hwmon_in_label,
	hwmon_in_alarm,
	hwmon_in_min_alarm,
	hwmon_in_max_alarm,
	hwmon_in_lcrit_alarm,
	hwmon_in_crit_alarm,
};

#define HWMON_I_INPUT		BIT(hwmon_in_input)
#define HWMON_I_MIN		BIT(hwmon_in_min)
#define HWMON_I_MAX		BIT(hwmon_in_max)
#define HWMON_I_LCRIT		BIT(hwmon_in_lcrit)
#define HWMON_I_CRIT		BIT(hwmon_in_crit)
#define HWMON_I_AVERAGE		BIT(hwmon_in_average)
#define HWMON_I_LOWEST		BIT(hwmon_in_lowest)
#define HWMON_I_HIGHEST		BIT(hwmon_in_highest)
#define HWMON_I_RESET_HISTORY	BIT(hwmon_in_reset_history)
#define HWMON_I_LABEL		BIT(hwmon_in_label)
#define HWMON_I_ALARM		BIT(hwmon_in_alarm)
#define HWMON_I_MIN_ALARM	BIT(hwmon_in_min_alarm)
#define HWMON_I_MAX_ALARM	BIT(hwmon_in_max_alarm)
#define HWMON_I_LCRIT_ALARM	BIT(hwmon_in_lcrit_alarm)
#define HWMON_I_CRIT_ALARM	BIT(hwmon_in_crit_alarm)

/**
 * struct hwmon_ops - hwmon device operations
 * @is_visible: Callback to return attribute visibility. Mandatory.
 *		Parameters are:
 *		@const void *drvdata:
 *			Pointer to driver-private data structure passed
 *			as argument to hwmon_device_register_with_info().
 *		@type:	Sensor type
 *		@attr:	Sensor attribute
 *		@channel:
 *			Channel number
 *		The function returns the file permissions.
 *		If the return value is 0, no attribute will be created.
 * @read:       Read callback. Optional. If not provided, attributes
 *		will not be readable.
 *		Parameters are:
 *		@dev:	Pointer to hardware monitoring device
 *		@type:	Sensor type
 *		@attr:	Sensor attribute
 *		@channel:
 *			Channel number
 *		@val:	Pointer to returned value
 *		The function returns 0 on success or a negative error number.
 * @write:	Write callback. Optional. If not provided, attributes
 *		will not be writable.
 *		Parameters are:
 *		@dev:	Pointer to hardware monitoring device
 *		@type:	Sensor type
 *		@attr:	Sensor attribute
 *		@channel:
 *			Channel number
 *		@val:	Value to write
 *		The function returns 0 on success or a negative error number.
 */
struct hwmon_ops {
	umode_t (*is_visible)(const void *drvdata, enum hwmon_sensor_types type,
			      u32 attr, int channel);
	int (*read)(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val);
	int (*write)(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long val);
};

/**
 * Channel information
 * @type:	Channel type.
 * @config:	Pointer to NULL-terminated list of channel parameters.
 *		Use for per-channel attributes.
 */
struct hwmon_channel_info {
	enum hwmon_sensor_types type;
	const u32 *config;
};

/**
 * Chip configuration
 * @ops:	Pointer to hwmon operations.
 * @info:	Null-terminated list of channel information.
 */
struct hwmon_chip_info {
	const struct hwmon_ops *ops;
	const struct hwmon_channel_info **info;
};

struct device *hwmon_device_register(struct device *dev);
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups);
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				       void *drvdata,
				       const struct attribute_group **groups);
struct device *
hwmon_device_register_with_info(struct device *dev,
				const char *name, void *drvdata,
				const struct hwmon_chip_info *info,
				const struct attribute_group **groups);
struct device *
devm_hwmon_device_register_with_info(struct device *dev,
				     const char *name, void *drvdata,
				     const struct hwmon_chip_info *info,
				     const struct attribute_group **groups);

void hwmon_device_unregister(struct device *dev);
void devm_hwmon_device_unregister(struct device *dev);

#endif
