/* SPDX-License-Identifier: GPL-2.0 */
/* LED Multicolor class interface
 * Copyright (C) 2019-20 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef _LINUX_MULTICOLOR_LEDS_H_INCLUDED
#define _LINUX_MULTICOLOR_LEDS_H_INCLUDED

#include <linux/leds.h>
#include <dt-bindings/leds/common.h>

/**
 * struct mc_subled - Color component description.
 * @color_index: Color ID.
 * @brightness: Scaled intensity.
 * @intensity: Current intensity.
 * @max_intensity: Maximum supported intensity value.
 * @channel: Channel index.
 *
 * Describes a color component of a multicolor LED. Many multicolor LEDs
 * do not support global brightness control in hardware, so they use
 * the brightness field in connection with led_mc_calc_color_components()
 * to perform the intensity scaling in software.
 * Such drivers should set max_intensity to 0 to signal the multicolor LED core
 * that the maximum global brightness of the LED class device should be used for
 * limiting incoming intensity values.
 *
 * Multicolor LEDs that do support global brightness control in hardware
 * should instead set max_intensity to the maximum intensity value supported
 * by the hardware for a given color component.
 */
struct mc_subled {
	unsigned int color_index;
	unsigned int brightness;
	unsigned int intensity;
	unsigned int max_intensity;
	unsigned int channel;
};

struct led_classdev_mc {
	/* led class device */
	struct led_classdev led_cdev;
	unsigned int num_colors;

	struct mc_subled *subled_info;
};

static inline struct led_classdev_mc *lcdev_to_mccdev(
						struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct led_classdev_mc, led_cdev);
}

/**
 * led_classdev_multicolor_register_ext - register a new object of led_classdev
 *				      class with support for multicolor LEDs
 * @parent: the multicolor LED to register
 * @mcled_cdev: the led_classdev_mc structure for this device
 * @init_data: the LED class multicolor device initialization data
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_classdev_multicolor_register_ext(struct device *parent,
					    struct led_classdev_mc *mcled_cdev,
					    struct led_init_data *init_data);

/**
 * led_classdev_multicolor_unregister - unregisters an object of led_classdev
 *					class with support for multicolor LEDs
 * @mcled_cdev: the multicolor LED to unregister
 *
 * Unregister a previously registered via led_classdev_multicolor_register
 * object
 */
void led_classdev_multicolor_unregister(struct led_classdev_mc *mcled_cdev);

/**
 * led_mc_calc_color_components() - Calculates component brightness values of a LED cluster.
 * @mcled_cdev - Multicolor LED class device of the LED cluster.
 * @brightness - Global brightness of the LED cluster.
 *
 * Calculates the brightness values for each color component of a monochrome LED cluster,
 * see Documentation/leds/leds-class-multicolor.rst for details.
 */
int led_mc_calc_color_components(struct led_classdev_mc *mcled_cdev,
				 enum led_brightness brightness);

int devm_led_classdev_multicolor_register_ext(struct device *parent,
					  struct led_classdev_mc *mcled_cdev,
					  struct led_init_data *init_data);

void devm_led_classdev_multicolor_unregister(struct device *parent,
					    struct led_classdev_mc *mcled_cdev);

static inline int led_classdev_multicolor_register(struct device *parent,
					    struct led_classdev_mc *mcled_cdev)
{
	return led_classdev_multicolor_register_ext(parent, mcled_cdev, NULL);
}

static inline int devm_led_classdev_multicolor_register(struct device *parent,
					     struct led_classdev_mc *mcled_cdev)
{
	return devm_led_classdev_multicolor_register_ext(parent, mcled_cdev,
							 NULL);
}

#endif	/* _LINUX_MULTICOLOR_LEDS_H_INCLUDED */
