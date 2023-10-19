/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ks8842.h KS8842 platform data struct definition
 * Copyright (c) 2010 Intel Corporation
 */

#ifndef _LINUX_KS8842_H
#define _LINUX_KS8842_H

#include <linux/if_ether.h>

/**
 * struct ks8842_platform_data - Platform data of the KS8842 network driver
 * @macaddr:	The MAC address of the device, set to all 0:s to use the on in
 *		the chip.
 * @rx_dma_channel:	The DMA channel to use for RX, -1 for none.
 * @tx_dma_channel:	The DMA channel to use for TX, -1 for none.
 *
 */
struct ks8842_platform_data {
	u8 macaddr[ETH_ALEN];
	int rx_dma_channel;
	int tx_dma_channel;
};

#endif
