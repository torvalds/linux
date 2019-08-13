/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * bq2415x charger driver
 *
 * Copyright (C) 2011-2013  Pali Rohár <pali.rohar@gmail.com>
 */

#ifndef BQ2415X_CHARGER_H
#define BQ2415X_CHARGER_H

/*
 * This is platform data for bq2415x chip. It contains default board
 * voltages and currents which can be also later configured via sysfs. If
 * value is -1 then default chip value (specified in datasheet) will be
 * used.
 *
 * Value resistor_sense is needed for for configuring charge and
 * termination current. It it is less or equal to zero, configuring charge
 * and termination current will not be possible.
 *
 * For automode support is needed to provide name of power supply device
 * in value notify_device. Device driver must immediately report property
 * POWER_SUPPLY_PROP_CURRENT_MAX when current changed.
 */

/* Supported modes with maximal current limit */
enum bq2415x_mode {
	BQ2415X_MODE_OFF,		/* offline mode (charger disabled) */
	BQ2415X_MODE_NONE,		/* unknown charger (100mA) */
	BQ2415X_MODE_HOST_CHARGER,	/* usb host/hub charger (500mA) */
	BQ2415X_MODE_DEDICATED_CHARGER, /* dedicated charger (unlimited) */
	BQ2415X_MODE_BOOST,		/* boost mode (charging disabled) */
};

struct bq2415x_platform_data {
	int current_limit;		/* mA */
	int weak_battery_voltage;	/* mV */
	int battery_regulation_voltage;	/* mV */
	int charge_current;		/* mA */
	int termination_current;	/* mA */
	int resistor_sense;		/* m ohm */
	const char *notify_device;	/* name */
};

#endif
