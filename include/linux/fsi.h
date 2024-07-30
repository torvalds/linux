/* SPDX-License-Identifier: GPL-2.0-only */
/* FSI device & driver interfaces
 *
 * Copyright (C) IBM Corporation 2016
 */

#ifndef LINUX_FSI_H
#define LINUX_FSI_H

#include <linux/device.h>

struct fsi_device {
	struct device		dev;
	u8			engine_type;
	u8			version;
	u8			unit;
	struct fsi_slave	*slave;
	uint32_t		addr;
	uint32_t		size;
};

extern int fsi_device_read(struct fsi_device *dev, uint32_t addr,
		void *val, size_t size);
extern int fsi_device_write(struct fsi_device *dev, uint32_t addr,
		const void *val, size_t size);
extern int fsi_device_peek(struct fsi_device *dev, void *val);

struct fsi_device_id {
	u8	engine_type;
	u8	version;
};

#define FSI_VERSION_ANY		0

#define FSI_DEVICE(t) \
	.engine_type = (t), .version = FSI_VERSION_ANY,

#define FSI_DEVICE_VERSIONED(t, v) \
	.engine_type = (t), .version = (v),

struct fsi_driver {
	struct device_driver		drv;
	const struct fsi_device_id	*id_table;
};

#define to_fsi_dev(devp) container_of(devp, struct fsi_device, dev)
#define to_fsi_drv(drvp) container_of_const(drvp, struct fsi_driver, drv)

extern int fsi_driver_register(struct fsi_driver *fsi_drv);
extern void fsi_driver_unregister(struct fsi_driver *fsi_drv);

/* module_fsi_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_fsi_driver(__fsi_driver) \
		module_driver(__fsi_driver, fsi_driver_register, \
				fsi_driver_unregister)

/* direct slave API */
extern int fsi_slave_claim_range(struct fsi_slave *slave,
		uint32_t addr, uint32_t size);
extern void fsi_slave_release_range(struct fsi_slave *slave,
		uint32_t addr, uint32_t size);
extern int fsi_slave_read(struct fsi_slave *slave, uint32_t addr,
		void *val, size_t size);
extern int fsi_slave_write(struct fsi_slave *slave, uint32_t addr,
		const void *val, size_t size);

extern struct bus_type fsi_bus_type;
extern const struct device_type fsi_cdev_type;

enum fsi_dev_type {
	fsi_dev_cfam,
	fsi_dev_sbefifo,
	fsi_dev_scom,
	fsi_dev_occ
};

extern int fsi_get_new_minor(struct fsi_device *fdev, enum fsi_dev_type type,
			     dev_t *out_dev, int *out_index);
extern void fsi_free_minor(dev_t dev);

#endif /* LINUX_FSI_H */
