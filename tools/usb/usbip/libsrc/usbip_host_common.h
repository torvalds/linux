/*
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 *
 * Refactored from usbip_host_driver.c, which is:
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __USBIP_HOST_COMMON_H
#define __USBIP_HOST_COMMON_H

#include <stdint.h>
#include <libudev.h>
#include <errno.h>
#include "list.h"
#include "usbip_common.h"
#include "sysfs_utils.h"

struct usbip_host_driver;

struct usbip_host_driver_ops {
	int (*open)(struct usbip_host_driver *hdriver);
	void (*close)(struct usbip_host_driver *hdriver);
	int (*refresh_device_list)(struct usbip_host_driver *hdriver);
	struct usbip_exported_device * (*get_device)(
		struct usbip_host_driver *hdriver, int num);

	int (*read_device)(struct udev_device *sdev,
			   struct usbip_usb_device *dev);
	int (*read_interface)(struct usbip_usb_device *udev, int i,
			      struct usbip_usb_interface *uinf);
	int (*is_my_device)(struct udev_device *udev);
};

struct usbip_host_driver {
	int ndevs;
	/* list of exported device */
	struct list_head edev_list;
	const char *udev_subsystem;
	struct usbip_host_driver_ops ops;
};

struct usbip_exported_device {
	struct udev_device *sudev;
	int32_t status;
	struct usbip_usb_device udev;
	struct list_head node;
	struct usbip_usb_interface uinf[];
};

/* External API to access the driver */
static inline int usbip_driver_open(struct usbip_host_driver *hdriver)
{
	if (!hdriver->ops.open)
		return -EOPNOTSUPP;
	return hdriver->ops.open(hdriver);
}

static inline void usbip_driver_close(struct usbip_host_driver *hdriver)
{
	if (!hdriver->ops.close)
		return;
	hdriver->ops.close(hdriver);
}

static inline int usbip_refresh_device_list(struct usbip_host_driver *hdriver)
{
	if (!hdriver->ops.refresh_device_list)
		return -EOPNOTSUPP;
	return hdriver->ops.refresh_device_list(hdriver);
}

static inline struct usbip_exported_device *
usbip_get_device(struct usbip_host_driver *hdriver, int num)
{
	if (!hdriver->ops.get_device)
		return NULL;
	return hdriver->ops.get_device(hdriver, num);
}

/* Helper functions for implementing driver backend */
int usbip_generic_driver_open(struct usbip_host_driver *hdriver);
void usbip_generic_driver_close(struct usbip_host_driver *hdriver);
int usbip_generic_refresh_device_list(struct usbip_host_driver *hdriver);
int usbip_export_device(struct usbip_exported_device *edev, int sockfd);
struct usbip_exported_device *usbip_generic_get_device(
		struct usbip_host_driver *hdriver, int num);

#endif /* __USBIP_HOST_COMMON_H */
