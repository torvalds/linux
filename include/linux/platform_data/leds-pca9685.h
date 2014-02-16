/*
 * Copyright 2013 Maximilian Güntner <maximilian.guentner@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Based on leds-pca963x.h by Peter Meerwald <p.meerwald@bct-electronic.com>
 *
 * LED driver for the NXP PCA9685 PWM chip
 *
 */

#ifndef __LINUX_PCA9685_H
#define __LINUX_PCA9685_H

#include <linux/leds.h>

enum pca9685_outdrv {
	PCA9685_OPEN_DRAIN,
	PCA9685_TOTEM_POLE,
};

enum pca9685_inverted {
	PCA9685_NOT_INVERTED,
	PCA9685_INVERTED,
};

struct pca9685_platform_data {
	struct led_platform_data leds;
	enum pca9685_outdrv outdrv;
	enum pca9685_inverted inverted;
};

#endif /* __LINUX_PCA9685_H */
