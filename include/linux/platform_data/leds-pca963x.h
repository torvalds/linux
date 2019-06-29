/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCA963X LED chip driver.
 *
 * Copyright 2012 bct electronic GmbH
 * Copyright 2013 Qtechnology A/S
 */

#ifndef __LINUX_PCA963X_H
#define __LINUX_PCA963X_H
#include <linux/leds.h>

enum pca963x_outdrv {
	PCA963X_OPEN_DRAIN,
	PCA963X_TOTEM_POLE, /* aka push-pull */
};

enum pca963x_blink_type {
	PCA963X_SW_BLINK,
	PCA963X_HW_BLINK,
};

enum pca963x_direction {
	PCA963X_NORMAL,
	PCA963X_INVERTED,
};

struct pca963x_platform_data {
	struct led_platform_data leds;
	enum pca963x_outdrv outdrv;
	enum pca963x_blink_type blink_type;
	enum pca963x_direction dir;
};

#endif /* __LINUX_PCA963X_H*/
