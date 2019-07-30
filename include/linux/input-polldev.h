/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INPUT_POLLDEV_H
#define _INPUT_POLLDEV_H

/*
 * Copyright (c) 2007 Dmitry Torokhov
 */

#include <linux/input.h>
#include <linux/workqueue.h>

/**
 * struct input_polled_dev - simple polled input device
 * @private: private driver data.
 * @open: driver-supplied method that prepares device for polling
 *	(enabled the device and maybe flushes device state).
 * @close: driver-supplied method that is called when device is no
 *	longer being polled. Used to put device into low power mode.
 * @poll: driver-supplied method that polls the device and posts
 *	input events (mandatory).
 * @poll_interval: specifies how often the poll() method should be called.
 *	Defaults to 500 msec unless overridden when registering the device.
 * @poll_interval_max: specifies upper bound for the poll interval.
 *	Defaults to the initial value of @poll_interval.
 * @poll_interval_min: specifies lower bound for the poll interval.
 *	Defaults to 0.
 * @input: input device structure associated with the polled device.
 *	Must be properly initialized by the driver (id, name, phys, bits).
 *
 * Polled input device provides a skeleton for supporting simple input
 * devices that do not raise interrupts but have to be periodically
 * scanned or polled to detect changes in their state.
 */
struct input_polled_dev {
	void *private;

	void (*open)(struct input_polled_dev *dev);
	void (*close)(struct input_polled_dev *dev);
	void (*poll)(struct input_polled_dev *dev);
	unsigned int poll_interval; /* msec */
	unsigned int poll_interval_max; /* msec */
	unsigned int poll_interval_min; /* msec */

	struct input_dev *input;

/* private: */
	struct delayed_work work;

	bool devres_managed;
};

struct input_polled_dev *input_allocate_polled_device(void);
struct input_polled_dev *devm_input_allocate_polled_device(struct device *dev);
void input_free_polled_device(struct input_polled_dev *dev);
int input_register_polled_device(struct input_polled_dev *dev);
void input_unregister_polled_device(struct input_polled_dev *dev);

#endif
