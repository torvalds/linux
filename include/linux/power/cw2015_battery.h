/*
 * Fuel gauge driver for CellWise 2013 / 2015
 *
 * Copyright (C) 2012, RockChip
 *
 * Authors: xuhuicong <xhc@rock-chips.com>
 *
 * Based on rk30_adc_battery.c

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef CW2015_BATTERY_H
#define CW2015_BATTERY_H

#define SIZE_BATINFO    64

#define CW2015_GPIO_HIGH  1
#define CW2015_GPIO_LOW   0

#define REG_VERSION             0x0
#define REG_VCELL               0x2
#define REG_SOC                 0x4
#define REG_RRT_ALERT           0x6
#define REG_CONFIG              0x8
#define REG_MODE                0xA
#define REG_BATINFO             0x10

#define MODE_SLEEP_MASK         (0x3<<6)
#define MODE_SLEEP              (0x3<<6)
#define MODE_NORMAL             (0x0<<6)
#define MODE_QUICK_START        (0x3<<4)
#define MODE_RESTART            (0xf<<0)

#define CONFIG_UPDATE_FLG       (0x1<<1)
#define ATHD                    (0x0<<3)

#define CW_I2C_SPEED			100000
#define BATTERY_UP_MAX_CHANGE		(420 * 1000)
#define BATTERY_DOWN_MAX_CHANGE		(120 * 1000)
#define BATTERY_DOWN_CHANGE		60
#define BATTERY_DOWN_MIN_CHANGE_RUN	30
#define BATTERY_DOWN_MIN_CHANGE_SLEEP	1800
#define BATTERY_JUMP_TO_ZERO		(30 * 1000)
#define BATTERY_CAPACITY_ERROR		(40 * 1000)
#define BATTERY_CHARGING_ZERO		(1800 * 1000)

#define DOUBLE_SERIES_BATTERY	0

#define CHARGING_ON		1
#define NO_CHARGING		0

#define BATTERY_DOWN_MAX_CHANGE_RUN_AC_ONLINE 3600

#define NO_STANDARD_AC_BIG_CHARGE_MODE 1
/* #define SYSTEM_SHUTDOWN_VOLTAGE  3400000 */
#define BAT_LOW_INTERRUPT    1

#define USB_CHARGER_MODE        1
#define AC_CHARGER_MODE         2
#define   CW_QUICKSTART         0

#define TIMER_MS_COUNTS			1000
#define DEFAULT_MONITOR_SEC		8

/* virtual params */
#define VIRTUAL_CURRENT			1000
#define VIRTUAL_VOLTAGE			3888
#define VIRTUAL_SOC			66
#define VIRTUAL_PRESET			1
#define VIRTUAL_TEMPERATURE		188
#define VIRTUAL_TIME2EMPTY		60
#define VIRTUAL_STATUS			POWER_SUPPLY_STATUS_CHARGING

enum bat_mode {
	MODE_BATTARY = 0,
	MODE_VIRTUAL,
};

struct cw_bat_platform_data {
	int divider_res1;
	int divider_res2;
	u32 *cw_bat_config_info;
};

struct cw_battery {
	struct i2c_client *client;
	struct workqueue_struct *battery_workqueue;
	struct delayed_work battery_delay_work;
	struct cw_bat_platform_data plat_data;

	struct power_supply *rk_bat;

	struct power_supply *chrg_usb_psy;
	struct power_supply *chrg_ac_psy;

#ifdef CONFIG_PM
	struct timespec suspend_time_before;
	struct timespec after;
	int suspend_resume_mark;
#endif
	int charger_mode;
	int capacity;
	int voltage;
	int status;
	int time_to_empty;
	int alt;
	u32 monitor_sec;
	u32 bat_mode;
	int bat_change;
};

#endif
