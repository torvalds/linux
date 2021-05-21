/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 ARM Ltd.
 */

#ifndef _LINUX_ARM_FFA_H
#define _LINUX_ARM_FFA_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uuid.h>

/* FFA Bus/Device/Driver related */
struct ffa_device {
	int vm_id;
	bool mode_32bit;
	uuid_t uuid;
	struct device dev;
};

#define to_ffa_dev(d) container_of(d, struct ffa_device, dev)

struct ffa_device_id {
	uuid_t uuid;
};

struct ffa_driver {
	const char *name;
	int (*probe)(struct ffa_device *sdev);
	void (*remove)(struct ffa_device *sdev);
	const struct ffa_device_id *id_table;

	struct device_driver driver;
};

#define to_ffa_driver(d) container_of(d, struct ffa_driver, driver)

static inline void ffa_dev_set_drvdata(struct ffa_device *fdev, void *data)
{
	fdev->dev.driver_data = data;
}

#if IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT)
struct ffa_device *ffa_device_register(const uuid_t *uuid, int vm_id);
void ffa_device_unregister(struct ffa_device *ffa_dev);
int ffa_driver_register(struct ffa_driver *driver, struct module *owner,
			const char *mod_name);
void ffa_driver_unregister(struct ffa_driver *driver);
bool ffa_device_is_valid(struct ffa_device *ffa_dev);
const struct ffa_dev_ops *ffa_dev_ops_get(struct ffa_device *dev);

#else
static inline
struct ffa_device *ffa_device_register(const uuid_t *uuid, int vm_id)
{
	return NULL;
}

static inline void ffa_device_unregister(struct ffa_device *dev) {}

static inline int
ffa_driver_register(struct ffa_driver *driver, struct module *owner,
		    const char *mod_name)
{
	return -EINVAL;
}

static inline void ffa_driver_unregister(struct ffa_driver *driver) {}

static inline
bool ffa_device_is_valid(struct ffa_device *ffa_dev) { return false; }

static inline
const struct ffa_dev_ops *ffa_dev_ops_get(struct ffa_device *dev)
{
	return NULL;
}
#endif /* CONFIG_ARM_FFA_TRANSPORT */

#define ffa_register(driver) \
	ffa_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#define ffa_unregister(driver) \
	ffa_driver_unregister(driver)

/**
 * module_ffa_driver() - Helper macro for registering a psa_ffa driver
 * @__ffa_driver: ffa_driver structure
 *
 * Helper macro for psa_ffa drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_ffa_driver(__ffa_driver)	\
	module_driver(__ffa_driver, ffa_register, ffa_unregister)

/* FFA transport related */
struct ffa_partition_info {
	u16 id;
	u16 exec_ctxt;
/* partition supports receipt of direct requests */
#define FFA_PARTITION_DIRECT_RECV	BIT(0)
/* partition can send direct requests. */
#define FFA_PARTITION_DIRECT_SEND	BIT(1)
/* partition can send and receive indirect messages. */
#define FFA_PARTITION_INDIRECT_MSG	BIT(2)
	u32 properties;
};

/* For use with FFA_MSG_SEND_DIRECT_{REQ,RESP} which pass data via registers */
struct ffa_send_direct_data {
	unsigned long data0; /* w3/x3 */
	unsigned long data1; /* w4/x4 */
	unsigned long data2; /* w5/x5 */
	unsigned long data3; /* w6/x6 */
	unsigned long data4; /* w7/x7 */
};

struct ffa_dev_ops {
	u32 (*api_version_get)(void);
	int (*partition_info_get)(const char *uuid_str,
				  struct ffa_partition_info *buffer);
	void (*mode_32bit_set)(struct ffa_device *dev);
	int (*sync_send_receive)(struct ffa_device *dev,
				 struct ffa_send_direct_data *data);
};

#endif /* _LINUX_ARM_FFA_H */
