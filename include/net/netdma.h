/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef NETDMA_H
#define NETDMA_H
#include <linux/config.h>
#ifdef CONFIG_NET_DMA
#include <linux/dmaengine.h>

static inline struct dma_chan *get_softnet_dma(void)
{
	struct dma_chan *chan;
	rcu_read_lock();
	chan = rcu_dereference(__get_cpu_var(softnet_data.net_dma));
	if (chan)
		dma_chan_get(chan);
	rcu_read_unlock();
	return chan;
}
#endif /* CONFIG_NET_DMA */
#endif /* NETDMA_H */
