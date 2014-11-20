/*
 * RockChip ADC Battery Driver
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

#ifndef RK_ADC_BATTERY_H
#define RK_ADC_BATTERY_H


/* adc battery */
struct rk30_adc_battery_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*is_dc_charging)(void);
	int (*charging_ok)(void);

	int (*is_usb_charging)(void);
	int spport_usb_charging;
	int (*control_usb_charging)(int);
	int usb_det_pin;
	int dc_det_pin;
	int batt_low_pin;
	int charge_ok_pin;
	int charge_set_pin;
	int back_light_pin;

	int ctrl_charge_led_pin;
	int ctrl_charge_enable;
	void (*ctrl_charge_led)(int);

	int dc_det_level;
	int batt_low_level;
	int charge_ok_level;
	int charge_set_level;
	int usb_det_level;

	int adc_channel;

	int dc_det_pin_pull;
	int batt_low_pin_pull;
	int charge_ok_pin_pull;
	int charge_set_pin_pull;

	int low_voltage_protection;

	int charging_sleep;

	int is_reboot_charging;
	int save_capacity;

	int reference_voltage;
	int pull_up_res;
	int pull_down_res;

	int time_down_discharge;
	int time_up_charge;
	int  use_board_table;
	int  table_size;
	int  *discharge_table;
	int  *charge_table;
	int  *property_tabel;
	int *board_batt_table;
	int *dts_batt_table;

	int is_dc_charge;
	int is_usb_charge;
	int auto_calibration;
	struct iio_channel *chan;
	struct iio_channel *ref_voltage_chan;
};
#endif
