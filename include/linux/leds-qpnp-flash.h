/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#ifndef __LEDS_QPNP_FLASH_H
#define __LEDS_QPNP_FLASH_H

#include <linux/leds.h>

#define ENABLE_REGULATOR		BIT(0)
#define DISABLE_REGULATOR		BIT(1)
#define QUERY_MAX_AVAIL_CURRENT		BIT(2)
#define QUERY_MAX_CURRENT		BIT(3)

#define FLASH_LED_PREPARE_OPTIONS_MASK	GENMASK(3, 0)

int qpnp_flash_register_led_prepare(struct device *dev, void *data);

#if IS_ENABLED(CONFIG_LEDS_QPNP_FLASH_V2)
int qpnp_flash_led_prepare(struct led_trigger *trig, int options,
					int *max_current);
#else
static inline int qpnp_flash_led_prepare(struct led_trigger *trig, int options,
					int *max_current)
{
	return -ENODEV;
}
#endif

#if IS_ENABLED(CONFIG_BACKLIGHT_QCOM_SPMI_WLED)
int wled_flash_led_prepare(struct led_trigger *trig, int options,
					int *max_current);
#else
static inline int wled_flash_led_prepare(struct led_trigger *trig, int options,
					int *max_current)
{
	return -EINVAL;
}
#endif

#endif
