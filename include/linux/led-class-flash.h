/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LED Flash class interface
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 */
#ifndef __LINUX_FLASH_LEDS_H_INCLUDED
#define __LINUX_FLASH_LEDS_H_INCLUDED

#include <linux/leds.h>

struct device_node;
struct led_classdev_flash;

/*
 * Supported led fault bits - must be kept in synch
 * with V4L2_FLASH_FAULT bits.
 */
#define LED_FAULT_OVER_VOLTAGE		(1 << 0)
#define LED_FAULT_TIMEOUT		(1 << 1)
#define LED_FAULT_OVER_TEMPERATURE	(1 << 2)
#define LED_FAULT_SHORT_CIRCUIT		(1 << 3)
#define LED_FAULT_OVER_CURRENT		(1 << 4)
#define LED_FAULT_INDICATOR		(1 << 5)
#define LED_FAULT_UNDER_VOLTAGE		(1 << 6)
#define LED_FAULT_INPUT_VOLTAGE		(1 << 7)
#define LED_FAULT_LED_OVER_TEMPERATURE	(1 << 8)
#define LED_NUM_FLASH_FAULTS		9

#define LED_FLASH_SYSFS_GROUPS_SIZE	5

struct led_flash_ops {
	/* set flash brightness */
	int (*flash_brightness_set)(struct led_classdev_flash *fled_cdev,
					u32 brightness);
	/* get flash brightness */
	int (*flash_brightness_get)(struct led_classdev_flash *fled_cdev,
					u32 *brightness);
	/* set flash strobe state */
	int (*strobe_set)(struct led_classdev_flash *fled_cdev, bool state);
	/* get flash strobe state */
	int (*strobe_get)(struct led_classdev_flash *fled_cdev, bool *state);
	/* set flash timeout */
	int (*timeout_set)(struct led_classdev_flash *fled_cdev, u32 timeout);
	/* get the flash LED fault */
	int (*fault_get)(struct led_classdev_flash *fled_cdev, u32 *fault);
};

/*
 * Current value of a flash setting along
 * with its constraints.
 */
struct led_flash_setting {
	/* maximum allowed value */
	u32 min;
	/* maximum allowed value */
	u32 max;
	/* step value */
	u32 step;
	/* current value */
	u32 val;
};

struct led_classdev_flash {
	/* led class device */
	struct led_classdev led_cdev;

	/* flash led specific ops */
	const struct led_flash_ops *ops;

	/* flash brightness value in microamperes along with its constraints */
	struct led_flash_setting brightness;

	/* flash timeout value in microseconds along with its constraints */
	struct led_flash_setting timeout;

	/* LED Flash class sysfs groups */
	const struct attribute_group *sysfs_groups[LED_FLASH_SYSFS_GROUPS_SIZE];
};

static inline struct led_classdev_flash *lcdev_to_flcdev(
						struct led_classdev *lcdev)
{
	return container_of(lcdev, struct led_classdev_flash, led_cdev);
}

#if IS_ENABLED(CONFIG_LEDS_CLASS_FLASH)
/**
 * led_classdev_flash_register_ext - register a new object of LED class with
 *				     init data and with support for flash LEDs
 * @parent: LED flash controller device this flash LED is driven by
 * @fled_cdev: the led_classdev_flash structure for this device
 * @init_data: the LED class flash device initialization data
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_classdev_flash_register_ext(struct device *parent,
				    struct led_classdev_flash *fled_cdev,
				    struct led_init_data *init_data);

/**
 * led_classdev_flash_unregister - unregisters an object of led_classdev class
 *				   with support for flash LEDs
 * @fled_cdev: the flash LED to unregister
 *
 * Unregister a previously registered via led_classdev_flash_register object
 */
void led_classdev_flash_unregister(struct led_classdev_flash *fled_cdev);

int devm_led_classdev_flash_register_ext(struct device *parent,
				     struct led_classdev_flash *fled_cdev,
				     struct led_init_data *init_data);


void devm_led_classdev_flash_unregister(struct device *parent,
					struct led_classdev_flash *fled_cdev);

#else

static inline int led_classdev_flash_register_ext(struct device *parent,
				    struct led_classdev_flash *fled_cdev,
				    struct led_init_data *init_data)
{
	return 0;
}

static inline void led_classdev_flash_unregister(struct led_classdev_flash *fled_cdev) {};
static inline int devm_led_classdev_flash_register_ext(struct device *parent,
				     struct led_classdev_flash *fled_cdev,
				     struct led_init_data *init_data)
{
	return 0;
}

static inline void devm_led_classdev_flash_unregister(struct device *parent,
					struct led_classdev_flash *fled_cdev)
{};

#endif  /* IS_ENABLED(CONFIG_LEDS_CLASS_FLASH) */

static inline int led_classdev_flash_register(struct device *parent,
					   struct led_classdev_flash *fled_cdev)
{
	return led_classdev_flash_register_ext(parent, fled_cdev, NULL);
}

static inline int devm_led_classdev_flash_register(struct device *parent,
				     struct led_classdev_flash *fled_cdev)
{
	return devm_led_classdev_flash_register_ext(parent, fled_cdev, NULL);
}

/**
 * led_set_flash_strobe - setup flash strobe
 * @fled_cdev: the flash LED to set strobe on
 * @state: 1 - strobe flash, 0 - stop flash strobe
 *
 * Strobe the flash LED.
 *
 * Returns: 0 on success or negative error value on failure
 */
static inline int led_set_flash_strobe(struct led_classdev_flash *fled_cdev,
					bool state)
{
	if (!fled_cdev)
		return -EINVAL;
	return fled_cdev->ops->strobe_set(fled_cdev, state);
}

/**
 * led_get_flash_strobe - get flash strobe status
 * @fled_cdev: the flash LED to query
 * @state: 1 - flash is strobing, 0 - flash is off
 *
 * Check whether the flash is strobing at the moment.
 *
 * Returns: 0 on success or negative error value on failure
 */
static inline int led_get_flash_strobe(struct led_classdev_flash *fled_cdev,
					bool *state)
{
	if (!fled_cdev)
		return -EINVAL;
	if (fled_cdev->ops->strobe_get)
		return fled_cdev->ops->strobe_get(fled_cdev, state);

	return -EINVAL;
}

/**
 * led_set_flash_brightness - set flash LED brightness
 * @fled_cdev: the flash LED to set
 * @brightness: the brightness to set it to
 *
 * Set a flash LED's brightness.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_set_flash_brightness(struct led_classdev_flash *fled_cdev,
			     u32 brightness);

/**
 * led_update_flash_brightness - update flash LED brightness
 * @fled_cdev: the flash LED to query
 *
 * Get a flash LED's current brightness and update led_flash->brightness
 * member with the obtained value.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_update_flash_brightness(struct led_classdev_flash *fled_cdev);

/**
 * led_set_flash_timeout - set flash LED timeout
 * @fled_cdev: the flash LED to set
 * @timeout: the flash timeout to set it to
 *
 * Set the flash strobe duration.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_set_flash_timeout(struct led_classdev_flash *fled_cdev, u32 timeout);

/**
 * led_get_flash_fault - get the flash LED fault
 * @fled_cdev: the flash LED to query
 * @fault: bitmask containing flash faults
 *
 * Get the flash LED fault.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_get_flash_fault(struct led_classdev_flash *fled_cdev, u32 *fault);

#endif	/* __LINUX_FLASH_LEDS_H_INCLUDED */
