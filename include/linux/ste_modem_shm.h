/*
 * Copyright (C) ST-Ericsson AB 2012
 * Author: Sjur Brendeland / sjur.brandeland@stericsson.com
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __INC_MODEM_DEV_H
#define __INC_MODEM_DEV_H
#include <linux/types.h>
#include <linux/platform_device.h>

struct ste_modem_device;

/**
 * struct ste_modem_dev_cb - Callbacks for modem initiated events.
 * @kick: Called when the modem kicks the host.
 *
 * This structure contains callbacks for actions triggered by the modem.
 */
struct ste_modem_dev_cb {
	void (*kick)(struct ste_modem_device *mdev, int notify_id);
};

/**
 * struct ste_modem_dev_ops - Functions to control modem and modem interface.
 *
 * @power:	Main power switch, used for cold-start or complete power off.
 * @kick:	Kick the modem.
 * @kick_subscribe: Subscribe for notifications from the modem.
 * @setup:	Provide callback functions to modem device.
 *
 * This structure contains functions used by the ste remoteproc driver
 * to manage the modem.
 */
struct ste_modem_dev_ops {
	int (*power)(struct ste_modem_device *mdev, bool on);
	int (*kick)(struct ste_modem_device *mdev, int notify_id);
	int (*kick_subscribe)(struct ste_modem_device *mdev, int notify_id);
	int (*setup)(struct ste_modem_device *mdev,
		     struct ste_modem_dev_cb *cfg);
};

/**
 * struct ste_modem_device - represent the STE modem device
 * @pdev: Reference to platform device
 * @ops: Operations used to manage the modem.
 * @drv_data: Driver private data.
 */
struct ste_modem_device {
	struct platform_device pdev;
	struct ste_modem_dev_ops ops;
	void *drv_data;
};

#endif /*INC_MODEM_DEV_H*/
