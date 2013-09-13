/*
 * bq2415x charger driver
 *
 * Copyright (C) 2011-2012  Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
 * Function set_mode_hook is needed for automode (setting correct current
 * limit when charger is connected/disconnected or setting boost mode).
 * When is NULL, automode function is disabled. When is not NULL, it must
 * have this prototype:
 *
 *    int (*set_mode_hook)(
 *      void (*hook)(enum bq2415x_mode mode, void *data),
 *      void *data)
 *
 * hook is hook function (see below) and data is pointer to driver private
 * data
 *
 * bq2415x driver will call it as:
 *
 *    platform_data->set_mode_hook(bq2415x_hook_function, bq2415x_device);
 *
 * Board/platform function set_mode_hook return non zero value when hook
 * function was successful registered. Platform code should call that hook
 * function (which get from pointer, with data) every time when charger
 * was connected/disconnected or require to enable boost mode. bq2415x
 * driver then will set correct current limit, enable/disable charger or
 * boost mode.
 *
 * Hook function has this prototype:
 *
 *    void hook(enum bq2415x_mode mode, void *data);
 *
 * mode is bq2415x mode (charger or boost)
 * data is pointer to driver private data (which get from
 * set_charger_type_hook)
 *
 * When bq driver is being unloaded, it call function:
 *
 *    platform_data->set_mode_hook(NULL, NULL);
 *
 * (hook function and driver private data are NULL)
 *
 * After that board/platform code must not call driver hook function! It
 * is possible that pointer to hook function will not be valid and calling
 * will cause undefined result.
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
	int (*set_mode_hook)(void (*hook)(enum bq2415x_mode mode, void *data),
			     void *data);
};

#endif
