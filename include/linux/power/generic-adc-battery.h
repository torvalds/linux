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
 */
struct gab_platform_data {
	struct power_supply_info battery_info;
};

#endif /* GENERIC_ADC_BATTERY_H */
