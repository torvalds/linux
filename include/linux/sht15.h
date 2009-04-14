/*
 * sht15.h - support for the SHT15 Temperature and Humidity Sensor
 *
 * Copyright (c) 2009 Jonathan Cameron
 *
 * Copyright (c) 2007 Wouter Horre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * struct sht15_platform_data - sht15 connectivity info
 * @gpio_data:	no. of gpio to which bidirectional data line is connected
 * @gpio_sck:	no. of gpio to which the data clock is connected.
 * @supply_mv:	supply voltage in mv. Overridden by regulator if available.
 **/
struct sht15_platform_data {
	int gpio_data;
	int gpio_sck;
	int supply_mv;
};

