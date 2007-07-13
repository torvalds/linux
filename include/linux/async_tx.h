/*
 * Copyright © 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef _ASYNC_TX_H_
#define _ASYNC_TX_H_
#include <linux/dmaengine.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

/**
 * dma_chan_ref - object used to manage dma channels received from the
 *   dmaengine core.
 * @chan - the channel being tracked
 * @node - node for the channel to be placed on async_tx_master_list
 * @rcu - for list_del_rcu
 * @count - number of times this channel is listed in the pool
 *	(for channels with multiple capabiities)
 */
struct dma_chan_ref {
	struct dma_chan *chan;
	struct list_head node;
	struct rcu_head rcu;
	atomic_t count;
};

/**
 * async_tx_flags - modifiers for the async_* calls
 * @ASYNC_TX_XOR_ZERO_DST: this flag must be used for xor operations where the
 * the destination address is not a source.  The asynchronous case handles this
 * implicitly, the synchronous case needs to zero the destination block.
 * @ASYNC_TX_XOR_DROP_DST: this flag must be used if the destination address is
 * also one of the source addresses.  In the synchronous case the destination
 * address is an implied source, whereas the asynchronous case it must be listed
 * as a source.  The destination address must be the first address in the source
 * array.
 * @ASYNC_TX_ASSUME_COHERENT: skip cache maintenance operations
 * @ASYNC_TX_ACK: immediately ack the descriptor, precludes setting up a
 * dependency chain
 * @ASYNC_TX_DEP_ACK: ack the dependency descriptor.  Useful for chaining.
 * @ASYNC_TX_KMAP_SRC: if the transaction is to be performed synchronously
 * take an atomic mapping (KM_USER0) on the source page(s)
 * @ASYNC_TX_KMAP_DST: if the transaction is to be performed synchronously
 * take an atomic mapping (KM_USER0) on the dest page(s)
 */
enum async_tx_flags {
	ASYNC_TX_XOR_ZERO_DST	 = (1 << 0),
	ASYNC_TX_XOR_DROP_DST	 = (1 << 1),
	ASYNC_TX_ASSUME_COHERENT = (1 << 2),
	ASYNC_TX_ACK		 = (1 << 3),
	ASYNC_TX_DEP_ACK	 = (1 << 4),
	ASYNC_TX_KMAP_SRC	 = (1 << 5),
	ASYNC_TX_KMAP_DST	 = (1 << 6),
};

#ifdef CONFIG_DMA_ENGINE
void async_tx_issue_pending_all(void);
enum dma_status dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx);
void async_tx_run_dependencies(struct dma_async_tx_descriptor *tx);
struct dma_chan *
async_tx_find_channel(struct dma_async_tx_descriptor *depend_tx,
	enum dma_transaction_type tx_type);
#else
static inline void async_tx_issue_pending_all(void)
{
	do { } while (0);
}

static inline enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	return DMA_SUCCESS;
}

static inline void
async_tx_run_dependencies(struct dma_async_tx_descriptor *tx,
	struct dma_chan *host_chan)
{
	do { } while (0);
}

static inline struct dma_chan *
async_tx_find_channel(struct dma_async_tx_descriptor *depend_tx,
	enum dma_transaction_type tx_type)
{
	return NULL;
}
#endif

/**
 * async_tx_sync_epilog - actions to take if an operation is run synchronously
 * @flags: async_tx flags
 * @depend_tx: transaction depends on depend_tx
 * @cb_fn: function to call when the transaction completes
 * @cb_fn_param: parameter to pass to the callback routine
 */
static inline void
async_tx_sync_epilog(unsigned long flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param)
{
	if (cb_fn)
		cb_fn(cb_fn_param);

	if (depend_tx && (flags & ASYNC_TX_DEP_ACK))
		async_tx_ack(depend_tx);
}

void
async_tx_submit(struct dma_chan *chan, struct dma_async_tx_descriptor *tx,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);

struct dma_async_tx_descriptor *
async_xor(struct page *dest, struct page **src_list, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);

struct dma_async_tx_descriptor *
async_xor_zero_sum(struct page *dest, struct page **src_list,
	unsigned int offset, int src_cnt, size_t len,
	u32 *result, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);

struct dma_async_tx_descriptor *
async_memcpy(struct page *dest, struct page *src, unsigned int dest_offset,
	unsigned int src_offset, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);

struct dma_async_tx_descriptor *
async_memset(struct page *dest, int val, unsigned int offset,
	size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);

struct dma_async_tx_descriptor *
async_trigger_callback(enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_fn_param);
#endif /* _ASYNC_TX_H_ */
