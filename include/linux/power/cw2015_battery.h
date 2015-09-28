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

#define CW_I2C_SPEED            100000
#define BATTERY_UP_MAX_CHANGE   420
#define BATTERY_DOWN_CHANGE   60
#define BATTERY_DOWN_MIN_CHANGE_RUN 30
#define BATTERY_DOWN_MIN_CHANGE_SLEEP 1800

#define BATTERY_DOWN_MAX_CHANGE_RUN_AC_ONLINE 3600

#define NO_STANDARD_AC_BIG_CHARGE_MODE 1
/* #define SYSTEM_SHUTDOWN_VOLTAGE  3400000 */
#define BAT_LOW_INTERRUPT    1

#define USB_CHARGER_MODE        1
#define AC_CHARGER_MODE         2
#define   CW_QUICKSTART         0

struct cw_bat_platform_data {
	int is_dc_charge;
	int dc_det_pin;
	int dc_det_level;

	int is_usb_charge;
	int chg_mode_sel_pin;
	int chg_mode_sel_level;

	int bat_low_pin;
	int bat_low_level;
	int chg_ok_pin;
	int chg_ok_level;
	u32 *cw_bat_config_info;
};

struct cw_battery {
	struct i2c_client *client;
	struct workqueue_struct *battery_workqueue;
	struct delayed_work battery_delay_work;
	struct delayed_work dc_wakeup_work;
	struct delayed_work bat_low_wakeup_work;
	struct cw_bat_platform_data plat_data;

	struct power_supply rk_bat;
	struct power_supply rk_ac;
	struct power_supply rk_usb;

	long sleep_time_capacity_change;
	long run_time_capacity_change;

	long sleep_time_charge_start;
	long run_time_charge_start;

	int dc_online;
	int usb_online;
	int charger_mode;
	int charger_init_mode;
	int capacity;
	int voltage;
	int status;
	int time_to_empty;
	int alt;

	int bat_change;
};

#if defined(CONFIG_ARCH_ROCKCHIP)
int get_gadget_connect_flag(void);
int dwc_otg_check_dpdm(void);
void rk_send_wakeup_key(void);
int dwc_vbus_status(void);
#else
static inline int get_gadget_connect_flag(void)
{
	return 0;
}

static inline int dwc_otg_check_dpdm(bool wait)
{
	return 0;
}

static inline void rk_send_wakeup_key(void)
{
}

static inline int dwc_vbus_status(void);
{
	return 0;
}
#endif

#endif
