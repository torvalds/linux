/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _PLATFORM_DATA_DMA_DW_H
#define _PLATFORM_DATA_DMA_DW_H

#include <linux/device.h>

#define DW_DMA_MAX_NR_MASTERS	4
#define DW_DMA_MAX_NR_CHANNELS	8

/**
 * struct dw_dma_slave - Controller-specific information about a slave
 *
 * @dma_dev:	required DMA master device
 * @src_id:	src request line
 * @dst_id:	dst request line
 * @m_master:	memory master for transfers on allocated channel
 * @p_master:	peripheral master for transfers on allocated channel
 * @hs_polarity:set active low polarity of handshake interface
 */
struct dw_dma_slave {
	struct device		*dma_dev;
	u8			src_id;
	u8			dst_id;
	u8			m_master;
	u8			p_master;
	bool			hs_polarity;
};

/**
 * struct dw_dma_platform_data - Controller configuration parameters
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @is_private: The device channels should be marked as private and not for
 *	by the general purpose DMA channel allocator.
 * @is_memcpy: The device channels do support memory-to-memory transfers.
 * @is_idma32: The type of the DMA controller is iDMA32
 * @chan_allocation_order: Allocate channels starting from 0 or 7
 * @chan_priority: Set channel priority increasing from 0 to 7 or 7 to 0.
 * @block_size: Maximum block size supported by the controller
 * @nr_masters: Number of AHB masters supported by the controller
 * @data_width: Maximum data width supported by hardware per AHB master
 *		(in bytes, power of 2)
 * @multi_block: Multi block transfers supported by hardware per channel.
 */
struct dw_dma_platform_data {
	unsigned int	nr_channels;
	bool		is_private;
	bool		is_memcpy;
	bool		is_idma32;
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
};

#endif /* _PLATFORM_DATA_DMA_DW_H */
