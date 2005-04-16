/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_VIO_H
#define _ASM_VIO_H

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
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

struct vio_dev;
struct vio_driver;
struct vio_device_id;
struct iommu_table;

int vio_register_driver(struct vio_driver *drv);
void vio_unregister_driver(struct vio_driver *drv);

#ifdef CONFIG_PPC_PSERIES
struct vio_dev * __devinit vio_register_device_node(
		struct device_node *node_vdev);
#endif
void __devinit vio_unregister_device(struct vio_dev *dev);
struct vio_dev *vio_find_node(struct device_node *vnode);

const void * vio_get_attribute(struct vio_dev *vdev, void* which, int* length);
int vio_get_irq(struct vio_dev *dev);
int vio_enable_interrupts(struct vio_dev *dev);
int vio_disable_interrupts(struct vio_dev *dev);

extern struct dma_mapping_ops vio_dma_ops;

extern struct bus_type vio_bus_type;

struct vio_device_id {
	char *type;
	char *compat;
};

struct vio_driver {
	struct list_head node;
	char *name;
	const struct vio_device_id *id_table;	/* NULL if wants all devices */
	int  (*probe)  (struct vio_dev *dev, const struct vio_device_id *id);	/* New device inserted */
	int (*remove) (struct vio_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	unsigned long driver_data;

	struct device_driver driver;
};

static inline struct vio_driver *to_vio_driver(struct device_driver *drv)
{
	return container_of(drv, struct vio_driver, driver);
}

/*
 * The vio_dev structure is used to describe virtual I/O devices.
 */
struct vio_dev {
	struct iommu_table *iommu_table;     /* vio_map_* uses this */
	char *name;
	char *type;
	uint32_t unit_address;	
	unsigned int irq;

	struct device dev;
};

static inline struct vio_dev *to_vio_dev(struct device *dev)
{
	return container_of(dev, struct vio_dev, dev);
}

#endif /* _ASM_VIO_H */
