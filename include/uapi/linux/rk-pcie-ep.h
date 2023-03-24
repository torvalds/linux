/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI__RK_PCIE_EP_H__
#define _UAPI__RK_PCIE_EP_H__

#include <linux/types.h>

/*
 * rockchip pcie driver elbi ioctrl output data
 */
struct pcie_ep_user_data {
	__u64 bar0_phys_addr;
	__u32 elbi_app_user[11];
};

/*
 * rockchip driver cache ioctrl input param
 */
struct pcie_ep_dma_cache_cfg {
	__u64 addr;
	__u32 size;
};

#define	PCIE_EP_OBJ_INFO_MAGIC 0x524B4550

enum pcie_ep_obj_irq_type {
	OBJ_IRQ_UNKNOWN,
	OBJ_IRQ_DMA,
	OBJ_IRQ_USER,
	OBJ_IRQ_ELBI,
};

struct pcie_ep_obj_irq_dma_status {
	__u32 wr;
	__u32 rd;
};

enum pcie_ep_mmap_resource {
	PCIE_EP_MMAP_RESOURCE_DBI,
	PCIE_EP_MMAP_RESOURCE_BAR0,
	PCIE_EP_MMAP_RESOURCE_BAR2,
	PCIE_EP_MMAP_RESOURCE_BAR4,
	PCIE_EP_MMAP_RESOURCE_MAX,
};

/*
 * rockchip ep device information which is store in BAR0
 */
struct pcie_ep_obj_info {
	__u32 magic;
	__u32 version;
	__u8 reserved[0x1F8];

	__u32 irq_type_rc;					/* Generate in ep isr, valid only for rc, clear in rc */
	struct pcie_ep_obj_irq_dma_status dma_status_rc;	/* Generate in ep isr, valid only for rc, clear in rc */
	__u32 irq_type_ep;					/* Generate in ep isr, valid only for ep, clear in ep */
	struct pcie_ep_obj_irq_dma_status dma_status_ep;	/* Generate in ep isr, valid only for ep, clear in ep */
	__u32 obj_irq_user_data;				/* OBJ_IRQ_USER userspace data */
};

#define PCIE_BASE	'P'
#define PCIE_DMA_GET_ELBI_DATA		_IOR(PCIE_BASE, 0, struct pcie_ep_user_data)
#define PCIE_DMA_CACHE_INVALIDE		_IOW(PCIE_BASE, 1, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_CACHE_FLUSH		_IOW(PCIE_BASE, 2, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_IRQ_MASK_ALL		_IOW(PCIE_BASE, 3, int)
#define PCIE_DMA_RAISE_MSI_OBJ_IRQ_USER	_IOW(PCIE_BASE, 4, int)
#define PCIE_EP_GET_USER_INFO		_IOR(PCIE_BASE, 5, struct pcie_ep_user_data)
#define PCIE_EP_SET_MMAP_RESOURCE	_IOW(PCIE_BASE, 6, enum pcie_ep_mmap_resource)

#endif
