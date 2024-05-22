/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) OR BSD-2-Clause */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _PDS_COMMON_H_
#define _PDS_COMMON_H_

#include <linux/notifier.h>

#define PDS_CORE_DRV_NAME			"pds_core"

/* the device's internal addressing uses up to 52 bits */
#define PDS_CORE_ADDR_LEN	52
#define PDS_CORE_ADDR_MASK	(BIT_ULL(PDS_ADDR_LEN) - 1)
#define PDS_PAGE_SIZE		4096

enum pds_core_driver_type {
	PDS_DRIVER_LINUX   = 1,
	PDS_DRIVER_WIN     = 2,
	PDS_DRIVER_DPDK    = 3,
	PDS_DRIVER_FREEBSD = 4,
	PDS_DRIVER_IPXE    = 5,
	PDS_DRIVER_ESXI    = 6,
};

enum pds_core_vif_types {
	PDS_DEV_TYPE_CORE	= 0,
	PDS_DEV_TYPE_VDPA	= 1,
	PDS_DEV_TYPE_VFIO	= 2,
	PDS_DEV_TYPE_ETH	= 3,
	PDS_DEV_TYPE_RDMA	= 4,
	PDS_DEV_TYPE_LM		= 5,

	/* new ones added before this line */
	PDS_DEV_TYPE_MAX	= 16   /* don't change - used in struct size */
};

#define PDS_DEV_TYPE_CORE_STR	"Core"
#define PDS_DEV_TYPE_VDPA_STR	"vDPA"
#define PDS_DEV_TYPE_VFIO_STR	"vfio"
#define PDS_DEV_TYPE_ETH_STR	"Eth"
#define PDS_DEV_TYPE_RDMA_STR	"RDMA"
#define PDS_DEV_TYPE_LM_STR	"LM"

#define PDS_VDPA_DEV_NAME	PDS_CORE_DRV_NAME "." PDS_DEV_TYPE_VDPA_STR
#define PDS_VFIO_LM_DEV_NAME	PDS_CORE_DRV_NAME "." PDS_DEV_TYPE_LM_STR "." PDS_DEV_TYPE_VFIO_STR

struct pdsc;

int pdsc_register_notify(struct notifier_block *nb);
void pdsc_unregister_notify(struct notifier_block *nb);
void *pdsc_get_pf_struct(struct pci_dev *vf_pdev);
int pds_client_register(struct pdsc *pf, char *devname);
int pds_client_unregister(struct pdsc *pf, u16 client_id);
#endif /* _PDS_COMMON_H_ */
