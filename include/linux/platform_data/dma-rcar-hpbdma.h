/*
 * Copyright (C) 2011-2013 Renesas Electronics Corporation
 * Copyright (C) 2013 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#ifndef __DMA_RCAR_HPBDMA_H
#define __DMA_RCAR_HPBDMA_H

#include <linux/bitops.h>
#include <linux/types.h>

/* Transmit sizes and respective register values */
enum {
	XMIT_SZ_8BIT	= 0,
	XMIT_SZ_16BIT	= 1,
	XMIT_SZ_32BIT	= 2,
	XMIT_SZ_MAX
};

/* DMA control register (DCR) bits */
#define HPB_DMAE_DCR_DTAMD		(1u << 26)
#define HPB_DMAE_DCR_DTAC		(1u << 25)
#define HPB_DMAE_DCR_DTAU		(1u << 24)
#define HPB_DMAE_DCR_DTAU1		(1u << 23)
#define HPB_DMAE_DCR_SWMD		(1u << 22)
#define HPB_DMAE_DCR_BTMD		(1u << 21)
#define HPB_DMAE_DCR_PKMD		(1u << 20)
#define HPB_DMAE_DCR_CT			(1u << 18)
#define HPB_DMAE_DCR_ACMD		(1u << 17)
#define HPB_DMAE_DCR_DIP		(1u << 16)
#define HPB_DMAE_DCR_SMDL		(1u << 13)
#define HPB_DMAE_DCR_SPDAM		(1u << 12)
#define HPB_DMAE_DCR_SDRMD_MASK		(3u << 10)
#define HPB_DMAE_DCR_SDRMD_MOD		(0u << 10)
#define HPB_DMAE_DCR_SDRMD_AUTO		(1u << 10)
#define HPB_DMAE_DCR_SDRMD_TIMER	(2u << 10)
#define HPB_DMAE_DCR_SPDS_MASK		(3u << 8)
#define HPB_DMAE_DCR_SPDS_8BIT		(0u << 8)
#define HPB_DMAE_DCR_SPDS_16BIT		(1u << 8)
#define HPB_DMAE_DCR_SPDS_32BIT		(2u << 8)
#define HPB_DMAE_DCR_DMDL		(1u << 5)
#define HPB_DMAE_DCR_DPDAM		(1u << 4)
#define HPB_DMAE_DCR_DDRMD_MASK		(3u << 2)
#define HPB_DMAE_DCR_DDRMD_MOD		(0u << 2)
#define HPB_DMAE_DCR_DDRMD_AUTO		(1u << 2)
#define HPB_DMAE_DCR_DDRMD_TIMER	(2u << 2)
#define HPB_DMAE_DCR_DPDS_MASK		(3u << 0)
#define HPB_DMAE_DCR_DPDS_8BIT		(0u << 0)
#define HPB_DMAE_DCR_DPDS_16BIT		(1u << 0)
#define HPB_DMAE_DCR_DPDS_32BIT		(2u << 0)

/* Asynchronous reset register (ASYNCRSTR) bits */
#define HPB_DMAE_ASYNCRSTR_ASRST41	BIT(10)
#define HPB_DMAE_ASYNCRSTR_ASRST40	BIT(9)
#define HPB_DMAE_ASYNCRSTR_ASRST39	BIT(8)
#define HPB_DMAE_ASYNCRSTR_ASRST27	BIT(7)
#define HPB_DMAE_ASYNCRSTR_ASRST26	BIT(6)
#define HPB_DMAE_ASYNCRSTR_ASRST25	BIT(5)
#define HPB_DMAE_ASYNCRSTR_ASRST24	BIT(4)
#define HPB_DMAE_ASYNCRSTR_ASRST23	BIT(3)
#define HPB_DMAE_ASYNCRSTR_ASRST22	BIT(2)
#define HPB_DMAE_ASYNCRSTR_ASRST21	BIT(1)
#define HPB_DMAE_ASYNCRSTR_ASRST20	BIT(0)

struct hpb_dmae_slave_config {
	unsigned int	id;
	dma_addr_t	addr;
	u32		dcr;
	u32		port;
	u32		rstr;
	u32		mdr;
	u32		mdm;
	u32		flags;
#define	HPB_DMAE_SET_ASYNC_RESET	BIT(0)
#define	HPB_DMAE_SET_ASYNC_MODE		BIT(1)
	u32		dma_ch;
};

#define HPB_DMAE_CHANNEL(_irq, _s_id)	\
{					\
	.ch_irq		= _irq,		\
	.s_id		= _s_id,	\
}

struct hpb_dmae_channel {
	unsigned int	ch_irq;
	unsigned int	s_id;
};

struct hpb_dmae_pdata {
	const struct hpb_dmae_slave_config *slaves;
	int num_slaves;
	const struct hpb_dmae_channel *channels;
	int num_channels;
	const unsigned int ts_shift[XMIT_SZ_MAX];
	int num_hw_channels;
};

#endif
