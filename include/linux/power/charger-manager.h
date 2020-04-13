/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo.Ham <myungjoo.ham@samsung.com>
 *
 * Charger Manager.
 * This framework enables to control and multiple chargers and to
 * monitor charging even in the context of suspend-to-RAM with
 * an interface combining the chargers.
 *
**/

#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>

enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum cm_event_types {
	CM_EVENT_UNKNOWN = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_BATT_OVERHEAT,
	CM_EVENT_BATT_COLD,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_OTHERS,
};

/**
 * struct charger_cable
 * @extcon_name: the name of extcon device.
 * @name: the name of charger cable(external connector).
 * @extcon_dev: the extcon device.
 * @wq: the workqueue to control charger according to the state of
 *	charger cable. If charger cable is attached, enable charger.
 *	But if charger cable is detached, disable charger.
 * @nb: the notifier block to receive changed state from EXTCON
 *	(External Connector) when charger cable is attached/detached.
 * @attached: the state of charger cable.
 *	true: the charger cable is attached
 *	false: the charger cable is detached
 * @charger: the instance of struct charger_regulator.
 * @cm: the Charger Manager representing the battery.
 */
struct charger_cable {
	const char *extcon_name;
	const char *name;

	/* The charger-manager use Extcon framework */
	struct extcon_specific_cable_nb extcon_dev;
	struct work_struct wq;
	struct notifier_block nb;

	/* The state of charger cable */
	bool attached;

	struct charger_regulator *charger;

	/*
	 * Set min/max current of regulator to protect over-current issue
	 * according to a kind of charger cable when cable is attached.
	 */
	int min_uA;
	int max_uA;

	struct charger_manager *cm;
};

/**
 * struct charger_regulator
 * @regulator_name: the name of regulator for using charger.
 * @consumer: the regulator consumer for the charger.
 * @externally_control:
 *	Set if the charger-manager cannot control charger,
 *	the charger will be maintained with disabled state.
 * @cables:
 *	the array of charger cables to enable/disable charger
 *	and set current limit according to constraint data of
 *	struct charger_cable if only charger cable included
 *	in the array of charger cables is attached/detached.
 * @num_cables: the number of charger cables.
 * @attr_g: Attribute group for the charger(regulator)
 * @attr_name: "name" sysfs entry
 * @attr_state: "state" sysfs entry
 * @attr_externally_control: "externally_control" sysfs entry
 * @attrs: Arrays pointing to attr_name/state/externally_control for attr_g
 */
struct charger_regulator {
	/* The name of regulator for charging */
	const char *regulator_name;
	struct regulator *consumer;

	/* charger never on when system is on */
	int externally_control;

	/*
	 * Store constraint information related to current limit,
	 * each cable have different condition for charging.
	 */
	struct charger_cable *cables;
	int num_cables;

	struct attribute_group attr_grp;
	struct device_attribute attr_name;
	struct device_attribute attr_state;
	struct device_attribute attr_externally_control;
	struct attribute *attrs[4];

	struct charger_manager *cm;
};

/**
 * struct charger_desc
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @fullbatt_vchkdrop_ms:
 * @fullbatt_vchkdrop_uV:
 *	Check voltage drop after the battery is fully charged.
 *	If it has dropped more than fullbatt_vchkdrop_uV after
 *	fullbatt_vchkdrop_ms, CM will restart charging.
 * @fullbatt_uV: voltage in microvolt
 *	If VBATT >= fullbatt_uV, it is assumed to be full.
 * @fullbatt_soc: state of Charge in %
 *	If state of Charge >= fullbatt_soc, it is assumed to be full.
 * @fullbatt_full_capacity: full capacity measure
 *	If full capacity of battery >= fullbatt_full_capacity,
 *	it is assumed to be full.
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @battery_present:
 *	Specify where information for existence of battery can be obtained
 * @psy_charger_stat: the names of power-supply for chargers
 * @num_charger_regulator: the number of entries in charger_regulators
 * @charger_regulators: array of charger regulators
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @thermal_zone : the name of thermal zone for battery
 * @temp_min : Minimum battery temperature for charging.
 * @temp_max : Maximum battery temperature for charging.
 * @temp_diff : Temperature difference to restart charging.
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 * @charging_max_duration_ms: Maximum possible duration for charging
 *	If whole charging duration exceed 'charging_max_duration_ms',
 *	cm stop charging.
 * @discharging_max_duration_ms:
 *	Maximum possible duration for discharging with charger cable
 *	after full-batt. If discharging duration exceed 'discharging
 *	max_duration_ms', cm start charging.
 */
struct charger_desc {
	const char *psy_name;

	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	unsigned int fullbatt_vchkdrop_ms;
	unsigned int fullbatt_vchkdrop_uV;
	unsigned int fullbatt_uV;
	unsigned int fullbatt_soc;
	unsigned int fullbatt_full_capacity;

	enum data_source battery_present;

	const char **psy_charger_stat;

	int num_charger_regulators;
	struct charger_regulator *charger_regulators;
	const struct attribute_group **sysfs_groups;

	const char *psy_fuel_gauge;

	const char *thermal_zone;

	int temp_min;
	int temp_max;
	int temp_diff;

	bool measure_battery_temp;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;
};

#define PSY_NAME_MAX	30

/**
 * struct charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @tzd_batt : thermal zone device for battery
 * @charger_enabled: the state of charger
 * @fullbatt_vchk_jiffies_at:
 *	jiffies at the time full battery check will occur.
 * @fullbatt_vchk_work: work queue for full battery check
 * @emergency_stop:
 *	When setting true, stop charging
 * @psy_name_buf: the name of power-supply-class for charger manager
 * @charger_psy: power_supply for charger manager
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 * @charging_start_time: saved start time of enabling charging
 * @charging_end_time: saved end time of disabling charging
 */
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd_batt;
#endif
	bool charger_enabled;

	unsigned long fullbatt_vchk_jiffies_at;
	struct delayed_work fullbatt_vchk_work;

	int emergency_stop;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply_desc charger_psy_desc;
	struct power_supply *charger_psy;

	u64 charging_start_time;
	u64 charging_end_time;
};

#if IS_ENABLED(CONFIG_CHARGER_MANAGER)
extern void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg);
#else
static inline void cm_notify_event(struct power_supply *psy,
				enum cm_event_types type, char *msg) { }
#endif
#endif /* _CHARGER_MANAGER_H */
