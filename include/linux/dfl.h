/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for DFL driver and device API
 *
 * Copyright (C) 2020 Intel Corporation, Inc.
 */

#ifndef __LINUX_DFL_H
#define __LINUX_DFL_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>

/**
 * enum dfl_id_type - define the DFL FIU types
 */
enum dfl_id_type {
	FME_ID = 0,
	PORT_ID = 1,
	DFL_ID_MAX,
};

/**
 * struct dfl_device - represent an dfl device on dfl bus
 *
 * @dev: generic device interface.
 * @id: id of the dfl device.
 * @type: type of DFL FIU of the device. See enum dfl_id_type.
 * @feature_id: feature identifier local to its DFL FIU type.
 * @revision: revision of this dfl device feature.
 * @mmio_res: mmio resource of this dfl device.
 * @irqs: list of Linux IRQ numbers of this dfl device.
 * @num_irqs: number of IRQs supported by this dfl device.
 * @cdev: pointer to DFL FPGA container device this dfl device belongs to.
 * @id_entry: matched id entry in dfl driver's id table.
 * @dfh_version: version of DFH for the device
 * @param_size: size of the block parameters in bytes
 * @params: pointer to block of parameters copied memory
 */
struct dfl_device {
	struct device dev;
	int id;
	u16 type;
	u16 feature_id;
	u8 revision;
	struct resource mmio_res;
	int *irqs;
	unsigned int num_irqs;
	struct dfl_fpga_cdev *cdev;
	const struct dfl_device_id *id_entry;
	u8 dfh_version;
	unsigned int param_size;
	void *params;
};

/**
 * struct dfl_driver - represent an dfl device driver
 *
 * @drv: driver model structure.
 * @id_table: pointer to table of device IDs the driver is interested in.
 *	      { } member terminated.
 * @probe: mandatory callback for device binding.
 * @remove: callback for device unbinding.
 */
struct dfl_driver {
	struct device_driver drv;
	const struct dfl_device_id *id_table;

	int (*probe)(struct dfl_device *dfl_dev);
	void (*remove)(struct dfl_device *dfl_dev);
};

#define to_dfl_dev(d) container_of(d, struct dfl_device, dev)
#define to_dfl_drv(d) container_of_const(d, struct dfl_driver, drv)

/*
 * use a macro to avoid include chaining to get THIS_MODULE.
 */
#define dfl_driver_register(drv) \
	__dfl_driver_register(drv, THIS_MODULE)
int __dfl_driver_register(struct dfl_driver *dfl_drv, struct module *owner);
void dfl_driver_unregister(struct dfl_driver *dfl_drv);

/*
 * module_dfl_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dfl_driver(__dfl_driver) \
	module_driver(__dfl_driver, dfl_driver_register, \
		      dfl_driver_unregister)

void *dfh_find_param(struct dfl_device *dfl_dev, int param_id, size_t *pcount);
#endif /* __LINUX_DFL_H */
