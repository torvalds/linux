/*
 * /include/media/exynos_fimc_is_sensor.h
 *
 * Copyright (C) 2012 Samsung Electronics, Co. Ltd
 *
 * Exynos series exynos_fimc_is_sensor device support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MEDIA_EXYNOS_SENSOR_H
#define MEDIA_EXYNOS_SENSOR_H

#include "exynos_fimc_is.h"

/*
 * struct exynos_fimc_is_sensor_platform_data - platform data for exynos_sensor driver
 * @irq: GPIO getting the irq pin of exynos_sensor
 * @gpio_rst: GPIO driving the reset pin of exynos_sensor
 * @enable_rst: the pin state when reset pin is enabled
 * @clk_on/off: sensor clock on/off
 * @set_power: an additional callback to a board setup code
 *			to be called after enabling and before disabling
 *			the exynos_sensor device supply regulators
 */
struct exynos_fimc_is_sensor_platform_data {
	int (*clk_on)(struct device *pdev, int sensor_id);
	int (*clk_off)(struct device *pdev, int sensor_id);
	int (*set_power)(struct device *dev, int on);
	struct exynos5_sensor_gpio_info *gpio_info;
	int irq;
	int gpio_rst;
	bool enable_rst;
};

extern int exynos5_fimc_is_sensor_clk_on(struct device *pdev, int sensor_id);
extern int exynos5_fimc_is_sensor_clk_off(struct device *pdev, int sensor_id);

#endif /* MEDIA_EXYNOS_SENSOR_H */
