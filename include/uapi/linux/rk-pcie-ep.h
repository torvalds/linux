/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI__RK_PCIE_EP_H__
#define _UAPI__RK_PCIE_EP_H__

#include <linux/types.h>

struct pcie_ep_user_data {
	__u32 elbi_app_user[11];
};

struct pcie_ep_dma_cache_cfg {
	__u64 addr;
	__u32 size;
};

#define PCIE_BASE	'P'
#define PCIE_DMA_GET_ELBI_DATA		_IOR(PCIE_BASE, 0, struct pcie_ep_user_data)
#define PCIE_DMA_CACHE_INVALIDE		_IOW(PCIE_BASE, 1, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_CACHE_FLUSH		_IOW(PCIE_BASE, 2, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_IRQ_MASK_ALL		_IOW(PCIE_BASE, 3, int)

#endif
