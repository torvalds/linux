// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 */

#include <unistd.h>
#include <libudev.h>

#include "usbip_host_common.h"
#include "usbip_host_driver.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

static int is_my_device(struct udev_device *dev)
{
	const char *driver;

	driver = udev_device_get_driver(dev);
	return driver != NULL && !strcmp(driver, USBIP_HOST_DRV_NAME);
}

static int usbip_host_driver_open(struct usbip_host_driver *hdriver)
{
	int ret;

	hdriver->ndevs = 0;
	INIT_LIST_HEAD(&hdriver->edev_list);

	ret = usbip_generic_driver_open(hdriver);
	if (ret)
		err("please load " USBIP_CORE_MOD_NAME ".ko and "
		    USBIP_HOST_DRV_NAME ".ko!");
	return ret;
}

struct usbip_host_driver host_driver = {
	.edev_list = LIST_HEAD_INIT(host_driver.edev_list),
	.udev_subsystem = "usb",
	.ops = {
		.open = usbip_host_driver_open,
		.close = usbip_generic_driver_close,
		.refresh_device_list = usbip_generic_refresh_device_list,
		.get_device = usbip_generic_get_device,
		.read_device = read_usb_device,
		.read_interface = read_usb_interface,
		.is_my_device = is_my_device,
	},
};
