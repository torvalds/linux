/*
 * Header for the new SH dmaengine driver
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef SH_DMA_H
#define SH_DMA_H

#include <linux/dmaengine.h>
#include <linux/list.h>
#include <linux/shdma-base.h>
#include <linux/types.h>

struct device;

/* Used by slave DMA clients to request DMA to/from a specific peripheral */
struct sh_dmae_slave {
	struct shdma_slave		shdma_slave;	/* Set by the platform */
};

/*
 * Supplied by platforms to specify, how a DMA channel has to be configured for
 * a certain peripheral
 */
struct sh_dmae_slave_config {
	int		slave_id;
	dma_addr_t	addr;
	u32		chcr;
	char		mid_rid;
};

/**
 * struct sh_dmae_channel - DMAC channel platform data
 * @offset:		register offset within the main IOMEM resource
 * @dmars:		channel DMARS register offset
 * @chclr_offset:	channel CHCLR register offset
 * @dmars_bit:		channel DMARS field offset within the register
 * @chclr_bit:		bit position, to be set to reset the channel
 */
struct sh_dmae_channel {
	unsigned int	offset;
	unsigned int	dmars;
	unsigned int	chclr_offset;
	unsigned char	dmars_bit;
	unsigned char	chclr_bit;
};

/**
 * struct sh_dmae_pdata - DMAC platform data
 * @slave:		array of slaves
 * @slave_num:		number of slaves in the above array
 * @channel:		array of DMA channels
 * @channel_num:	number of channels in the above array
 * @ts_low_shift:	shift of the low part of the TS field
 * @ts_low_mask:	low TS field mask
 * @ts_high_shift:	additional shift of the high part of the TS field
 * @ts_high_mask:	high TS field mask
 * @ts_shift:		array of Transfer Size shifts, indexed by TS value
 * @ts_shift_num:	number of shifts in the above array
 * @dmaor_init:		DMAOR initialisation value
 * @chcr_offset:	CHCR address offset
 * @chcr_ie_bit:	CHCR Interrupt Enable bit
 * @dmaor_is_32bit:	DMAOR is a 32-bit register
 * @needs_tend_set:	the TEND register has to be set
 * @no_dmars:		DMAC has no DMARS registers
 * @chclr_present:	DMAC has one or several CHCLR registers
 * @chclr_bitwise:	channel CHCLR registers are bitwise
 * @slave_only:		DMAC cannot be used for MEMCPY
 */
struct sh_dmae_pdata {
	const struct sh_dmae_slave_config *slave;
	int slave_num;
	const struct sh_dmae_channel *channel;
	int channel_num;
	unsigned int ts_low_shift;
	unsigned int ts_low_mask;
	unsigned int ts_high_shift;
	unsigned int ts_high_mask;
	const unsigned int *ts_shift;
	int ts_shift_num;
	u16 dmaor_init;
	unsigned int chcr_offset;
	u32 chcr_ie_bit;

	unsigned int dmaor_is_32bit:1;
	unsigned int needs_tend_set:1;
	unsigned int no_dmars:1;
	unsigned int chclr_present:1;
	unsigned int chclr_bitwise:1;
	unsigned int slave_only:1;
};

/* DMAOR definitions */
#define DMAOR_AE	0x00000004
#define DMAOR_NMIF	0x00000002
#define DMAOR_DME	0x00000001

/* Definitions for the SuperH DMAC */
#define DM_INC	0x00004000
#define DM_DEC	0x00008000
#define DM_FIX	0x0000c000
#define SM_INC	0x00001000
#define SM_DEC	0x00002000
#define SM_FIX	0x00003000
#define CHCR_DE	0x00000001
#define CHCR_TE	0x00000002
#define CHCR_IE	0x00000004

#endif
