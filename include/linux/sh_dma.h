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

#include <linux/list.h>
#include <linux/dmaengine.h>

/* Used by slave DMA clients to request DMA to/from a specific peripheral */
struct sh_dmae_slave {
	unsigned int			slave_id; /* Set by the platform */
	struct device			*dma_dev; /* Set by the platform */
	const struct sh_dmae_slave_config	*config;  /* Set by the driver */
};

struct sh_dmae_regs {
	u32 sar; /* SAR / source address */
	u32 dar; /* DAR / destination address */
	u32 tcr; /* TCR / transfer count */
};

struct sh_desc {
	struct sh_dmae_regs hw;
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	enum dma_data_direction direction;
	dma_cookie_t cookie;
	size_t partial;
	int chunks;
	int mark;
};

struct sh_dmae_slave_config {
	unsigned int			slave_id;
	dma_addr_t			addr;
	u32				chcr;
	char				mid_rid;
};

struct sh_dmae_channel {
	unsigned int	offset;
	unsigned int	dmars;
	unsigned int	dmars_bit;
};

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
};

/* DMA register */
#define SAR	0x00
#define DAR	0x04
#define TCR	0x08
#define CHCR	0x0C
#define DMAOR	0x40

#define TEND	0x18 /* USB-DMAC */

/* DMAOR definitions */
#define DMAOR_AE	0x00000004
#define DMAOR_NMIF	0x00000002
#define DMAOR_DME	0x00000001

/* Definitions for the SuperH DMAC */
#define REQ_L	0x00000000
#define REQ_E	0x00080000
#define RACK_H	0x00000000
#define RACK_L	0x00040000
#define ACK_R	0x00000000
#define ACK_W	0x00020000
#define ACK_H	0x00000000
#define ACK_L	0x00010000
#define DM_INC	0x00004000
#define DM_DEC	0x00008000
#define DM_FIX	0x0000c000
#define SM_INC	0x00001000
#define SM_DEC	0x00002000
#define SM_FIX	0x00003000
#define RS_IN	0x00000200
#define RS_OUT	0x00000300
#define TS_BLK	0x00000040
#define TM_BUR	0x00000020
#define CHCR_DE	0x00000001
#define CHCR_TE	0x00000002
#define CHCR_IE	0x00000004

#endif
