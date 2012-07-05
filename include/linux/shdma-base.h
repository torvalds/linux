/*
 * Dmaengine driver base library for DMA controllers, found on SH-based SoCs
 *
 * extracted from shdma.c and headers
 *
 * Copyright (C) 2011-2012 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2009 Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 * Copyright (C) 2009 Renesas Solutions, Inc. All rights reserved.
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#ifndef SHDMA_BASE_H
#define SHDMA_BASE_H

#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/types.h>

/**
 * shdma_pm_state - DMA channel PM state
 * SHDMA_PM_ESTABLISHED:	either idle or during data transfer
 * SHDMA_PM_BUSY:		during the transfer preparation, when we have to
 *				drop the lock temporarily
 * SHDMA_PM_PENDING:	transfers pending
 */
enum shdma_pm_state {
	SHDMA_PM_ESTABLISHED,
	SHDMA_PM_BUSY,
	SHDMA_PM_PENDING,
};

struct device;

/*
 * Drivers, using this library are expected to embed struct shdma_dev,
 * struct shdma_chan, struct shdma_desc, and struct shdma_slave
 * in their respective device, channel, descriptor and slave objects.
 */

struct shdma_slave {
	int slave_id;
};

struct shdma_desc {
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	enum dma_transfer_direction direction;
	dma_cookie_t cookie;
	int chunks;
	int mark;
};

struct shdma_chan {
	spinlock_t chan_lock;		/* Channel operation lock */
	struct list_head ld_queue;	/* Link descriptors queue */
	struct list_head ld_free;	/* Free link descriptors */
	struct dma_chan dma_chan;	/* DMA channel */
	struct device *dev;		/* Channel device */
	void *desc;			/* buffer for descriptor array */
	int desc_num;			/* desc count */
	size_t max_xfer_len;		/* max transfer length */
	int id;				/* Raw id of this channel */
	int irq;			/* Channel IRQ */
	int slave_id;			/* Client ID for slave DMA */
	enum shdma_pm_state pm_state;
};

/**
 * struct shdma_ops - simple DMA driver operations
 * desc_completed:	return true, if this is the descriptor, that just has
 *			completed (atomic)
 * halt_channel:	stop DMA channel operation (atomic)
 * channel_busy:	return true, if the channel is busy (atomic)
 * slave_addr:		return slave DMA address
 * desc_setup:		set up the hardware specific descriptor portion (atomic)
 * set_slave:		bind channel to a slave
 * setup_xfer:		configure channel hardware for operation (atomic)
 * start_xfer:		start the DMA transfer (atomic)
 * embedded_desc:	return Nth struct shdma_desc pointer from the
 *			descriptor array
 * chan_irq:		process channel IRQ, return true if a transfer has
 *			completed (atomic)
 */
struct shdma_ops {
	bool (*desc_completed)(struct shdma_chan *, struct shdma_desc *);
	void (*halt_channel)(struct shdma_chan *);
	bool (*channel_busy)(struct shdma_chan *);
	dma_addr_t (*slave_addr)(struct shdma_chan *);
	int (*desc_setup)(struct shdma_chan *, struct shdma_desc *,
			  dma_addr_t, dma_addr_t, size_t *);
	int (*set_slave)(struct shdma_chan *, int, bool);
	void (*setup_xfer)(struct shdma_chan *, int);
	void (*start_xfer)(struct shdma_chan *, struct shdma_desc *);
	struct shdma_desc *(*embedded_desc)(void *, int);
	bool (*chan_irq)(struct shdma_chan *, int);
};

struct shdma_dev {
	struct dma_device dma_dev;
	struct shdma_chan **schan;
	const struct shdma_ops *ops;
	size_t desc_size;
};

#define shdma_for_each_chan(c, d, i) for (i = 0, c = (d)->schan[0]; \
				i < (d)->dma_dev.chancnt; c = (d)->schan[++i])

int shdma_request_irq(struct shdma_chan *, int,
			   unsigned long, const char *);
void shdma_free_irq(struct shdma_chan *);
bool shdma_reset(struct shdma_dev *sdev);
void shdma_chan_probe(struct shdma_dev *sdev,
			   struct shdma_chan *schan, int id);
void shdma_chan_remove(struct shdma_chan *schan);
int shdma_init(struct device *dev, struct shdma_dev *sdev,
		    int chan_num);
void shdma_cleanup(struct shdma_dev *sdev);

#endif
