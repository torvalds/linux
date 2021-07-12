/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 */
#ifndef _PLATFORM_DATA_DMA_DW_H
#define _PLATFORM_DATA_DMA_DW_H

#include <linux/bits.h>
#include <linux/types.h>

#define DW_DMA_MAX_NR_MASTERS	4
#define DW_DMA_MAX_NR_CHANNELS	8
#define DW_DMA_MIN_BURST	1
#define DW_DMA_MAX_BURST	256

struct device;

/**
 * struct dw_dma_slave - Controller-specific information about a slave
 *
 * @dma_dev:	required DMA master device
 * @src_id:	src request line
 * @dst_id:	dst request line
 * @m_master:	memory master for transfers on allocated channel
 * @p_master:	peripheral master for transfers on allocated channel
 * @channels:	mask of the channels permitted for allocation (zero value means any)
 * @hs_polarity:set active low polarity of handshake interface
 */
struct dw_dma_slave {
	struct device		*dma_dev;
	u8			src_id;
	u8			dst_id;
	u8			m_master;
	u8			p_master;
	u8			channels;
	bool			hs_polarity;
};

/**
 * struct dw_dma_platform_data - Controller configuration parameters
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @chan_allocation_order: Allocate channels starting from 0 or 7
 * @chan_priority: Set channel priority increasing from 0 to 7 or 7 to 0.
 * @block_size: Maximum block size supported by the controller
 * @nr_masters: Number of AHB masters supported by the controller
 * @data_width: Maximum data width supported by hardware per AHB master
 *		(in bytes, power of 2)
 * @multi_block: Multi block transfers supported by hardware per channel.
 * @max_burst: Maximum value of burst transaction size supported by hardware
 *	       per channel (in units of CTL.SRC_TR_WIDTH/CTL.DST_TR_WIDTH).
 * @protctl: Protection control signals setting per channel.
 * @quirks: Optional platform quirks.
 */
struct dw_dma_platform_data {
	unsigned int	nr_channels;
#define CHAN_ALLOCATION_ASCENDING	0	/* zero to seven */
#define CHAN_ALLOCATION_DESCENDING	1	/* seven to zero */
	unsigned char	chan_allocation_order;
#define CHAN_PRIORITY_ASCENDING		0	/* chan0 highest */
#define CHAN_PRIORITY_DESCENDING	1	/* chan7 highest */
	unsigned char	chan_priority;
	unsigned int	block_size;
	unsigned char	nr_masters;
	unsigned char	data_width[DW_DMA_MAX_NR_MASTERS];
	unsigned char	multi_block[DW_DMA_MAX_NR_CHANNELS];
	u32		max_burst[DW_DMA_MAX_NR_CHANNELS];
#define CHAN_PROTCTL_PRIVILEGED		BIT(0)
#define CHAN_PROTCTL_BUFFERABLE		BIT(1)
#define CHAN_PROTCTL_CACHEABLE		BIT(2)
#define CHAN_PROTCTL_MASK		GENMASK(2, 0)
	unsigned char	protctl;
#define DW_DMA_QUIRK_XBAR_PRESENT	BIT(0)
	unsigned int	quirks;
};

#endif /* _PLATFORM_DATA_DMA_DW_H */
