/* SPDX-License-Identifier: GPL-2.0-only */
/*
    hwmon.h - part of lm_sensors, Linux kernel modules for hardware monitoring

    This file declares helper functions for the sysfs class "hwmon",
    for use by sensors drivers.

    Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>

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
	hwmon_humidity,
	hwmon_fan,
	hwmon_pwm,
	hwmon_intrusion,
	hwmon_max,
};

enum hwmon_chip_attributes {
	hwmon_chip_temp_reset_history,
	hwmon_chip_in_reset_history,
	hwmon_chip_curr_reset_history,
	hwmon_chip_power_reset_history,
	hwmon_chip_register_tz,
	hwmon_chip_update_interval,
	hwmon_chip_alarms,
	hwmon_chip_samples,
	hwmon_chip_curr_samples,
	hwmon_chip_in_samples,
	hwmon_chip_power_samples,
	hwmon_chip_temp_samples,
	hwmon_chip_beep_enable,
	hwmon_chip_pec,
};

#define HWMON_C_TEMP_RESET_HISTORY	BIT(hwmon_chip_temp_reset_history)
#define HWMON_C_IN_RESET_HISTORY	BIT(hwmon_chip_in_reset_history)
#define HWMON_C_CURR_RESET_HISTORY	BIT(hwmon_chip_curr_reset_history)
#define HWMON_C_POWER_RESET_HISTORY	BIT(hwmon_chip_power_reset_history)
#define HWMON_C_REGISTER_TZ		BIT(hwmon_chip_register_tz)
#define HWMON_C_UPDATE_INTERVAL		BIT(hwmon_chip_update_interval)
#define HWMON_C_ALARMS			BIT(hwmon_chip_alarms)
#define HWMON_C_SAMPLES			BIT(hwmon_chip_samples)
#define HWMON_C_CURR_SAMPLES		BIT(hwmon_chip_curr_samples)
#define HWMON_C_IN_SAMPLES		BIT(hwmon_chip_in_samples)
#define HWMON_C_POWER_SAMPLES		BIT(hwmon_chip_power_samples)
#define HWMON_C_TEMP_SAMPLES		BIT(hwmon_chip_temp_samples)
#define HWMON_C_BEEP_ENABLE		BIT(hwmon_chip_beep_enable)
#define HWMON_C_PEC			BIT(hwmon_chip_pec)

enum hwmon_temp_attributes {
	hwmon_temp_enable,
	hwmon_temp_input,
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
	hwmon_temp_rated_min,
	hwmon_temp_rated_max,
	hwmon_temp_beep,
};

#define HWMON_T_ENABLE		BIT(hwmon_temp_enable)
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
#define HWMON_T_ALARM		BIT(hwmon_temp_alarm)
#define HWMON_T_MIN_ALARM	BIT(hwmon_temp_min_alarm)
#define HWMON_T_MAX_ALARM	BIT(hwmon_temp_max_alarm)
#define HWMON_T_CRIT_ALARM	BIT(hwmon_temp_crit_alarm)
#define HWMON_T_LCRIT_ALARM	BIT(hwmon_temp_lcrit_alarm)
#define HWMON_T_EMERGENCY_ALARM	BIT(hwmon_temp_emergency_alarm)
#define HWMON_T_FAULT		BIT(hwmon_temp_fault)
#define HWMON_T_OFFSET		BIT(hwmon_temp_offset)
#define HWMON_T_LABEL		BIT(hwmon_temp_label)
#define HWMON_T_LOWEST		BIT(hwmon_temp_lowest)
#define HWMON_T_HIGHEST		BIT(hwmon_temp_highest)
#define HWMON_T_RESET_HISTORY	BIT(hwmon_temp_reset_history)
#define HWMON_T_RATED_MIN	BIT(hwmon_temp_rated_min)
#define HWMON_T_RATED_MAX	BIT(hwmon_temp_rated_max)
#define HWMON_T_BEEP		BIT(hwmon_temp_beep)

enum hwmon_in_attributes {
	hwmon_in_enable,
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
	hwmon_in_rated_min,
	hwmon_in_rated_max,
	hwmon_in_beep,
	hwmon_in_fault,
};

#define HWMON_I_ENABLE		BIT(hwmon_in_enable)
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
#define HWMON_I_RATED_MIN	BIT(hwmon_in_rated_min)
#define HWMON_I_RATED_MAX	BIT(hwmon_in_rated_max)
#define HWMON_I_BEEP		BIT(hwmon_in_beep)
#define HWMON_I_FAULT		BIT(hwmon_in_fault)

enum hwmon_curr_attributes {
	hwmon_curr_enable,
	hwmon_curr_input,
	hwmon_curr_min,
	hwmon_curr_max,
	hwmon_curr_lcrit,
	hwmon_curr_crit,
	hwmon_curr_average,
	hwmon_curr_lowest,
	hwmon_curr_highest,
	hwmon_curr_reset_history,
	hwmon_curr_label,
	hwmon_curr_alarm,
	hwmon_curr_min_alarm,
	hwmon_curr_max_alarm,
	hwmon_curr_lcrit_alarm,
	hwmon_curr_crit_alarm,
	hwmon_curr_rated_min,
	hwmon_curr_rated_max,
	hwmon_curr_beep,
};

#define HWMON_C_ENABLE		BIT(hwmon_curr_enable)
#define HWMON_C_INPUT		BIT(hwmon_curr_input)
#define HWMON_C_MIN		BIT(hwmon_curr_min)
#define HWMON_C_MAX		BIT(hwmon_curr_max)
#define HWMON_C_LCRIT		BIT(hwmon_curr_lcrit)
#define HWMON_C_CRIT		BIT(hwmon_curr_crit)
#define HWMON_C_AVERAGE		BIT(hwmon_curr_average)
#define HWMON_C_LOWEST		BIT(hwmon_curr_lowest)
#define HWMON_C_HIGHEST		BIT(hwmon_curr_highest)
#define HWMON_C_RESET_HISTORY	BIT(hwmon_curr_reset_history)
#define HWMON_C_LABEL		BIT(hwmon_curr_label)
#define HWMON_C_ALARM		BIT(hwmon_curr_alarm)
#define HWMON_C_MIN_ALARM	BIT(hwmon_curr_min_alarm)
#define HWMON_C_MAX_ALARM	BIT(hwmon_curr_max_alarm)
#define HWMON_C_LCRIT_ALARM	BIT(hwmon_curr_lcrit_alarm)
#define HWMON_C_CRIT_ALARM	BIT(hwmon_curr_crit_alarm)
#define HWMON_C_RATED_MIN	BIT(hwmon_curr_rated_min)
#define HWMON_C_RATED_MAX	BIT(hwmon_curr_rated_max)
#define HWMON_C_BEEP		BIT(hwmon_curr_beep)

enum hwmon_power_attributes {
	hwmon_power_enable,
	hwmon_power_average,
	hwmon_power_average_interval,
	hwmon_power_average_interval_max,
	hwmon_power_average_interval_min,
	hwmon_power_average_highest,
	hwmon_power_average_lowest,
	hwmon_power_average_max,
	hwmon_power_average_min,
	hwmon_power_input,
	hwmon_power_input_highest,
	hwmon_power_input_lowest,
	hwmon_power_reset_history,
	hwmon_power_accuracy,
	hwmon_power_cap,
	hwmon_power_cap_hyst,
	hwmon_power_cap_max,
	hwmon_power_cap_min,
	hwmon_power_min,
	hwmon_power_max,
	hwmon_power_crit,
	hwmon_power_lcrit,
	hwmon_power_label,
	hwmon_power_alarm,
	hwmon_power_cap_alarm,
	hwmon_power_min_alarm,
	hwmon_power_max_alarm,
	hwmon_power_lcrit_alarm,
	hwmon_power_crit_alarm,
	hwmon_power_rated_min,
	hwmon_power_rated_max,
};

#define HWMON_P_ENABLE			BIT(hwmon_power_enable)
#define HWMON_P_AVERAGE			BIT(hwmon_power_average)
#define HWMON_P_AVERAGE_INTERVAL	BIT(hwmon_power_average_interval)
#define HWMON_P_AVERAGE_INTERVAL_MAX	BIT(hwmon_power_average_interval_max)
#define HWMON_P_AVERAGE_INTERVAL_MIN	BIT(hwmon_power_average_interval_min)
#define HWMON_P_AVERAGE_HIGHEST		BIT(hwmon_power_average_highest)
#define HWMON_P_AVERAGE_LOWEST		BIT(hwmon_power_average_lowest)
#define HWMON_P_AVERAGE_MAX		BIT(hwmon_power_average_max)
#define HWMON_P_AVERAGE_MIN		BIT(hwmon_power_average_min)
#define HWMON_P_INPUT			BIT(hwmon_power_input)
#define HWMON_P_INPUT_HIGHEST		BIT(hwmon_power_input_highest)
#define HWMON_P_INPUT_LOWEST		BIT(hwmon_power_input_lowest)
#define HWMON_P_RESET_HISTORY		BIT(hwmon_power_reset_history)
#define HWMON_P_ACCURACY		BIT(hwmon_power_accuracy)
#define HWMON_P_CAP			BIT(hwmon_power_cap)
#define HWMON_P_CAP_HYST		BIT(hwmon_power_cap_hyst)
#define HWMON_P_CAP_MAX			BIT(hwmon_power_cap_max)
#define HWMON_P_CAP_MIN			BIT(hwmon_power_cap_min)
#define HWMON_P_MIN			BIT(hwmon_power_min)
#define HWMON_P_MAX			BIT(hwmon_power_max)
#define HWMON_P_LCRIT			BIT(hwmon_power_lcrit)
#define HWMON_P_CRIT			BIT(hwmon_power_crit)
#define HWMON_P_LABEL			BIT(hwmon_power_label)
#define HWMON_P_ALARM			BIT(hwmon_power_alarm)
#define HWMON_P_CAP_ALARM		BIT(hwmon_power_cap_alarm)
#define HWMON_P_MIN_ALARM		BIT(hwmon_power_min_alarm)
#define HWMON_P_MAX_ALARM		BIT(hwmon_power_max_alarm)
#define HWMON_P_LCRIT_ALARM		BIT(hwmon_power_lcrit_alarm)
#define HWMON_P_CRIT_ALARM		BIT(hwmon_power_crit_alarm)
#define HWMON_P_RATED_MIN		BIT(hwmon_power_rated_min)
#define HWMON_P_RATED_MAX		BIT(hwmon_power_rated_max)

enum hwmon_energy_attributes {
	hwmon_energy_enable,
	hwmon_energy_input,
	hwmon_energy_label,
};

#define HWMON_E_ENABLE			BIT(hwmon_energy_enable)
#define HWMON_E_INPUT			BIT(hwmon_energy_input)
#define HWMON_E_LABEL			BIT(hwmon_energy_label)

enum hwmon_humidity_attributes {
	hwmon_humidity_enable,
	hwmon_humidity_input,
	hwmon_humidity_label,
	hwmon_humidity_min,
	hwmon_humidity_min_hyst,
	hwmon_humidity_max,
	hwmon_humidity_max_hyst,
	hwmon_humidity_alarm,
	hwmon_humidity_fault,
	hwmon_humidity_rated_min,
	hwmon_humidity_rated_max,
	hwmon_humidity_min_alarm,
	hwmon_humidity_max_alarm,
};

#define HWMON_H_ENABLE			BIT(hwmon_humidity_enable)
#define HWMON_H_INPUT			BIT(hwmon_humidity_input)
#define HWMON_H_LABEL			BIT(hwmon_humidity_label)
#define HWMON_H_MIN			BIT(hwmon_humidity_min)
#define HWMON_H_MIN_HYST		BIT(hwmon_humidity_min_hyst)
#define HWMON_H_MAX			BIT(hwmon_humidity_max)
#define HWMON_H_MAX_HYST		BIT(hwmon_humidity_max_hyst)
#define HWMON_H_ALARM			BIT(hwmon_humidity_alarm)
#define HWMON_H_FAULT			BIT(hwmon_humidity_fault)
#define HWMON_H_RATED_MIN		BIT(hwmon_humidity_rated_min)
#define HWMON_H_RATED_MAX		BIT(hwmon_humidity_rated_max)
#define HWMON_H_MIN_ALARM		BIT(hwmon_humidity_min_alarm)
#define HWMON_H_MAX_ALARM		BIT(hwmon_humidity_max_alarm)

enum hwmon_fan_attributes {
	hwmon_fan_enable,
	hwmon_fan_input,
	hwmon_fan_label,
	hwmon_fan_min,
	hwmon_fan_max,
	hwmon_fan_div,
	hwmon_fan_pulses,
	hwmon_fan_target,
	hwmon_fan_alarm,
	hwmon_fan_min_alarm,
	hwmon_fan_max_alarm,
	hwmon_fan_fault,
	hwmon_fan_beep,
};

#define HWMON_F_ENABLE			BIT(hwmon_fan_enable)
#define HWMON_F_INPUT			BIT(hwmon_fan_input)
#define HWMON_F_LABEL			BIT(hwmon_fan_label)
#define HWMON_F_MIN			BIT(hwmon_fan_min)
#define HWMON_F_MAX			BIT(hwmon_fan_max)
#define HWMON_F_DIV			BIT(hwmon_fan_div)
#define HWMON_F_PULSES			BIT(hwmon_fan_pulses)
#define HWMON_F_TARGET			BIT(hwmon_fan_target)
#define HWMON_F_ALARM			BIT(hwmon_fan_alarm)
#define HWMON_F_MIN_ALARM		BIT(hwmon_fan_min_alarm)
#define HWMON_F_MAX_ALARM		BIT(hwmon_fan_max_alarm)
#define HWMON_F_FAULT			BIT(hwmon_fan_fault)
#define HWMON_F_BEEP			BIT(hwmon_fan_beep)

enum hwmon_pwm_attributes {
	hwmon_pwm_input,
	hwmon_pwm_enable,
	hwmon_pwm_mode,
	hwmon_pwm_freq,
	hwmon_pwm_auto_channels_temp,
};

#define HWMON_PWM_INPUT			BIT(hwmon_pwm_input)
#define HWMON_PWM_ENABLE		BIT(hwmon_pwm_enable)
#define HWMON_PWM_MODE			BIT(hwmon_pwm_mode)
#define HWMON_PWM_FREQ			BIT(hwmon_pwm_freq)
#define HWMON_PWM_AUTO_CHANNELS_TEMP	BIT(hwmon_pwm_auto_channels_temp)

enum hwmon_intrusion_attributes {
	hwmon_intrusion_alarm,
	hwmon_intrusion_beep,
};
#define HWMON_INTRUSION_ALARM		BIT(hwmon_intrusion_alarm)
#define HWMON_INTRUSION_BEEP		BIT(hwmon_intrusion_beep)

/**
 * struct hwmon_ops - hwmon device operations
 * @visible:	Static visibility. If non-zero, 'is_visible' is ignored.
 * @is_visible: Callback to return attribute visibility. Mandatory unless
 *		'visible' is non-zero.
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
 * @read:	Read callback for data attributes. Mandatory if readable
 *		data attributes are present.
 *		Parameters are:
 *		@dev:	Pointer to hardware monitoring device
 *		@type:	Sensor type
 *		@attr:	Sensor attribute
 *		@channel:
 *			Channel number
 *		@val:	Pointer to returned value
 *		The function returns 0 on success or a negative error number.
 * @read_string:
 *		Read callback for string attributes. Mandatory if string
 *		attributes are present.
 *		Parameters are:
 *		@dev:	Pointer to hardware monitoring device
 *		@type:	Sensor type
 *		@attr:	Sensor attribute
 *		@channel:
 *			Channel number
 *		@str:	Pointer to returned string
 *		The function returns 0 on success or a negative error number.
 * @write:	Write callback for data attributes. Mandatory if writeable
 *		data attributes are present.
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
	umode_t visible;
	umode_t (*is_visible)(const void *drvdata, enum hwmon_sensor_types type,
			      u32 attr, int channel);
	int (*read)(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val);
	int (*read_string)(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, const char **str);
	int (*write)(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long val);
};

/**
 * struct hwmon_channel_info - Channel information
 * @type:	Channel type.
 * @config:	Pointer to NULL-terminated list of channel parameters.
 *		Use for per-channel attributes.
 */
