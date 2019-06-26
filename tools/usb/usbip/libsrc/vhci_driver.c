// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_common.h"
#include "vhci_driver.h"
#include <limits.h>
#include <netdb.h>
#include <libudev.h>
#include <dirent.h>
#include "sysfs_utils.h"

#undef  PROGNAME
#define PROGNAME "libusbip"

struct usbip_vhci_driver *vhci_driver;
struct udev *udev_context;

static struct usbip_imported_device *
imported_device_init(struct usbip_imported_device *idev, char *busid)
{
	struct udev_device *sudev;

	sudev = udev_device_new_from_subsystem_sysname(udev_context,
						       "usb", busid);
	if (!sudev) {
		dbg("udev_device_new_from_subsystem_sysname failed: %s", busid);
		goto err;
	}
	read_usb_device(sudev, &idev->udev);
	udev_device_unref(sudev);

	return idev;

err:
	return NULL;
}

static int parse_status(const char *value)
{
	int ret = 0;
	char *c;

	/* skip a header line */
	c = strchr(value, '\n');
	if (!c)
		return -1;
	c++;

	while (*c != '\0') {
		int port, status, speed, devid;
		int sockfd;
		char lbusid[SYSFS_BUS_ID_SIZE];
		struct usbip_imported_device *idev;
		char hub[3];

		ret = sscanf(c, "%2s  %d %d %d %x %u %31s\n",
				hub, &port, &status, &speed,
				&devid, &sockfd, lbusid);

		if (ret < 5) {
			dbg("sscanf failed: %d", ret);
			BUG();
		}

		dbg("hub %s port %d status %d speed %d devid %x",
				hub, port, status, speed, devid);
		dbg("sockfd %u lbusid %s", sockfd, lbusid);

		/* if a device is connected, look at it */
		idev = &vhci_driver->idev[port];
		memset(idev, 0, sizeof(*idev));

		if (strncmp("hs", hub, 2) == 0)
			idev->hub = HUB_SPEED_HIGH;
		else /* strncmp("ss", hub, 2) == 0 */
			idev->hub = HUB_SPEED_SUPER;

		idev->port	= port;
		idev->status	= status;

		idev->devid	= devid;

		idev->busnum	= (devid >> 16);
		idev->devnum	= (devid & 0x0000ffff);

		if (idev->status != VDEV_ST_NULL
		    && idev->status != VDEV_ST_NOTASSIGNED) {
			idev = imported_device_init(idev, lbusid);
			if (!idev) {
				dbg("imported_device_init failed");
				return -1;
			}
		}

		/* go to the next line */
		c = strchr(c, '\n');
		if (!c)
			break;
		c++;
	}

	dbg("exit");

	return 0;
}

#define MAX_STATUS_NAME 18

