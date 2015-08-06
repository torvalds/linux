/*
 * Copyright (C) 2013-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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
#ifndef _PXP_DEVICE
#define _PXP_DEVICE

#include <linux/idr.h>
#include <linux/hash.h>
#include <uapi/linux/pxp_device.h>

struct pxp_irq_info {
	wait_queue_head_t waitq;
	atomic_t irq_pending;
	int hist_status;
};

struct pxp_buffer_hash {
	struct hlist_head *hash_table;
	u32 order;
	spinlock_t hash_lock;
};

struct pxp_buf_obj {
	uint32_t handle;

	uint32_t size;
	uint32_t mem_type;

	unsigned long offset;
	void *virtual;

	struct hlist_node item;
};

struct pxp_chan_obj {
	uint32_t handle;
	struct dma_chan *chan;
};

/* File private data */
struct pxp_file {
	struct file *filp;

	/* record allocated dma buffer */
	struct idr buffer_idr;
	spinlock_t buffer_lock;

	/* record allocated dma channel */
	struct idr channel_idr;
	spinlock_t channel_lock;
};

#endif
