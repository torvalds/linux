/*
 * db8500_thermal.h - DB8500 Thermal Management Implementation
 *
 * Copyright (C) 2012 ST-Ericsson
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Hongbo Zhang <hongbo.zhang@linaro.com>
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
 */

#ifndef _DB8500_THERMAL_H_
#define _DB8500_THERMAL_H_

#include <linux/thermal.h>

#define COOLING_DEV_MAX 8

struct db8500_trip_point {
	unsigned long temp;
	enum thermal_trip_type type;
	char cdev_name[COOLING_DEV_MAX][THERMAL_NAME_LENGTH];
};

struct db8500_thsens_platform_data {
	struct db8500_trip_point trip_points[THERMAL_MAX_TRIPS];
	int num_trips;
};

#endif /* _DB8500_THERMAL_H_ */
