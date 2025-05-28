/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021-2025, Intel Corporation. */

#ifndef _IIDC_RDMA_H_
#define _IIDC_RDMA_H_

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/dscp.h>

enum iidc_rdma_event_type {
	IIDC_RDMA_EVENT_BEFORE_MTU_CHANGE,
	IIDC_RDMA_EVENT_AFTER_MTU_CHANGE,
	IIDC_RDMA_EVENT_BEFORE_TC_CHANGE,
	IIDC_RDMA_EVENT_AFTER_TC_CHANGE,
	IIDC_RDMA_EVENT_WARN_RESET,
	IIDC_RDMA_EVENT_CRIT_ERR,
	IIDC_RDMA_EVENT_NBITS		/* must be last */
};

struct iidc_rdma_event {
	DECLARE_BITMAP(type, IIDC_RDMA_EVENT_NBITS);
	u32 reg;
};

enum iidc_rdma_reset_type {
	IIDC_FUNC_RESET,
	IIDC_DEV_RESET,
};

enum iidc_rdma_protocol {
	IIDC_RDMA_PROTOCOL_IWARP = BIT(0),
	IIDC_RDMA_PROTOCOL_ROCEV2 = BIT(1),
};

/* Structure to be populated by core LAN PCI driver */
struct iidc_rdma_core_dev_info {
	struct pci_dev *pdev; /* PCI device of corresponding to main function */
	struct auxiliary_device *adev;
	/* Current active RDMA protocol */
	enum iidc_rdma_protocol rdma_protocol;
	void *iidc_priv; /* elements unique to each driver */
};

/* Structure representing auxiliary driver tailored information about the core
 * PCI dev, each auxiliary driver using the IIDC interface will have an
 * instance of this struct dedicated to it.
 */
struct iidc_rdma_core_auxiliary_dev {
	struct auxiliary_device adev;
	struct iidc_rdma_core_dev_info *cdev_info;
};

/* structure representing the auxiliary driver. This struct is to be
 * allocated and populated by the auxiliary driver's owner. The core PCI
 * driver will access these ops by performing a container_of on the
 * auxiliary_device->dev.driver.
 */
struct iidc_rdma_core_auxiliary_drv {
	struct auxiliary_driver adrv;
	void (*event_handler)(struct iidc_rdma_core_dev_info *cdev,
			      struct iidc_rdma_event *event);
};

#endif /* _IIDC_RDMA_H_*/
