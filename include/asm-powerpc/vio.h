/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 * Copyright (c) 2003 IBM Corp.
 *  Dave Engebretsen engebret@us.ibm.com
 *  Santiago Leon santil@us.ibm.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_VIO_H
#define _ASM_POWERPC_VIO_H
#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>

#include <asm/hvcall.h>
#include <asm/scatterlist.h>

/*
 * Architecture-specific constants for drivers to
 * extract attributes of the device using vio_get_attribute()
 */
#define VETH_MAC_ADDR "local-mac-address"
#define VETH_MCAST_FILTER_SIZE "ibm,mac-address-filters"

/* End architecture-specific constants */

#define h_vio_signal(ua, mode) \
  plpar_hcall_norets(H_VIO_SIGNAL, ua, mode)

#define VIO_IRQ_DISABLE		0UL
#define VIO_IRQ_ENABLE		1UL

struct iommu_table;

/*
 * The vio_dev structure is used to describe virtual I/O devices.
 */
struct vio_dev {
	const char *name;
	const char *type;
	uint32_t unit_address;
	unsigned int irq;
	struct device dev;
};

struct vio_driver {
	const struct vio_device_id *id_table;
	int (*probe)(struct vio_dev *dev, const struct vio_device_id *id);
	int (*remove)(struct vio_dev *dev);
	struct device_driver driver;
};

extern int vio_register_driver(struct vio_driver *drv);
extern void vio_unregister_driver(struct vio_driver *drv);

extern void __devinit vio_unregister_device(struct vio_dev *dev);

struct device_node;

extern struct vio_dev * __devinit vio_register_device_node(
		struct device_node *node_vdev);
extern const void *vio_get_attribute(struct vio_dev *vdev, char *which,
		int *length);
#ifdef CONFIG_PPC_PSERIES
extern struct vio_dev *vio_find_node(struct device_node *vnode);
extern int vio_enable_interrupts(struct vio_dev *dev);
extern int vio_disable_interrupts(struct vio_dev *dev);
#else
static inline int vio_enable_interrupts(struct vio_dev *dev)
{
	return 0;
}
#endif

static inline struct vio_driver *to_vio_driver(struct device_driver *drv)
{
	return container_of(drv, struct vio_driver, driver);
}

static inline struct vio_dev *to_vio_dev(struct device *dev)
{
	return container_of(dev, struct vio_dev, dev);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_VIO_H */
