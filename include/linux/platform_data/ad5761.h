/*
 * AD5721, AD5721R, AD5761, AD5761R, Voltage Output Digital to Analog Converter
 *
 * Copyright 2016 Qtechnology A/S
 * 2016 Ricardo Ribalda <ricardo.ribalda@gmail.com>
 *
 * Licensed under the GPL-2.
 */
#ifndef __LINUX_PLATFORM_DATA_AD5761_H__
#define __LINUX_PLATFORM_DATA_AD5761_H__

/**
 * enum ad5761_voltage_range - Voltage range the AD5761 is configured for.
 * @AD5761_VOLTAGE_RANGE_M10V_10V:  -10V to  10V
 * @AD5761_VOLTAGE_RANGE_0V_10V:      0V to  10V
 * @AD5761_VOLTAGE_RANGE_M5V_5V:     -5V to   5V
 * @AD5761_VOLTAGE_RANGE_0V_5V:       0V to   5V
 * @AD5761_VOLTAGE_RANGE_M2V5_7V5: -2.5V to 7.5V
 * @AD5761_VOLTAGE_RANGE_M3V_3V:     -3V to   3V
 * @AD5761_VOLTAGE_RANGE_0V_16V:      0V to  16V
 * @AD5761_VOLTAGE_RANGE_0V_20V:      0V to  20V
 */

enum ad5761_voltage_range {
	AD5761_VOLTAGE_RANGE_M10V_10V,
	AD5761_VOLTAGE_RANGE_0V_10V,
	AD5761_VOLTAGE_RANGE_M5V_5V,
	AD5761_VOLTAGE_RANGE_0V_5V,
	AD5761_VOLTAGE_RANGE_M2V5_7V5,
	AD5761_VOLTAGE_RANGE_M3V_3V,
	AD5761_VOLTAGE_RANGE_0V_16V,
	AD5761_VOLTAGE_RANGE_0V_20V,
};

/**
 * struct ad5761_platform_data - AD5761 DAC driver platform data
 * @voltage_range: Voltage range the AD5761 is configured for
 */

struct ad5761_platform_data {
	enum ad5761_voltage_range voltage_range;
};

#endif
