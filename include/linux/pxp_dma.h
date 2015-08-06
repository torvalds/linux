/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef _PXP_DMA
#define _PXP_DMA

#include <uapi/linux/pxp_dma.h>

struct pxp_tx_desc {
	struct dma_async_tx_descriptor txd;
	struct list_head tx_list;
	struct list_head list;
	int len;
	union {
		struct pxp_layer_param s0_param;
		struct pxp_layer_param out_param;
		struct pxp_layer_param ol_param;
		struct pxp_layer_param processing_param;
	} layer_param;
	struct pxp_proc_data proc_data;

	u32 hist_status;	/* Histogram output status */

	struct pxp_tx_desc *next;
};

struct pxp_channel {
	struct dma_chan dma_chan;
	dma_cookie_t completed;	/* last completed cookie */
	enum pxp_channel_status status;
	void *client;		/* Only one client per channel */
	unsigned int n_tx_desc;
	struct pxp_tx_desc *desc;	/* allocated tx-descriptors */
	struct list_head active_list;	/* active tx-descriptors */
	struct list_head free_list;	/* free tx-descriptors */
	struct list_head queue;	/* queued tx-descriptors */
	struct list_head list;	/* track queued channel number */
	spinlock_t lock;	/* protects sg[0,1], queue */
	struct mutex chan_mutex;	/* protects status, cookie, free_list */
	int active_buffer;
	unsigned int eof_irq;
	char eof_name[16];	/* EOF IRQ name for request_irq()  */
};

#define to_tx_desc(tx) container_of(tx, struct pxp_tx_desc, txd)
#define to_pxp_channel(d) container_of(d, struct pxp_channel, dma_chan)

void pxp_txd_ack(struct dma_async_tx_descriptor *txd,
		 struct pxp_channel *pxp_chan);

#ifdef CONFIG_MXC_PXP_CLIENT_DEVICE
int register_pxp_device(void);
void unregister_pxp_device(void);
#else
int register_pxp_device(void) { return 0; }
void unregister_pxp_device(void) {}
#endif
void pxp_fill(
        u32 bpp,
        u32 value,
        u32 width,
        u32 height,
        u32 output_buffer,
        u32 output_pitch);

void m4_process(void);
#endif
