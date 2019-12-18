/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2019 Intel Corporation */

#ifndef __LINUX_PECI_H
#define __LINUX_PECI_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/peci-ioctl.h>

#define PECI_NAME_SIZE   32

struct peci_board_info {
	char			type[PECI_NAME_SIZE];
	u8			addr;	/* CPU client address */
	struct device_node	*of_node;
};

/**
 * struct peci_adapter - represent a PECI adapter
 * @owner: owner module of the PECI adpater
 * @bus_lock: mutex for exclusion of multiple callers
 * @dev: device interface to this driver
 * @nr: the bus number to map
 * @name: name of the adapter
 * @userspace_clients_lock: mutex for exclusion of clients handling
 * @userspace_clients: list of registered clients
 * @xfer: low-level transfer function pointer of the adapter
 * @cmd_mask: mask for supportable PECI commands
 * @use_dma: flag for indicating that adapter uses DMA
 *
 * Each PECI adapter can communicate with one or more PECI client children.
 * These make a small bus, sharing a single wired PECI connection.
 */
struct peci_adapter {
	struct module		*owner;
	struct mutex		bus_lock; /* mutex for bus locking */
	struct device		dev;
	int			nr;
	char			name[PECI_NAME_SIZE];
	struct mutex		userspace_clients_lock; /* clients list mutex */
	struct list_head	userspace_clients;
	int			(*xfer)(struct peci_adapter *adapter,
					struct peci_xfer_msg *msg);
	u32			cmd_mask;
	bool			use_dma;
};

static inline struct peci_adapter *to_peci_adapter(void *d)
{
	return container_of(d, struct peci_adapter, dev);
}

static inline void *peci_get_adapdata(const struct peci_adapter *adapter)
{
	return dev_get_drvdata(&adapter->dev);
}

static inline void peci_set_adapdata(struct peci_adapter *adapter, void *data)
{
	dev_set_drvdata(&adapter->dev, data);
}

/**
 * struct peci_client - represent a PECI client device
 * @dev: driver model device node for the client
 * @adapter: manages the bus segment hosting this PECI device
 * @addr: address used on the PECI bus connected to the parent adapter
 * @name: indicates the type of the device
 * @detected: detected PECI clients list
 *
 * A peci_client identifies a single device (i.e. CPU) connected to a peci bus.
 * The behaviour exposed to Linux is defined by the driver managing the device.
 */
struct peci_client {
	struct device		dev;
	struct peci_adapter	*adapter;
	u8			addr;
	char			name[PECI_NAME_SIZE];
	struct list_head	detected;
};

static inline struct peci_client *to_peci_client(void *d)
{
	return container_of(d, struct peci_client, dev);
}

struct peci_device_id {
	char	name[PECI_NAME_SIZE];
	ulong	driver_data;	/* Data private to the driver */
};

/**
 * struct peci_driver - represent a PECI device driver
 * @probe: callback for device binding
 * @remove: callback for device unbinding
 * @shutdown: callback for device shutdown
 * @driver: device driver model driver
 * @id_table: list of PECI devices supported by this driver
 *
 * The driver.owner field should be set to the module owner of this driver.
 * The driver.name field should be set to the name of this driver.
 */
struct peci_driver {
	int				(*probe)(struct peci_client *client);
	int				(*remove)(struct peci_client *client);
	void				(*shutdown)(struct peci_client *client);
	struct device_driver		driver;
	const struct peci_device_id	*id_table;
};

static inline struct peci_driver *to_peci_driver(void *d)
{
	return container_of(d, struct peci_driver, driver);
}

/**
 * module_peci_driver - Helper macro for registering a modular PECI driver
 * @__peci_driver: peci_driver struct
 *
 * Helper macro for PECI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_peci_driver(__peci_driver) \
	module_driver(__peci_driver, peci_add_driver, peci_del_driver)

/* use a define to avoid include chaining to get THIS_MODULE */
#define peci_add_driver(driver) peci_register_driver(THIS_MODULE, driver)

extern struct bus_type peci_bus_type;
extern struct device_type peci_adapter_type;
extern struct device_type peci_client_type;

int  peci_register_driver(struct module *owner, struct peci_driver *drv);
void peci_del_driver(struct peci_driver *driver);
struct peci_client *peci_verify_client(struct device *dev);
struct peci_adapter *peci_alloc_adapter(struct device *dev, uint size);
struct peci_adapter *peci_get_adapter(int nr);
void peci_put_adapter(struct peci_adapter *adapter);
int  peci_add_adapter(struct peci_adapter *adapter);
void peci_del_adapter(struct peci_adapter *adapter);
struct peci_adapter *peci_verify_adapter(struct device *dev);
int  peci_for_each_dev(void *data, int (*fn)(struct device *, void *));
struct peci_xfer_msg *peci_get_xfer_msg(u8 tx_len, u8 rx_len);
void peci_put_xfer_msg(struct peci_xfer_msg *msg);
int  peci_command(struct peci_adapter *adpater, enum peci_cmd cmd, void *vmsg);
int  peci_get_cpu_id(struct peci_adapter *adapter, u8 addr, u32 *cpu_id);

#endif /* __LINUX_PECI_H */