struct hwmon_channel_info {
	enum hwmon_sensor_types type;
	const u32 *config;
};

#define HWMON_CHANNEL_INFO(stype, ...)		\
	(&(const struct hwmon_channel_info) {	\
		.type = hwmon_##stype,		\
		.config = (const u32 []) {	\
			__VA_ARGS__, 0		\
		}				\
	})

/**
 * struct hwmon_chip_info - Chip configuration
 * @ops:	Pointer to hwmon operations.
 * @info:	Null-terminated list of channel information.
 */
struct hwmon_chip_info {
	const struct hwmon_ops *ops;
	const struct hwmon_channel_info * const *info;
};

/* hwmon_device_register() is deprecated */
struct device *hwmon_device_register(struct device *dev);

/*
 * hwmon_device_register_with_groups() and
 * devm_hwmon_device_register_with_groups() are deprecated.
 */
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
				const struct attribute_group **extra_groups);
struct device *
hwmon_device_register_for_thermal(struct device *dev, const char *name,
				  void *drvdata);
struct device *
devm_hwmon_device_register_with_info(struct device *dev,
				const char *name, void *drvdata,
				const struct hwmon_chip_info *info,
				const struct attribute_group **extra_groups);

void hwmon_device_unregister(struct device *dev);

int hwmon_notify_event(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel);

char *hwmon_sanitize_name(const char *name);
char *devm_hwmon_sanitize_name(struct device *dev, const char *name);

/**
 * hwmon_is_bad_char - Is the char invalid in a hwmon name
 * @ch: the char to be considered
 *
 * hwmon_is_bad_char() can be used to determine if the given character
 * may not be used in a hwmon name.
 *
 * Returns true if the char is invalid, false otherwise.
 */
static inline bool hwmon_is_bad_char(const char ch)
{
	switch (ch) {
	case '-':
	case '*':
	case ' ':
	case '\t':
	case '\n':
		return true;
	default:
		return false;
	}
}

#endif
