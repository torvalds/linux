/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AK4641 ALSA SoC Codec driver
 *
 * Copyright 2009 Philipp Zabel
 */

#ifndef __AK4641_H
#define __AK4641_H

/**
 * struct ak4641_platform_data - platform specific AK4641 configuration
 * @gpio_power:	GPIO to control external power to AK4641
 * @gpio_npdn:	GPIO connected to AK4641 nPDN pin
 *
 * Both GPIO parameters are optional.
 */
struct ak4641_platform_data {
	int gpio_power;
	int gpio_npdn;
};

#endif /* __AK4641_H */
