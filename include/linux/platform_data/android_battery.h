/*
 *  android_battery.h
 *
 *  Copyright (C) 2012 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_ANDROID_BATTERY_H
#define _LINUX_ANDROID_BATTERY_H

enum {
	CHARGE_SOURCE_NONE = 0,
	CHARGE_SOURCE_AC,
	CHARGE_SOURCE_USB,
};

struct android_bat_callbacks {
	void (*charge_source_changed)
		(struct android_bat_callbacks *, int);
};

struct android_bat_platform_data {
	void (*register_callbacks)(struct android_bat_callbacks *);
	void (*unregister_callbacks)(void);
	void (*set_charging_current) (int);
	void (*set_charging_enable) (int);
	int (*poll_charge_source) (void);
	int (*get_capacity) (void);
	int (*get_temperature) (int *);
	int (*get_voltage_now)(void);
	int (*get_current_now)(int *);

	int temp_high_threshold;
	int temp_high_recovery;
	int temp_low_recovery;
	int temp_low_threshold;
};

#endif
