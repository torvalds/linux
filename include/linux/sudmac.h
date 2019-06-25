/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for the SUDMAC driver
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 */
#ifndef SUDMAC_H
#define SUDMAC_H

#include <linux/dmaengine.h>
#include <linux/shdma-base.h>
#include <linux/types.h>

/* Used by slave DMA clients to request DMA to/from a specific peripheral */
struct sudmac_slave {
	struct shdma_slave	shdma_slave;	/* Set by the platform */
};

/*
 * Supplied by platforms to specify, how a DMA channel has to be configured for
 * a certain peripheral
 */
struct sudmac_slave_config {
	int		slave_id;
};

struct sudmac_channel {
	unsigned long	offset;
	unsigned long	config;
	unsigned long	wait;		/* The configuable range is 0 to 3 */
	unsigned long	dint_end_bit;
};

struct sudmac_pdata {
	const struct sudmac_slave_config *slave;
	int slave_num;
	const struct sudmac_channel *channel;
	int channel_num;
};

/* Definitions for the sudmac_channel.config */
#define SUDMAC_TX_BUFFER_MODE	BIT(0)
#define SUDMAC_RX_END_MODE	BIT(1)

/* Definitions for the sudmac_channel.dint_end_bit */
#define SUDMAC_DMA_BIT_CH0	BIT(0)
#define SUDMAC_DMA_BIT_CH1	BIT(1)

#endif
