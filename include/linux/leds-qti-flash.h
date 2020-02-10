/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __LEDS_QTI_FLASH_H
#define __LEDS_QTI_FLASH_H

#include <linux/leds.h>

#define QUERY_MAX_AVAIL_CURRENT		BIT(0)

#if IS_ENABLED(CONFIG_LEDS_QTI_FLASH)
int qti_flash_led_prepare(struct led_trigger *trig,
			int options, int *max_current);
#else
static inline int qti_flash_led_prepare(struct led_trigger *trig,
					int options, int *max_current)
{
	return -EINVAL;
}
#endif
#endif
