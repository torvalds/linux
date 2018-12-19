/*
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>
#include <unistd.h>

#include <libudev.h>

#include "usbip_common.h"
#include "usbip_host_driver.h"
#include "list.h"
#include "sysfs_utils.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

struct usbip_host_driver *host_driver;
struct udev *udev_context;

static int32_t read_attr_usbip_status(struct usbip_usb_device *udev)
{
	char status_attr_path[SYSFS_PATH_MAX];
	int size;
	int fd;
	int length;
	char status;
	int value = 0;

	size = snprintf(status_attr_path, SYSFS_PATH_MAX, "%s/usbip_status",
			udev->path);
	if (size < 0 || (unsigned int)size >= sizeof(status_attr_path)) {
		err("usbip_status path length %i >= %lu or < 0", size,
		    (unsigned long)sizeof(status_attr_path));
		return -1;
	}

	fd = open(status_attr_path, O_RDONLY);
	if (fd < 0) {
		err("error opening attribute %s", status_attr_path);
		return -1;
	}

	length = read(fd, &status, 1);
	if (length < 0) {
		err("error reading attribute %s", status_attr_path);
		close(fd);
		return -1;
	}

	value = atoi(&status);

	return value;
}

static
struct usbip_exported_device *usbip_exported_device_new(const char *sdevpath)
{
	struct usbip_exported_device *edev = NULL;
	struct usbip_exported_device *edev_old;
	size_t size;
	int i;

	edev = calloc(1, sizeof(struct usbip_exported_device));

	edev->sudev = udev_device_new_from_syspath(udev_context, sdevpath);
	if (!edev->sudev) {
		err("udev_device_new_from_syspath: %s", sdevpath);
		goto err;
	}

	read_usb_device(edev->sudev, &edev->udev);

	edev->status = read_attr_usbip_status(&edev->udev);
	if (edev->status < 0)
		goto err;

	/* reallocate buffer to include usb interface data */
	size = sizeof(struct usbip_exported_device) +
		edev->udev.bNumInterfaces * sizeof(struct usbip_usb_interface);

	edev_old = edev;
	edev = realloc(edev, size);
	if (!edev) {
		edev = edev_old;
		dbg("realloc failed");
		goto err;
	}

	for (i = 0; i < edev->udev.bNumInterfaces; i++)
		read_usb_interface(&edev->udev, i, &edev->uinf[i]);

	return edev;
err:
	if (edev->sudev)
		udev_device_unref(edev->sudev);
	if (edev)
		free(edev);

	return NULL;
}

static int refresh_exported_devices(void)
{
	struct usbip_exported_device *edev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path;
	const char *driver;

	enumerate = udev_enumerate_new(udev_context);
	udev_enumerate_add_match_subsystem(enumerate, "usb");
	udev_enumerate_scan_devices(enumerate);

	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev_context, path);
		if (dev == NULL)
			continue;

		/* Check whether device uses usbip-host driver. */
		driver = udev_device_get_driver(dev);
		if (driver != NULL && !strcmp(driver, USBIP_HOST_DRV_NAME)) {
			edev = usbip_exported_device_new(path);
			if (!edev) {
				dbg("usbip_exported_device_new failed");
				continue;
			}

			list_add(&edev->node, &host_driver->edev_list);
			host_driver->ndevs++;
		}
	}

	return 0;
}

static void usbip_exported_device_destroy(void)
{
	struct list_head *i, *tmp;
	struct usbip_exported_device *edev;

	list_for_each_safe(i, tmp, &host_driver->edev_list) {
		edev = list_entry(i, struct usbip_exported_device, node);
		list_del(i);
		free(edev);
	}
}

int usbip_host_driver_open(void)
{
	int rc;

	udev_context = udev_new();
	if (!udev_context) {
		err("udev_new failed");
		return -1;
	}

	host_driver = calloc(1, sizeof(*host_driver));

	host_driver->ndevs = 0;
	INIT_LIST_HEAD(&host_driver->edev_list);

	rc = refresh_exported_devices();
	if (rc < 0)
		goto err_free_host_driver;

	return 0;

err_free_host_driver:
	free(host_driver);
	host_driver = NULL;

	udev_unref(udev_context);

	return -1;
}

void usbip_host_driver_close(void)
{
	if (!host_driver)
		return;

	usbip_exported_device_destroy();

	free(host_driver);
	host_driver = NULL;

	udev_unref(udev_context);
}

int usbip_host_refresh_device_list(void)
{
	int rc;

	usbip_exported_device_destroy();

	host_driver->ndevs = 0;
	INIT_LIST_HEAD(&host_driver->edev_list);

	rc = refresh_exported_devices();
	if (rc < 0)
		return -1;

	return 0;
}

int usbip_host_export_device(struct usbip_exported_device *edev, int sockfd)
{
	char attr_name[] = "usbip_sockfd";
	char sockfd_attr_path[SYSFS_PATH_MAX];
	int size;
	char sockfd_buff[30];
	int ret;

	if (edev->status != SDEV_ST_AVAILABLE) {
		dbg("device not available: %s", edev->udev.busid);
		switch (edev->status) {
		case SDEV_ST_ERROR:
			dbg("status SDEV_ST_ERROR");
			break;
		case SDEV_ST_USED:
			dbg("status SDEV_ST_USED");
			break;
		default:
			dbg("status unknown: 0x%x", edev->status);
		}
		return -1;
	}

	/* only the first interface is true */
	size = snprintf(sockfd_attr_path, sizeof(sockfd_attr_path), "%s/%s",
			edev->udev.path, attr_name);
	if (size < 0 || (unsigned int)size >= sizeof(sockfd_attr_path)) {
		err("exported device path length %i >= %lu or < 0", size,
		    (unsigned long)sizeof(sockfd_attr_path));
		return -1;
	}

	size = snprintf(sockfd_buff, sizeof(sockfd_buff), "%d\n", sockfd);
	if (size < 0 || (unsigned int)size >= sizeof(sockfd_buff)) {
		err("socket length %i >= %lu or < 0", size,
		    (unsigned long)sizeof(sockfd_buff));
		return -1;
	}

	ret = write_sysfs_attribute(sockfd_attr_path, sockfd_buff,
				    strlen(sockfd_buff));
	if (ret < 0) {
		err("write_sysfs_attribute failed: sockfd %s to %s",
		    sockfd_buff, sockfd_attr_path);
		return ret;
	}

	info("connect: %s", edev->udev.busid);

	return ret;
}

struct usbip_exported_device *usbip_host_get_device(int num)
{
	struct list_head *i;
	struct usbip_exported_device *edev;
	int cnt = 0;

	list_for_each(i, &host_driver->edev_list) {
		edev = list_entry(i, struct usbip_exported_device, node);
		if (num == cnt)
			return edev;
		else
			cnt++;
	}

	return NULL;
}
