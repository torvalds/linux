/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI__RK_PCIE_EP_H__
#define _UAPI__RK_PCIE_EP_H__

#include <linux/types.h>

/* rkep device mode status definition */
#define RKEP_MODE_BOOTROM       1
#define RKEP_MODE_LOADER        2
#define RKEP_MODE_KERNEL        3
#define RKEP_MODE_FUN0          4
/* Common status */
#define RKEP_SMODE_INIT         0
#define RKEP_SMODE_LNKRDY       1
#define RKEP_SMODE_LNKUP        2
#define RKEP_SMODE_ERR          0xff
/* Firmware download status */
#define RKEP_SMODE_FWDLRDY      0x10
#define RKEP_SMODE_FWDLDONE     0x11
/* Application status*/
#define RKEP_SMODE_APPRDY       0x20

/*
 * rockchip driver cache ioctrl input param
 */
struct pcie_ep_dma_cache_cfg {
	__u64 addr;
	__u32 size;
};

struct pcie_ep_dma_block {
	__u64 bus_paddr;
	__u64 local_paddr;
	__u32 size;
};

struct pcie_ep_dma_block_req {
	__u16 vir_id;	/* Default 0 */
	__u8 chn;
	__u8 wr;
	__u32 flag;
#define PCIE_EP_DMA_BLOCK_FLAG_COHERENT BIT(0)		/* Cache coherent, 1-need, 0-None */
	struct pcie_ep_dma_block block;
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
	PCIE_EP_MMAP_RESOURCE_USER_MEM,
	PCIE_EP_MMAP_RESOURCE_RK3568_RC_DBI,
	PCIE_EP_MMAP_RESOURCE_RK3588_RC_DBI,
	PCIE_EP_MMAP_RESOURCE_MAX,
};

#define PCIE_EP_OBJ_INFO_MSI_DATA_NUM	0x8
#define RKEP_EP_VIRTUAL_ID_MAX		(PCIE_EP_OBJ_INFO_MSI_DATA_NUM * 32) /* 256 virtual_id */

/*
 * rockchip ep device information which is store in BAR0
 */
struct pcie_ep_obj_info {
	__u32 magic;
	__u32 version;
	struct {
		__u16 mode;
		__u16 submode;
	} devmode;
	__u32 msi_data[PCIE_EP_OBJ_INFO_MSI_DATA_NUM];
	__u8 reserved[0x1D0];

	__u32 irq_type_rc;					/* Generate in ep isr, valid only for rc, clear in rc */
	struct pcie_ep_obj_irq_dma_status dma_status_rc;	/* Generate in ep isr, valid only for rc, clear in rc */
	__u32 irq_type_ep;					/* Generate in ep isr, valid only for ep, clear in ep */
	struct pcie_ep_obj_irq_dma_status dma_status_ep;	/* Generate in ep isr, valid only for ep, clear in ep */
	__u32 irq_user_data_rc;					/* Generate in ep, valid only for rc, No need to clear */
	__u32 irq_user_data_ep;					/* Generate in rc, valid only for ep, No need to clear */
};

/*
 * rockchip driver ep_obj poll ioctrl input param
 */
struct pcie_ep_obj_poll_virtual_id_cfg {
	__u32 timeout_ms;
	__u32 sync;
	__u32 virtual_id;
	__u32 poll_status;
};

#define PCIE_BASE	'P'
#define PCIE_DMA_CACHE_INVALIDE		_IOW(PCIE_BASE, 1, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_CACHE_FLUSH		_IOW(PCIE_BASE, 2, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_IRQ_MASK_ALL		_IOW(PCIE_BASE, 3, int)
#define PCIE_EP_RAISE_MSI		_IOW(PCIE_BASE, 4, int)
#define PCIE_EP_SET_MMAP_RESOURCE	_IOW(PCIE_BASE, 6, int)
#define PCIE_EP_RAISE_ELBI		_IOW(PCIE_BASE, 7, int)
#define PCIE_EP_REQUEST_VIRTUAL_ID	_IOR(PCIE_BASE, 16, int)
#define PCIE_EP_RELEASE_VIRTUAL_ID	_IOW(PCIE_BASE, 17, int)
#define PCIE_EP_RAISE_IRQ_USER		_IOW(PCIE_BASE, 18, int)
#define PCIE_EP_POLL_IRQ_USER		_IOW(PCIE_BASE, 19, struct pcie_ep_obj_poll_virtual_id_cfg)
#define PCIE_EP_DMA_XFER_BLOCK		_IOW(PCIE_BASE, 32, struct pcie_ep_dma_block_req)

#endif
