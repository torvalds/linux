/*
 * V4L2 flash LED sub-device registration helpers.
 *
 *	Copyright (C) 2015 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _V4L2_FLASH_H
#define _V4L2_FLASH_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

struct led_classdev_flash;
struct led_classdev;
struct v4l2_flash;
enum led_brightness;

/**
 * struct v4l2_flash_ctrl_data - flash control initialization data, filled
 *				basing on the features declared by the LED flash
 *				class driver in the v4l2_flash_config
 * @config:	initialization data for a control
 * @cid:	contains v4l2 flash control id if the config
 *		field was initialized, 0 otherwise
 */
struct v4l2_flash_ctrl_data {
	struct v4l2_ctrl_config config;
	u32 cid;
};

/**
 * struct v4l2_flash_ops - V4L2 flash operations
 *
 * @external_strobe_set: Setup strobing the flash by hardware pin state
 *	assertion.
 * @intensity_to_led_brightness: Convert intensity to brightness in a device
 *	specific manner
 * @led_brightness_to_intensity: convert brightness to intensity in a device
 *	specific manner.
 */
struct v4l2_flash_ops {
	int (*external_strobe_set)(struct v4l2_flash *v4l2_flash,
					bool enable);
	enum led_brightness (*intensity_to_led_brightness)
		(struct v4l2_flash *v4l2_flash, s32 intensity);
	s32 (*led_brightness_to_intensity)
		(struct v4l2_flash *v4l2_flash, enum led_brightness);
};

/**
 * struct v4l2_flash_config - V4L2 Flash sub-device initialization data
 * @dev_name:			the name of the media entity,
 *				unique in the system
 * @intensity:			non-flash strobe constraints for the LED
 * @flash_faults:		bitmask of flash faults that the LED flash class
 *				device can report; corresponding LED_FAULT* bit
 *				definitions are available in the header file
 *				<linux/led-class-flash.h>
 * @has_external_strobe:	external strobe capability
 */
struct v4l2_flash_config {
	char dev_name[32];
	struct led_flash_setting intensity;
	u32 flash_faults;
	unsigned int has_external_strobe:1;
};

/**
 * struct v4l2_flash - Flash sub-device context
 * @fled_cdev:		LED flash class device controlled by this sub-device
 * @iled_cdev:		LED class device representing indicator LED associated
 *			with the LED flash class device
 * @ops:		V4L2 specific flash ops
 * @sd:			V4L2 sub-device
 * @hdl:		flash controls handler
 * @ctrls:		array of pointers to controls, whose values define
 *			the sub-device state
 */
struct v4l2_flash {
	struct led_classdev_flash *fled_cdev;
	struct led_classdev *iled_cdev;
	const struct v4l2_flash_ops *ops;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl **ctrls;
};

static inline struct v4l2_flash *v4l2_subdev_to_v4l2_flash(
							struct v4l2_subdev *sd)
{
	return container_of(sd, struct v4l2_flash, sd);
}

static inline struct v4l2_flash *v4l2_ctrl_to_v4l2_flash(struct v4l2_ctrl *c)
{
	return container_of(c->handler, struct v4l2_flash, hdl);
}

#if IS_ENABLED(CONFIG_V4L2_FLASH_LED_CLASS)
/**
 * v4l2_flash_init - initialize V4L2 flash led sub-device
 * @dev:	flash device, e.g. an I2C device
 * @fwn:	fwnode_handle of the LED, may be NULL if the same as device's
 * @fled_cdev:	LED flash class device to wrap
 * @ops:	V4L2 Flash device ops
 * @config:	initialization data for V4L2 Flash sub-device
 *
 * Create V4L2 Flash sub-device wrapping given LED subsystem device.
 * The ops pointer is stored by the V4L2 flash framework. No
 * references are held to config nor its contents once this function
 * has returned.
 *
 * Returns: A valid pointer, or, when an error occurs, the return
 * value is encoded using ERR_PTR(). Use IS_ERR() to check and
 * PTR_ERR() to obtain the numeric return value.
 */
struct v4l2_flash *v4l2_flash_init(
	struct device *dev, struct fwnode_handle *fwn,
	struct led_classdev_flash *fled_cdev,
	const struct v4l2_flash_ops *ops, struct v4l2_flash_config *config);

/**
 * v4l2_flash_indicator_init - initialize V4L2 indicator sub-device
 * @dev:	flash device, e.g. an I2C device
 * @fwn:	fwnode_handle of the LED, may be NULL if the same as device's
 * @iled_cdev:	LED flash class device representing the indicator LED
 * @config:	initialization data for V4L2 Flash sub-device
 *
 * Create V4L2 Flash sub-device wrapping given LED subsystem device.
 * The ops pointer is stored by the V4L2 flash framework. No
 * references are held to config nor its contents once this function
 * has returned.
 *
 * Returns: A valid pointer, or, when an error occurs, the return
 * value is encoded using ERR_PTR(). Use IS_ERR() to check and
 * PTR_ERR() to obtain the numeric return value.
 */
struct v4l2_flash *v4l2_flash_indicator_init(
	struct device *dev, struct fwnode_handle *fwn,
	struct led_classdev *iled_cdev, struct v4l2_flash_config *config);

/**
 * v4l2_flash_release - release V4L2 Flash sub-device
 * @v4l2_flash: the V4L2 Flash sub-device to release
 *
 * Release V4L2 Flash sub-device.
 */
void v4l2_flash_release(struct v4l2_flash *v4l2_flash);

#else
static inline struct v4l2_flash *v4l2_flash_init(
	struct device *dev, struct fwnode_handle *fwn,
	struct led_classdev_flash *fled_cdev,
	const struct v4l2_flash_ops *ops, struct v4l2_flash_config *config)
{
	return NULL;
}

static inline struct v4l2_flash *v4l2_flash_indicator_init(
	struct device *dev, struct fwnode_handle *fwn,
	struct led_classdev *iled_cdev, struct v4l2_flash_config *config)
{
	return NULL;
}

static inline void v4l2_flash_release(struct v4l2_flash *v4l2_flash)
{
}
#endif /* CONFIG_V4L2_FLASH_LED_CLASS */

#endif /* _V4L2_FLASH_H */
