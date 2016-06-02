/**
 * portmux.h - USB Port Mux definitions
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_USB_PORTMUX_H
#define __LINUX_USB_PORTMUX_H

#include <linux/device.h>

/**
 * struct portmux_ops - ops two switch the port
 *
 * @set_host_cb: callback for switching port to host
 * @set_device_cb: callback for switching port to device
 */
struct portmux_ops {
	int (*set_host_cb)(struct device *dev);
	int (*set_device_cb)(struct device *dev);
};

/**
 * struct portmux_desc - port mux device descriptor
 *
 * @name: the name of the mux device
 * @dev: the parent of the mux device
 * @ops: ops to switch the port role
 */
struct portmux_desc {
	const char *name;
	struct device *dev;
	const struct portmux_ops *ops;
};

/**
 * enum portmux_role - role of the port
 */
enum portmux_role {
	PORTMUX_UNKNOWN,
	PORTMUX_HOST,
	PORTMUX_DEVICE,
};

/**
 * struct portmux_dev - A mux device
 *
 * @desc: the descriptor of the mux
 * @dev: device of this mux
 * @mux_mutex: lock to serialize port switch operation
 * @mux_state: state of the mux
 */
struct portmux_dev {
	const struct portmux_desc *desc;
	struct device dev;

	/* lock for mux_state */
	struct mutex mux_mutex;
	enum portmux_role mux_state;
};

/*
 * Functions for mux driver
 */
struct portmux_dev *portmux_register(struct portmux_desc *desc);
void portmux_unregister(struct portmux_dev *pdev);
#ifdef CONFIG_PM_SLEEP
void portmux_complete(struct portmux_dev *pdev);
#endif

/*
 * Functions for mux consumer
 */
#if defined(CONFIG_USB_PORTMUX)
int portmux_switch(struct portmux_dev *pdev, enum portmux_role role);
#else
static inline int portmux_switch(struct portmux_dev *pdev,
				 enum portmux_role role)
{
	return 0;
}
#endif

#endif /* __LINUX_USB_PORTMUX_H */
