/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_H
#define _DW_EDMA_H

#include <linux/device.h>
#include <linux/dmaengine.h>

#define EDMA_MAX_WR_CH                                  8
#define EDMA_MAX_RD_CH                                  8

struct dw_edma;

struct dw_edma_region {
	u64		paddr;
	union {
		void		*mem;
		void __iomem	*io;
	} vaddr;
	size_t		sz;
};

/**
 * struct dw_edma_core_ops - platform-specific eDMA methods
 * @irq_vector:		Get IRQ number of the passed eDMA channel. Note the
 *			method accepts the channel id in the end-to-end
 *			numbering with the eDMA write channels being placed
 *			first in the row.
 * @pci_address:	Get PCIe bus address corresponding to the passed CPU
 *			address. Note there is no need in specifying this
 *			function if the address translation is performed by
 *			the DW PCIe RP/EP controller with the DW eDMA device in
 *			subject and DMA_BYPASS isn't set for all the outbound
 *			iATU windows. That will be done by the controller
 *			automatically.
 */
struct dw_edma_plat_ops {
	int (*irq_vector)(struct device *dev, unsigned int nr);
	u64 (*pci_address)(struct device *dev, phys_addr_t cpu_addr);
};

enum dw_edma_map_format {
	EDMA_MF_EDMA_LEGACY = 0x0,
	EDMA_MF_EDMA_UNROLL = 0x1,
	EDMA_MF_HDMA_COMPAT = 0x5,
	EDMA_MF_HDMA_NATIVE = 0x7,
};

/**
 * enum dw_edma_chip_flags - Flags specific to an eDMA chip
 * @DW_EDMA_CHIP_LOCAL:		eDMA is used locally by an endpoint
 */
enum dw_edma_chip_flags {
	DW_EDMA_CHIP_LOCAL	= BIT(0),
};

/**
 * struct dw_edma_chip - representation of DesignWare eDMA controller hardware
 * @dev:		 struct device of the eDMA controller
 * @id:			 instance ID
 * @nr_irqs:		 total number of DMA IRQs
 * @ops			 DMA channel to IRQ number mapping
 * @flags		 dw_edma_chip_flags
 * @reg_base		 DMA register base address
 * @ll_wr_cnt		 DMA write link list count
 * @ll_rd_cnt		 DMA read link list count
 * @rg_region		 DMA register region
 * @ll_region_wr	 DMA descriptor link list memory for write channel
 * @ll_region_rd	 DMA descriptor link list memory for read channel
 * @dt_region_wr	 DMA data memory for write channel
 * @dt_region_rd	 DMA data memory for read channel
 * @mf			 DMA register map format
 * @dw:			 struct dw_edma that is filled by dw_edma_probe()
 */
struct dw_edma_chip {
	struct device		*dev;
	int			nr_irqs;
	const struct dw_edma_plat_ops	*ops;
	u32			flags;

	void __iomem		*reg_base;

	u16			ll_wr_cnt;
	u16			ll_rd_cnt;
	/* link list address */
	struct dw_edma_region	ll_region_wr[EDMA_MAX_WR_CH];
	struct dw_edma_region	ll_region_rd[EDMA_MAX_RD_CH];

	/* data region */
	struct dw_edma_region	dt_region_wr[EDMA_MAX_WR_CH];
	struct dw_edma_region	dt_region_rd[EDMA_MAX_RD_CH];

	enum dw_edma_map_format	mf;

	struct dw_edma		*dw;
};

/* Export to the platform drivers */
#if IS_REACHABLE(CONFIG_DW_EDMA)
int dw_edma_probe(struct dw_edma_chip *chip);
int dw_edma_remove(struct dw_edma_chip *chip);
#else
static inline int dw_edma_probe(struct dw_edma_chip *chip)
{
	return -ENODEV;
}

static inline int dw_edma_remove(struct dw_edma_chip *chip)
{
	return 0;
}
#endif /* CONFIG_DW_EDMA */

#endif /* _DW_EDMA_H */
