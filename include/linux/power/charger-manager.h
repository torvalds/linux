/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo.Ham <myungjoo.ham@samsung.com>
 *
 * Charger Manager.
 * This framework enables to control and multiple chargers and to
 * monitor charging even in the context of suspend-to-RAM with
 * an interface combining the chargers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
**/

#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/power_supply.h>

enum data_source {
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

/**
 * struct charger_global_desc
 * @rtc_name: the name of RTC used to wake up the system from suspend.
 * @rtc_only_wakeup:
 *	If the system is woken up by waekup-sources other than the RTC or
 *	callbacks, Charger Manager should recognize with
 *	rtc_only_wakeup() returning false.
 *	If the RTC given to CM is the only wakeup reason,
 *	rtc_only_wakeup should return true.
 */
struct charger_global_desc {
	char *rtc_name;

	bool (*rtc_only_wakeup)(void);
};

/**
 * struct charger_desc
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @fullbatt_uV: voltage in microvolt
 *	If it is not being charged and VBATT >= fullbatt_uV,
 *	it is assumed to be full.
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @battery_present:
 *	Specify where information for existance of battery can be obtained
 * @psy_charger_stat: the names of power-supply for chargers
 * @num_charger_regulator: the number of entries in charger_regulators
 * @charger_regulators: array of regulator_bulk_data for chargers
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @temperature_out_of_range:
 *	Determine whether the status is overheat or cold or normal.
 *	return_value > 0: overheat
 *	return_value == 0: normal
 *	return_value < 0: cold
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 */
struct charger_desc {
	char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	unsigned int fullbatt_uV;

	enum data_source battery_present;

	char **psy_charger_stat;

	int num_charger_regulators;
	struct regulator_bulk_data *charger_regulators;

	char *psy_fuel_gauge;

	int (*temperature_out_of_range)(int *mC);
	bool measure_battery_temp;
};

#define PSY_NAME_MAX	30

/**
 * struct charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @charger_enabled: the state of charger
 * @emergency_stop:
 *	When setting true, stop charging
 * @last_temp_mC: the measured temperature in milli-Celsius
 * @psy_name_buf: the name of power-supply-class for charger manager
 * @charger_psy: power_supply for charger manager
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 */
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

	struct power_supply *fuel_gauge;
	struct power_supply **charger_stat;

	bool charger_enabled;

	int emergency_stop;
	int last_temp_mC;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply charger_psy;

	bool status_save_ext_pwr_inserted;
	bool status_save_batt;
};

#ifdef CONFIG_CHARGER_MANAGER
extern int setup_charger_manager(struct charger_global_desc *gd);
extern bool cm_suspend_again(void);
#else
static void __maybe_unused setup_charger_manager(struct charger_global_desc *gd)
{ }

static bool __maybe_unused cm_suspend_again(void)
{
	return false;
}
#endif

#endif /* _CHARGER_MANAGER_H */
