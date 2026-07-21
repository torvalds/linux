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
#define HDMA_MAX_WR_CH                                  64
#define HDMA_MAX_RD_CH                                  64

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
 * struct dw_edma_plat_ops - platform-specific eDMA methods
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
 * @DW_EDMA_CHIP_PARTIAL:	Only channels described by this instance are
 *				owned by this driver. Controller-wide state
 *				must be preserved, and layouts with shared
 *				direction-wide registers must only be shared at
 *				direction granularity. Layouts with per-channel
 *				registers may be shared at channel granularity.
 */
enum dw_edma_chip_flags {
	DW_EDMA_CHIP_LOCAL	= BIT(0),
	DW_EDMA_CHIP_PARTIAL	= BIT(1),
};

/**
 * enum dw_edma_ch_irq_mode - per-channel interrupt routing control
 * @DW_EDMA_CH_IRQ_LOCAL:     local interrupt only (edma_int[])
 * @DW_EDMA_CH_IRQ_REMOTE:    remote interrupt only (IMWr/MSI), without
 *                            delivering local edma_int[].
 *
 * DesignWare EP eDMA can signal interrupts locally through the edma_int[]
 * bus, and remotely using posted memory writes (IMWr) that may be
 * interpreted as MSI/MSI-X by the RC.
 *
 * For the v0 eDMA linked-list programming path, DMA_*_INT_MASK gates the local
 * edma_int[] assertion, while there is no dedicated per-channel mask for IMWr
 * generation. To request a remote-only interrupt, Synopsys recommends setting
 * both LIE and RIE, and masking the local interrupt in DMA_*_INT_MASK. See the
 * DesignWare endpoint databook 6.30a, Linked List Mode interrupt handling
 * ("Software Programming of an Endpoint's LIE and RIE Bits for Linked List
 * Transfers", Attention).
 *
 * A local (DW_EDMA_CHIP_LOCAL) instance never issues transfers on a
 * remote-routed channel: REMOTE routing on such an instance denotes a channel
 * handed over to and driven by the remote side, and the recipe above is
 * applied by the driving instance.
 *
 * HDMA linked-list watermark interrupts have the same LWIE/RWIE guidance. HDMA
 * non-linked-list mode has dedicated local and remote stop/abort interrupt
 * enables.
 */
enum dw_edma_ch_irq_mode {
	DW_EDMA_CH_IRQ_LOCAL	= 0,
	DW_EDMA_CH_IRQ_REMOTE,
};

/**
 * struct dw_edma_chip - representation of DesignWare eDMA controller hardware
 * @dev:		 struct device of the eDMA controller
 * @nr_irqs:		 total number of DMA IRQs
 * @ops:		 DMA channel to IRQ number mapping
 * @flags:		 dw_edma_chip_flags
 * @reg_base:		 DMA register base address
 * @ll_wr_cnt:		 DMA write link list count
 * @ll_rd_cnt:		 DMA read link list count
 * @ll_region_wr:	 DMA descriptor link list memory for write channel
 * @ll_region_rd:	 DMA descriptor link list memory for read channel
 * @dt_region_wr:	 DMA data memory for write channel
 * @dt_region_rd:	 DMA data memory for read channel
 * @db_irq:		 Virtual IRQ dedicated to interrupt emulation
 * @db_offset:		 Offset from DMA register base
 * @mf:			 DMA register map format
 * @func_no:		 PCI endpoint function number used by DMA TLPs
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
	struct dw_edma_region	ll_region_wr[HDMA_MAX_WR_CH];
	struct dw_edma_region	ll_region_rd[HDMA_MAX_RD_CH];

	/* data region */
	struct dw_edma_region	dt_region_wr[HDMA_MAX_WR_CH];
	struct dw_edma_region	dt_region_rd[HDMA_MAX_RD_CH];

	/* interrupt emulation */
	int			db_irq;
	resource_size_t		db_offset;

	enum dw_edma_map_format	mf;
	u8			func_no;

	struct dw_edma		*dw;
	bool			cfg_non_ll;
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
