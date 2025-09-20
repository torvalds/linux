// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 */

#include <libudev.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <getopt.h>

#include "usbip_common.h"
#include "utils.h"
#include "usbip.h"
#include "sysfs_utils.h"

static const char usbip_unbind_usage_string[] =
	"usbip unbind <args>\n"
	"    -b, --busid=<busid>    Unbind " USBIP_HOST_DRV_NAME ".ko from "
	"device on <busid>\n";

void usbip_unbind_usage(void)
{
	printf("usage: %s", usbip_unbind_usage_string);
}

static int unbind_device(char *busid)
{
	char bus_type[] = "usb";
	int rc, ret = -1;

	char unbind_attr_name[] = "unbind";
	char unbind_attr_path[SYSFS_PATH_MAX];
	char rebind_attr_name[] = "rebind";
	char rebind_attr_path[SYSFS_PATH_MAX];

	struct udev *udev;
	struct udev_device *dev;
	const char *driver;

	/* Create libudev context. */
	udev = udev_new();

	/* Check whether the device with this bus ID exists. */
	dev = udev_device_new_from_subsystem_sysname(udev, "usb", busid);
	if (!dev) {
		err("device with the specified bus ID does not exist");
		goto err_close_udev;
	}

	/* Check whether the device is using usbip-host driver. */
	driver = udev_device_get_driver(dev);
	if (!driver || strcmp(driver, "usbip-host")) {
		err("device is not bound to usbip-host driver");
		goto err_close_udev;
	}

	/* Unbind device from driver. */
	snprintf(unbind_attr_path, sizeof(unbind_attr_path), "%s/%s/%s/%s/%s/%s",
		 SYSFS_MNT_PATH, SYSFS_BUS_NAME, bus_type, SYSFS_DRIVERS_NAME,
		 USBIP_HOST_DRV_NAME, unbind_attr_name);

	rc = write_sysfs_attribute(unbind_attr_path, busid, strlen(busid));
	if (rc < 0) {
		err("error unbinding device %s from driver", busid);
		goto err_close_udev;
	}

	/* Notify driver of unbind. */
	rc = modify_match_busid(busid, 0);
	if (rc < 0) {
		err("unable to unbind device on %s", busid);
		goto err_close_udev;
	}

	/* Trigger new probing. */
	snprintf(rebind_attr_path, sizeof(unbind_attr_path), "%s/%s/%s/%s/%s/%s",
			SYSFS_MNT_PATH, SYSFS_BUS_NAME, bus_type, SYSFS_DRIVERS_NAME,
			USBIP_HOST_DRV_NAME, rebind_attr_name);

	rc = write_sysfs_attribute(rebind_attr_path, busid, strlen(busid));
	if (rc < 0) {
		err("error rebinding");
		goto err_close_udev;
	}

	ret = 0;
	info("unbind device on busid %s: complete", busid);

err_close_udev:
	udev_device_unref(dev);
	udev_unref(udev);

	return ret;
}

int usbip_unbind(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "busid", required_argument, NULL, 'b' },
		{ NULL,    0,                 NULL,  0  }
	};

	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'b':
			ret = unbind_device(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_unbind_usage();
out:
	return ret;
}
