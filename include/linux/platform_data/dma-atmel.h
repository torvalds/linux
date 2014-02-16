/*
 * Header file for the Atmel AHB DMA Controller driver
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef AT_HDMAC_H
#define AT_HDMAC_H

#include <linux/dmaengine.h>

/**
 * struct at_dma_platform_data - Controller configuration parameters
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @cap_mask: dma_capability flags supported by the platform
 */
struct at_dma_platform_data {
	unsigned int	nr_channels;
	dma_cap_mask_t  cap_mask;
};

/**
 * struct at_dma_slave - Controller-specific information about a slave
 * @dma_dev: required DMA master device
 * @cfg: Platform-specific initializer for the CFG register
 */
struct at_dma_slave {
	struct device		*dma_dev;
	u32			cfg;
};


/* Platform-configurable bits in CFG */
#define ATC_PER_MSB(h)	((0x30U & (h)) >> 4)	/* Extract most significant bits of a handshaking identifier */

#define	ATC_SRC_PER(h)		(0xFU & (h))	/* Channel src rq associated with periph handshaking ifc h */
#define	ATC_DST_PER(h)		((0xFU & (h)) <<  4)	/* Channel dst rq associated with periph handshaking ifc h */
#define	ATC_SRC_REP		(0x1 <<  8)	/* Source Replay Mod */
#define	ATC_SRC_H2SEL		(0x1 <<  9)	/* Source Handshaking Mod */
#define		ATC_SRC_H2SEL_SW	(0x0 <<  9)
#define		ATC_SRC_H2SEL_HW	(0x1 <<  9)
#define	ATC_SRC_PER_MSB(h)	(ATC_PER_MSB(h) << 10)	/* Channel src rq (most significant bits) */
#define	ATC_DST_REP		(0x1 << 12)	/* Destination Replay Mod */
#define	ATC_DST_H2SEL		(0x1 << 13)	/* Destination Handshaking Mod */
#define		ATC_DST_H2SEL_SW	(0x0 << 13)
#define		ATC_DST_H2SEL_HW	(0x1 << 13)
#define	ATC_DST_PER_MSB(h)	(ATC_PER_MSB(h) << 14)	/* Channel dst rq (most significant bits) */
#define	ATC_SOD			(0x1 << 16)	/* Stop On Done */
#define	ATC_LOCK_IF		(0x1 << 20)	/* Interface Lock */
#define	ATC_LOCK_B		(0x1 << 21)	/* AHB Bus Lock */
#define	ATC_LOCK_IF_L		(0x1 << 22)	/* Master Interface Arbiter Lock */
#define		ATC_LOCK_IF_L_CHUNK	(0x0 << 22)
#define		ATC_LOCK_IF_L_BUFFER	(0x1 << 22)
#define	ATC_AHB_PROT_MASK	(0x7 << 24)	/* AHB Protection */
#define	ATC_FIFOCFG_MASK	(0x3 << 28)	/* FIFO Request Configuration */
#define		ATC_FIFOCFG_LARGESTBURST	(0x0 << 28)
#define		ATC_FIFOCFG_HALFFIFO		(0x1 << 28)
#define		ATC_FIFOCFG_ENOUGHSPACE		(0x2 << 28)


#endif /* AT_HDMAC_H */
