/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 */
#ifndef __ASM_ARCH_USB_H
#define __ASM_ARCH_USB_H

#include <linux/dmaengine.h>

#define UX500_MUSB_DMA_NUM_RX_TX_CHANNELS 8

struct ux500_musb_board_data {
	void	**dma_rx_param_array;
	void	**dma_tx_param_array;
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
};

void ux500_add_usb(struct device *parent, resource_size_t base,
		   int irq, int *dma_rx_cfg, int *dma_tx_cfg);
#endif