static int refresh_imported_device_list(void)
{
	const char *attr_status;
	char status[MAX_STATUS_NAME+1] = "status";
	int i, ret;

	for (i = 0; i < vhci_driver->ncontrollers; i++) {
		if (i > 0)
			snprintf(status, sizeof(status), "status.%d", i);

		attr_status = udev_device_get_sysattr_value(vhci_driver->hc_device,
							    status);
		if (!attr_status) {
			err("udev_device_get_sysattr_value failed");
			return -1;
		}

		dbg("controller %d", i);

		ret = parse_status(attr_status);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static int get_nports(struct udev_device *hc_device)
{
	const char *attr_nports;

	attr_nports = udev_device_get_sysattr_value(hc_device, "nports");
	if (!attr_nports) {
		err("udev_device_get_sysattr_value nports failed");
		return -1;
	}

	return (int)strtoul(attr_nports, NULL, 10);
}

static int vhci_hcd_filter(const struct dirent *dirent)
{
	return !strncmp(dirent->d_name, "vhci_hcd.", 9);
}

static int get_ncontrollers(void)
{
	struct dirent **namelist;
	struct udev_device *platform;
	int n;

	platform = udev_device_get_parent(vhci_driver->hc_device);
	if (platform == NULL)
		return -1;

	n = scandir(udev_device_get_syspath(platform), &namelist, vhci_hcd_filter, NULL);
	if (n < 0)
		err("scandir failed");
	else {
		for (int i = 0; i < n; i++)
			free(namelist[i]);
		free(namelist);
	}

	return n;
}

/*
 * Read the given port's record.
 *
 * To avoid buffer overflow we will read the entire line and
 * validate each part's size. The initial buffer is padded by 4 to
 * accommodate the 2 spaces, 1 newline and an additional character
 * which is needed to properly validate the 3rd part without it being
 * truncated to an acceptable length.
 */
static int read_record(int rhport, char *host, unsigned long host_len,
		char *port, unsigned long port_len, char *busid)
{
	int part;
	FILE *file;
	char path[PATH_MAX+1];
	char *buffer, *start, *end;
	char delim[] = {' ', ' ', '\n'};
	int max_len[] = {(int)host_len, (int)port_len, SYSFS_BUS_ID_SIZE};
	size_t buffer_len = host_len + port_len + SYSFS_BUS_ID_SIZE + 4;

	buffer = malloc(buffer_len);
	if (!buffer)
		return -1;

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d", rhport);

	file = fopen(path, "r");
	if (!file) {
		err("fopen");
		free(buffer);
		return -1;
	}

	if (fgets(buffer, buffer_len, file) == NULL) {
		err("fgets");
		free(buffer);
		fclose(file);
		return -1;
	}
	fclose(file);

	/* validate the length of each of the 3 parts */
	start = buffer;
	for (part = 0; part < 3; part++) {
		end = strchr(start, delim[part]);
		if (end == NULL || (end - start) > max_len[part]) {
			free(buffer);
			return -1;
		}
		start = end + 1;
	}

	if (sscanf(buffer, "%s %s %s\n", host, port, busid) != 3) {
		err("sscanf");
		free(buffer);
		return -1;
	}

	free(buffer);

	return 0;
}

/* ---------------------------------------------------------------------- */

int usbip_vhci_driver_open(void)
{
	int nports;
	struct udev_device *hc_device;

	udev_context = udev_new();
	if (!udev_context) {
		err("udev_new failed");
		return -1;
	}

	/* will be freed in usbip_driver_close() */
	hc_device =
		udev_device_new_from_subsystem_sysname(udev_context,
						       USBIP_VHCI_BUS_TYPE,
						       USBIP_VHCI_DEVICE_NAME);
	if (!hc_device) {
		err("udev_device_new_from_subsystem_sysname failed");
		goto err;
	}

	nports = get_nports(hc_device);
	if (nports <= 0) {
		err("no available ports");
		goto err;
	}
	dbg("available ports: %d", nports);

	vhci_driver = calloc(1, sizeof(struct usbip_vhci_driver) +
			nports * sizeof(struct usbip_imported_device));
	if (!vhci_driver) {
		err("vhci_driver allocation failed");
		goto err;
	}

	vhci_driver->nports = nports;
	vhci_driver->hc_device = hc_device;
	vhci_driver->ncontrollers = get_ncontrollers();
	dbg("available controllers: %d", vhci_driver->ncontrollers);

	if (vhci_driver->ncontrollers <=0) {
		err("no available usb controllers");
		goto err;
	}

	if (refresh_imported_device_list())
		goto err;

	return 0;

err:
	udev_device_unref(hc_device);

	if (vhci_driver)
		free(vhci_driver);

	vhci_driver = NULL;

	udev_unref(udev_context);

	return -1;
}


void usbip_vhci_driver_close(void)
{
	if (!vhci_driver)
		return;

	udev_device_unref(vhci_driver->hc_device);

	free(vhci_driver);

	vhci_driver = NULL;

	udev_unref(udev_context);
}


int usbip_vhci_refresh_device_list(void)
{

	if (refresh_imported_device_list())
		goto err;

	return 0;
err:
	dbg("failed to refresh device list");
	return -1;
}


int usbip_vhci_get_free_port(uint32_t speed)
{
	for (int i = 0; i < vhci_driver->nports; i++) {

		switch (speed) {
		case	USB_SPEED_SUPER:
			if (vhci_driver->idev[i].hub != HUB_SPEED_SUPER)
				continue;
		break;
		default:
			if (vhci_driver->idev[i].hub != HUB_SPEED_HIGH)
				continue;
		break;
		}

		if (vhci_driver->idev[i].status == VDEV_ST_NULL)
			return vhci_driver->idev[i].port;
	}

	return -1;
}

int usbip_vhci_attach_device2(uint8_t port, int sockfd, uint32_t devid,
		uint32_t speed) {
	char buff[200]; /* what size should be ? */
	char attach_attr_path[SYSFS_PATH_MAX];
	char attr_attach[] = "attach";
	const char *path;
	int ret;

	snprintf(buff, sizeof(buff), "%u %d %u %u",
			port, sockfd, devid, speed);
	dbg("writing: %s", buff);

	path = udev_device_get_syspath(vhci_driver->hc_device);
	snprintf(attach_attr_path, sizeof(attach_attr_path), "%s/%s",
		 path, attr_attach);
	dbg("attach attribute path: %s", attach_attr_path);

	ret = write_sysfs_attribute(attach_attr_path, buff, strlen(buff));
	if (ret < 0) {
		dbg("write_sysfs_attribute failed");
		return -1;
	}

	dbg("attached port: %d", port);

	return 0;
}

static unsigned long get_devid(uint8_t busnum, uint8_t devnum)
{
	return (busnum << 16) | devnum;
}

/* will be removed */
int usbip_vhci_attach_device(uint8_t port, int sockfd, uint8_t busnum,
		uint8_t devnum, uint32_t speed)
{
	int devid = get_devid(busnum, devnum);

	return usbip_vhci_attach_device2(port, sockfd, devid, speed);
}

int usbip_vhci_detach_device(uint8_t port)
{
	char detach_attr_path[SYSFS_PATH_MAX];
	char attr_detach[] = "detach";
	char buff[200]; /* what size should be ? */
	const char *path;
	int ret;

	snprintf(buff, sizeof(buff), "%u", port);
	dbg("writing: %s", buff);

	path = udev_device_get_syspath(vhci_driver->hc_device);
	snprintf(detach_attr_path, sizeof(detach_attr_path), "%s/%s",
		 path, attr_detach);
	dbg("detach attribute path: %s", detach_attr_path);

	ret = write_sysfs_attribute(detach_attr_path, buff, strlen(buff));
	if (ret < 0) {
		dbg("write_sysfs_attribute failed");
		return -1;
	}

	dbg("detached port: %d", port);

	return 0;
}

int usbip_vhci_imported_device_dump(struct usbip_imported_device *idev)
{
	char product_name[100];
	char host[NI_MAXHOST] = "unknown host";
	char serv[NI_MAXSERV] = "unknown port";
	char remote_busid[SYSFS_BUS_ID_SIZE];
	int ret;
	int read_record_error = 0;

	if (idev->status == VDEV_ST_NULL || idev->status == VDEV_ST_NOTASSIGNED)
		return 0;

	ret = read_record(idev->port, host, sizeof(host), serv, sizeof(serv),
			  remote_busid);
	if (ret) {
		err("read_record");
		read_record_error = 1;
	}

	printf("Port %02d: <%s> at %s\n", idev->port,
	       usbip_status_string(idev->status),
	       usbip_speed_string(idev->udev.speed));

	usbip_names_get_product(product_name, sizeof(product_name),
				idev->udev.idVendor, idev->udev.idProduct);

	printf("       %s\n",  product_name);

	if (!read_record_error) {
		printf("%10s -> usbip://%s:%s/%s\n", idev->udev.busid,
		       host, serv, remote_busid);
		printf("%10s -> remote bus/dev %03d/%03d\n", " ",
		       idev->busnum, idev->devnum);
	} else {
		printf("%10s -> unknown host, remote port and remote busid\n",
		       idev->udev.busid);
		printf("%10s -> remote bus/dev %03d/%03d\n", " ",
		       idev->busnum, idev->devnum);
	}

	return 0;
}
