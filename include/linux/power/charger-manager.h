/* linux/include/linux/power/charger-manager.h
 *
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

#ifndef __SAMSUNG_DEV_CHARGER_H
#define __SAMSUNG_DEV_CHARGER_H

#include <linux/power_supply.h>
#include <linux/extcon.h>

enum data_source {
	CM_ASSUME_ALWAYS_TRUE,
	CM_ASSUME_ALWAYS_FALSE,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum cm_event_types {
	CM_EVENT_UNDESCRIBED = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_OTHERS,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	/* To use PWR-ONLY option, EXT_PWR_IN_OUT type irqs should exist */
	CM_POLL_EXTERNAL_POWER_ONLY,
	/* To use CHG-ONLY option, CHG_START_STOP type irqs should exist */
	CM_POLL_CHARGING_ONLY,
};

struct charger_global_desc {
	/*
	 * For in-suspend monitoring, suspend-again related data is
	 * required. These are used as global for Charger-Manager.
	 * They should work with no_irq with dpm_suspend()'ed environment.
	 *
	 * rtc is the name of RTC used to wakeup the system from
	 * suspend. Previously appointed alarm is saved and restored if
	 * enabled and the alarm time is later than now.
	 */
	char *rtc;

	/*
	 * If the system is waked up by waekup-sources other than the RTC or
	 * callbacks.setup provided with charger_global_desc, Charger Manager
	 * should recognize with is_rtc_only_wakeup_reason() returning false.
	 * If the RTC given to CM is the only wakeup reason,
	 * is_rtc_only_wakeup_reason should return true.
	 */
	bool (*is_rtc_only_wakeup_reason)(void);

	/*
	 * Assume that the jiffy timer stops in suspend-to-RAM.
	 * When enabled, CM does not rely on jiffies value in
	 * suspend_again and assumes that jiffies value does not
	 * change during suspend.
	 */
	bool assume_timer_stops_in_suspend;
};

#ifdef CONFIG_EXTCON
struct charger_cable {
	const char *extcon_name;
	const char *name;

	/*
	 * Set min/max current of regulator to protect over-current issue
	 * according to a kind of charger cable when cable is attached.
	 */
	int min_uA;
	int max_uA;

	/* The charger-manager use Exton framework*/
	struct extcon_specific_cable_nb extcon_dev;
	struct work_struct wq;
	struct notifier_block nb;

	/* The state of charger cable */
	bool attached;

	struct charger_regulator *charger;
	struct charger_manager *cm;
};

struct charger_regulator {
	/* The name of regulator for charging */
	const char *regulator_name;
	struct regulator *consumer;

	/*
	 * Store constraint information related to current limit,
	 * each cable have different condition for charging.
	 */
	struct charger_cable *cables;
	int num_cables;
};
#endif

struct charger_desc {
	/*
	 * The name of psy (power-supply-class) entry.
	 * If psy_name is NULL, "battery" is used.
	 */
	char *psy_name;

	/* The manager may poll with shorter interval, but not longer. */
	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;

	/*
	 * Check voltage drop after the battery is fully charged.
	 * If it has dropped more than fullbatt_vchkdrop_uV after
	 * fullbatt_vchkdrop_ms, CM will restart charging.
	 */
	unsigned int fullbatt_vchkdrop_ms;
	unsigned int fullbatt_vchkdrop_uV;

	/*
	 * If it is not being charged and VBATT >= fullbatt_uV,
	 * it is assumed to be full. In order not to use this, set
	 * fullbatt_uV 0.
	 */
	unsigned int fullbatt_uV;

	/*
	 * How the data is picked up for "PRESENT"?
	 * Are we reading the value from chargers or fuel gauges?
	 */
	enum data_source battery_present;

	/*
	 * The power-supply entries of psy_charger_stat[i] shows "PRESENT",
	 * "ONLINE", "STATUS (Should notify at least FULL or NOT)" of the
	 * charger-i. "Charging/Discharging/NotCharging" of "STATUS" are
	 * optional and recommended.
	 */
	char **psy_charger_stat;

	/*
	 * The power-supply entries with VOLTAGE_NOW, CAPACITY,
	 * and "PRESENT".
	 */
	char *psy_fuel_gauge;

	int (*is_temperature_error)(int *mC);
	bool measure_ambient_temp;
	bool measure_battery_temp;

	int soc_margin;

	struct charger_regulator *charger_regulators;
	int num_charger_regulators;
};

#define PSY_NAME_MAX	30
struct charger_manager {
	struct list_head entry;
	struct device *dev;
	struct charger_desc *desc;

	struct power_supply *fuel_gauge;
	struct power_supply **charger_stat;

	bool cancel_suspend; /* if there is a pending charger event. */
	bool charger_enabled;

	unsigned long fullbatt_vchk_jiffies_at; /* 0 for N/A */
	unsigned int fullbatt_vchk_uV;
	struct delayed_work fullbatt_vchk_work;

	bool user_prohibit;
	int emergency_stop; /* Do not charge */
	int last_temp_mC;

	char psy_name_buf[PSY_NAME_MAX + 1]; /* Output to user */
	struct power_supply charger_psy;

	/*
	 * status saved entering a suspend and if the saved status is
	 * changed at suspend_again, suspend_again STOPs
	 */
	bool status_save_ext_pwr_inserted;
	bool status_save_batt;

	int batt_tmu_status;
};

/* In case IRQs cannot be given and notifications will be given. */
#ifdef CONFIG_CHARGER_MANAGER
extern void cm_notify_event(struct power_supply *psy, enum cm_event_types type,
			    char *msg); /* msg: optional */
extern struct charger_manager *get_charger_manager(char *psy_name);
extern int setup_charger_manager(struct charger_global_desc *gd);
extern bool is_charger_manager_active(void);
extern bool cm_suspend_again(void);
extern void cm_prohibit_charging(struct charger_manager *cm);
extern void cm_allow_charging(struct charger_manager *cm);
#else
static void __maybe_unused cm_notify_event(struct power_supply *psy,
					   enum cm_event_types type, char *msg)
{ }

static struct charger_manager __maybe_unused *get_charger_manager(
						char *psy_name)
{
	return NULL;
}

static void __maybe_unused setup_charger_manager(struct charger_global_desc *gd)
{ }

static bool __maybe_unused is_charger_manager_active(void)
{
	return false;
}

static bool __maybe_unused cm_suspend_again(void)
{
	return false;
}
static void __maybe_unused cm_prohibit_charging(struct charger_manager *cm) { }
static void __maybe_unused cm_allow_charging(struct charger_manager *cm) { }
#endif

#endif /* __SAMSUNG_DEV_CHARGER_H */
