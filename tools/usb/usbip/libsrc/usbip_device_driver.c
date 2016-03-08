/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 *		 2015 Samsung Electronics
 * Author:	 Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *
 * Based on tools/usb/usbip/libsrc/usbip_host_driver.c, which is:
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <fcntl.h>
#include <string.h>
#include <linux/usb/ch9.h>

#include <unistd.h>

#include "usbip_host_common.h"
#include "usbip_device_driver.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

#define copy_descr_attr16(dev, descr, attr)			\
		((dev)->attr = le16toh((descr)->attr))		\

#define copy_descr_attr(dev, descr, attr)			\
		((dev)->attr = (descr)->attr)			\

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct {
	enum usb_device_speed speed;
	const char *name;
} speed_names[] = {
	{
		.speed = USB_SPEED_UNKNOWN,
		.name = "UNKNOWN",
	},
	{
		.speed = USB_SPEED_LOW,
		.name = "low-speed",
	},
	{
		.speed = USB_SPEED_FULL,
		.name = "full-speed",
	},
	{
		.speed = USB_SPEED_HIGH,
		.name = "high-speed",
	},
	{
		.speed = USB_SPEED_WIRELESS,
		.name = "wireless",
	},
	{
		.speed = USB_SPEED_SUPER,
		.name = "super-speed",
	},
};

static
int read_usb_vudc_device(struct udev_device *sdev, struct usbip_usb_device *dev)
{
	const char *path, *name;
	char filepath[SYSFS_PATH_MAX];
	struct usb_device_descriptor descr;
	unsigned i;
	FILE *fd = NULL;
	struct udev_device *plat;
	const char *speed;
	int ret = 0;

	plat = udev_device_get_parent(sdev);
	path = udev_device_get_syspath(plat);
	snprintf(filepath, SYSFS_PATH_MAX, "%s/%s",
		 path, VUDC_DEVICE_DESCR_FILE);
	fd = fopen(filepath, "r");
	if (!fd)
		return -1;
	ret = fread((char *) &descr, sizeof(descr), 1, fd);
	if (ret < 0)
		return -1;
	fclose(fd);

	copy_descr_attr(dev, &descr, bDeviceClass);
	copy_descr_attr(dev, &descr, bDeviceSubClass);
	copy_descr_attr(dev, &descr, bDeviceProtocol);
	copy_descr_attr(dev, &descr, bNumConfigurations);
	copy_descr_attr16(dev, &descr, idVendor);
	copy_descr_attr16(dev, &descr, idProduct);
	copy_descr_attr16(dev, &descr, bcdDevice);

	strncpy(dev->path, path, SYSFS_PATH_MAX);

	dev->speed = USB_SPEED_UNKNOWN;
	speed = udev_device_get_sysattr_value(sdev, "current_speed");
	if (speed) {
		for (i = 0; i < ARRAY_SIZE(speed_names); i++) {
			if (!strcmp(speed_names[i].name, speed)) {
				dev->speed = speed_names[i].speed;
				break;
			}
		}
	}

	/* Only used for user output, little sense to output them in general */
	dev->bNumInterfaces = 0;
	dev->bConfigurationValue = 0;
	dev->busnum = 0;

	name = udev_device_get_sysname(plat);
	strncpy(dev->busid, name, SYSFS_BUS_ID_SIZE);
	return 0;
}

static int is_my_device(struct udev_device *dev)
{
	const char *driver;

	driver = udev_device_get_property_value(dev, "USB_UDC_NAME");
	return driver != NULL && !strcmp(driver, USBIP_DEVICE_DRV_NAME);
}

static int usbip_device_driver_open(struct usbip_host_driver *hdriver)
{
	int ret;

	hdriver->ndevs = 0;
	INIT_LIST_HEAD(&hdriver->edev_list);

	ret = usbip_generic_driver_open(hdriver);
	if (ret)
		err("please load " USBIP_CORE_MOD_NAME ".ko and "
		    USBIP_DEVICE_DRV_NAME ".ko!");

	return ret;
}

struct usbip_host_driver device_driver = {
	.edev_list = LIST_HEAD_INIT(device_driver.edev_list),
	.udev_subsystem = "udc",
	.ops = {
		.open = usbip_device_driver_open,
		.close = usbip_generic_driver_close,
		.refresh_device_list = usbip_generic_refresh_device_list,
		.get_device = usbip_generic_get_device,
		.read_device = read_usb_vudc_device,
		.is_my_device = is_my_device,
	},
};
