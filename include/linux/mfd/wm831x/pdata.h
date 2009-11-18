/*
 * include/linux/mfd/wm831x/pdata.h -- Platform data for WM831x
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_WM831X_PDATA_H__
#define __MFD_WM831X_PDATA_H__

struct wm831x;
struct regulator_init_data;

struct wm831x_backlight_pdata {
	int isink;     /** ISINK to use, 1 or 2 */
	int max_uA;    /** Maximum current to allow */
};

struct wm831x_backup_pdata {
	int charger_enable;
	int no_constant_voltage;  /** Disable constant voltage charging */
	int vlim;   /** Voltage limit in milivolts */
	int ilim;   /** Current limit in microamps */
};

struct wm831x_battery_pdata {
	int enable;         /** Enable charging */
	int fast_enable;    /** Enable fast charging */
	int off_mask;       /** Mask OFF while charging */
	int trickle_ilim;   /** Trickle charge current limit, in mA */
	int vsel;           /** Target voltage, in mV */
	int eoc_iterm;      /** End of trickle charge current, in mA */
	int fast_ilim;      /** Fast charge current limit, in mA */
	int timeout;        /** Charge cycle timeout, in minutes */
};

/* Sources for status LED configuration.  Values are register values
 * plus 1 to allow for a zero default for preserve.
 */
enum wm831x_status_src {
	WM831X_STATUS_PRESERVE = 0,  /* Keep the current hardware setting */
	WM831X_STATUS_OTP = 1,
	WM831X_STATUS_POWER = 2,
	WM831X_STATUS_CHARGER = 3,
	WM831X_STATUS_MANUAL = 4,
};

struct wm831x_status_pdata {
	enum wm831x_status_src default_src;
	const char *name;
	const char *default_trigger;
};

struct wm831x_touch_pdata {
	int fivewire;          /** 1 for five wire mode, 0 for 4 wire */
	int isel;              /** Current for pen down (uA) */
	int rpu;               /** Pen down sensitivity resistor divider */
	int pressure;          /** Report pressure (boolean) */
	int data_irq;          /** Touch data ready IRQ */
};

enum wm831x_watchdog_action {
	WM831X_WDOG_NONE = 0,
	WM831X_WDOG_INTERRUPT = 1,
	WM831X_WDOG_RESET = 2,
	WM831X_WDOG_WAKE = 3,
};

struct wm831x_watchdog_pdata {
	enum wm831x_watchdog_action primary, secondary;
	int update_gpio;
	unsigned int software:1;
};

#define WM831X_MAX_STATUS 2
#define WM831X_MAX_DCDC   4
#define WM831X_MAX_EPE    2
#define WM831X_MAX_LDO    11
#define WM831X_MAX_ISINK  2

struct wm831x_pdata {
	/** Called before subdevices are set up */
	int (*pre_init)(struct wm831x *wm831x);
	/** Called after subdevices are set up */
	int (*post_init)(struct wm831x *wm831x);

	int gpio_base;
	struct wm831x_backlight_pdata *backlight;
	struct wm831x_backup_pdata *backup;
	struct wm831x_battery_pdata *battery;
	struct wm831x_touch_pdata *touch;
	struct wm831x_watchdog_pdata *watchdog;

	/** LED1 = 0 and so on */
	struct wm831x_status_pdata *status[WM831X_MAX_STATUS];
	/** DCDC1 = 0 and so on */
	struct regulator_init_data *dcdc[WM831X_MAX_DCDC];
	/** EPE1 = 0 and so on */
	struct regulator_init_data *epe[WM831X_MAX_EPE];
	/** LDO1 = 0 and so on */
	struct regulator_init_data *ldo[WM831X_MAX_LDO];
	/** ISINK1 = 0 and so on*/
	struct regulator_init_data *isink[WM831X_MAX_ISINK];
};

#endif
