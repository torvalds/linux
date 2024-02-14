/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012, Anish Kumar <anish198519851985@gmail.com>
 */

#ifndef GENERIC_ADC_BATTERY_H
#define GENERIC_ADC_BATTERY_H

/**
 * struct gab_platform_data - platform_data for generic adc iio battery driver.
 * @battery_info:         recommended structure to specify static power supply
 *			   parameters
 * @cal_charge:           calculate charge level.
 * @jitter_delay:         delay required after the interrupt to check battery
 *			  status.Default set is 10ms.
 */
struct gab_platform_data {
	struct power_supply_info battery_info;
	int	(*cal_charge)(long value);
	int     jitter_delay;
};

#endif /* GENERIC_ADC_BATTERY_H */
