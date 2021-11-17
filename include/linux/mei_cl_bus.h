/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013-2016, Intel Corporation. All rights reserved.
 */
#ifndef _LINUX_MEI_CL_BUS_H
#define _LINUX_MEI_CL_BUS_H

#include <linux/device.h>
#include <linux/uuid.h>
#include <linux/mod_devicetable.h>

struct mei_cl_device;
struct mei_device;

typedef void (*mei_cldev_cb_t)(struct mei_cl_device *cldev);

/**
 * struct mei_cl_device - MEI device handle
 * An mei_cl_device pointer is returned from mei_add_device()
 * and links MEI bus clients to their actual ME host client pointer.
 * Drivers for MEI devices will get an mei_cl_device pointer
 * when being probed and shall use it for doing ME bus I/O.
 *
 * @bus_list: device on the bus list
 * @bus: parent mei device
 * @dev: linux driver model device pointer
 * @me_cl: me client
 * @cl: mei client
 * @name: device name
 * @rx_work: async work to execute Rx event callback
 * @rx_cb: Drivers register this callback to get asynchronous ME
 *	Rx buffer pending notifications.
 * @notif_work: async work to execute FW notif event callback
 * @notif_cb: Drivers register this callback to get asynchronous ME
 *	FW notification pending notifications.
 *
 * @do_match: wheather device can be matched with a driver
 * @is_added: device is already scanned
 * @priv_data: client private data
 */
struct mei_cl_device {
	struct list_head bus_list;
	struct mei_device *bus;
	struct device dev;

	struct mei_me_client *me_cl;
	struct mei_cl *cl;
	char name[MEI_CL_NAME_SIZE];

	struct work_struct rx_work;
	mei_cldev_cb_t rx_cb;
	struct work_struct notif_work;
	mei_cldev_cb_t notif_cb;

	unsigned int do_match:1;
	unsigned int is_added:1;

	void *priv_data;
};

#define to_mei_cl_device(d) container_of(d, struct mei_cl_device, dev)

struct mei_cl_driver {
	struct device_driver driver;
	const char *name;

	const struct mei_cl_device_id *id_table;

	int (*probe)(struct mei_cl_device *cldev,
		     const struct mei_cl_device_id *id);
	void (*remove)(struct mei_cl_device *cldev);
};

int __mei_cldev_driver_register(struct mei_cl_driver *cldrv,
				struct module *owner);
#define mei_cldev_driver_register(cldrv)             \
	__mei_cldev_driver_register(cldrv, THIS_MODULE)

void mei_cldev_driver_unregister(struct mei_cl_driver *cldrv);

/**
 * module_mei_cl_driver - Helper macro for registering mei cl driver
 *
 * @__mei_cldrv: mei_cl_driver structure
 *
 *  Helper macro for mei cl drivers which do not do anything special in module
 *  init/exit, for eliminating a boilerplate code.
 */
#define module_mei_cl_driver(__mei_cldrv) \
	module_driver(__mei_cldrv, \
		      mei_cldev_driver_register,\
		      mei_cldev_driver_unregister)

ssize_t mei_cldev_send(struct mei_cl_device *cldev, const u8 *buf,
		       size_t length);
ssize_t mei_cldev_recv(struct mei_cl_device *cldev, u8 *buf, size_t length);
ssize_t mei_cldev_recv_nonblock(struct mei_cl_device *cldev, u8 *buf,
				size_t length);
ssize_t mei_cldev_send_vtag(struct mei_cl_device *cldev, const u8 *buf,
			    size_t length, u8 vtag);
ssize_t mei_cldev_recv_vtag(struct mei_cl_device *cldev, u8 *buf, size_t length,
			    u8 *vtag);
ssize_t mei_cldev_recv_nonblock_vtag(struct mei_cl_device *cldev, u8 *buf,
				     size_t length, u8 *vtag);

int mei_cldev_register_rx_cb(struct mei_cl_device *cldev, mei_cldev_cb_t rx_cb);
int mei_cldev_register_notif_cb(struct mei_cl_device *cldev,
				mei_cldev_cb_t notif_cb);

const uuid_le *mei_cldev_uuid(const struct mei_cl_device *cldev);
u8 mei_cldev_ver(const struct mei_cl_device *cldev);

void *mei_cldev_get_drvdata(const struct mei_cl_device *cldev);
void mei_cldev_set_drvdata(struct mei_cl_device *cldev, void *data);

int mei_cldev_enable(struct mei_cl_device *cldev);
int mei_cldev_disable(struct mei_cl_device *cldev);
bool mei_cldev_enabled(const struct mei_cl_device *cldev);

#endif /* _LINUX_MEI_CL_BUS_H */
