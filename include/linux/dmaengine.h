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
#ifndef DMAENGINE_H
#define DMAENGINE_H

#include <linux/device.h>
#include <linux/uio.h>
#include <linux/dma-mapping.h>

/**
 * typedef dma_cookie_t - an opaque DMA cookie
 *
 * if dma_cookie_t is >0 it's a DMA request cookie, <0 it's an error code
 */
typedef s32 dma_cookie_t;

#define dma_submit_error(cookie) ((cookie) < 0 ? 1 : 0)

/**
 * enum dma_status - DMA transaction status
 * @DMA_SUCCESS: transaction completed successfully
 * @DMA_IN_PROGRESS: transaction not yet processed
 * @DMA_ERROR: transaction failed
 */
enum dma_status {
	DMA_SUCCESS,
	DMA_IN_PROGRESS,
	DMA_ERROR,
};

/**
 * enum dma_transaction_type - DMA transaction types/indexes
 */
enum dma_transaction_type {
	DMA_MEMCPY,
	DMA_XOR,
	DMA_PQ_XOR,
	DMA_DUAL_XOR,
	DMA_PQ_UPDATE,
	DMA_ZERO_SUM,
	DMA_PQ_ZERO_SUM,
	DMA_MEMSET,
	DMA_MEMCPY_CRC32C,
	DMA_INTERRUPT,
	DMA_PRIVATE,
	DMA_SLAVE,
};

/* last transaction type for creation of the capabilities mask */
#define DMA_TX_TYPE_END (DMA_SLAVE + 1)


/**
 * enum dma_ctrl_flags - DMA flags to augment operation preparation,
 * 	control completion, and communicate status.
 * @DMA_PREP_INTERRUPT - trigger an interrupt (callback) upon completion of
 * 	this transaction
 * @DMA_CTRL_ACK - the descriptor cannot be reused until the client
 * 	acknowledges receipt, i.e. has has a chance to establish any
 * 	dependency chains
 * @DMA_COMPL_SKIP_SRC_UNMAP - set to disable dma-unmapping the source buffer(s)
 * @DMA_COMPL_SKIP_DEST_UNMAP - set to disable dma-unmapping the destination(s)
 */
enum dma_ctrl_flags {
	DMA_PREP_INTERRUPT = (1 << 0),
	DMA_CTRL_ACK = (1 << 1),
	DMA_COMPL_SKIP_SRC_UNMAP = (1 << 2),
	DMA_COMPL_SKIP_DEST_UNMAP = (1 << 3),
};

/**
 * dma_cap_mask_t - capabilities bitmap modeled after cpumask_t.
 * See linux/cpumask.h
 */
typedef struct { DECLARE_BITMAP(bits, DMA_TX_TYPE_END); } dma_cap_mask_t;

/**
 * struct dma_chan_percpu - the per-CPU part of struct dma_chan
 * @memcpy_count: transaction counter
 * @bytes_transferred: byte counter
 */

struct dma_chan_percpu {
	/* stats */
	unsigned long memcpy_count;
	unsigned long bytes_transferred;
};

/**
 * struct dma_chan - devices supply DMA channels, clients use them
 * @device: ptr to the dma device who supplies this channel, always !%NULL
 * @cookie: last cookie value returned to client
 * @chan_id: channel ID for sysfs
 * @dev: class device for sysfs
 * @device_node: used to add this to the device chan list
 * @local: per-cpu pointer to a struct dma_chan_percpu
 * @client-count: how many clients are using this channel
 * @table_count: number of appearances in the mem-to-mem allocation table
 * @private: private data for certain client-channel associations
 */
struct dma_chan {
	struct dma_device *device;
	dma_cookie_t cookie;

	/* sysfs */
	int chan_id;
	struct dma_chan_dev *dev;

	struct list_head device_node;
	struct dma_chan_percpu *local;
	int client_count;
	int table_count;
	void *private;
};

/**
 * struct dma_chan_dev - relate sysfs device node to backing channel device
 * @chan - driver channel device
 * @device - sysfs device
 * @dev_id - parent dma_device dev_id
 * @idr_ref - reference count to gate release of dma_device dev_id
 */
struct dma_chan_dev {
	struct dma_chan *chan;
	struct device device;
	int dev_id;
	atomic_t *idr_ref;
};

static inline const char *dma_chan_name(struct dma_chan *chan)
{
	return dev_name(&chan->dev->device);
}

void dma_chan_cleanup(struct kref *kref);

/**
 * typedef dma_filter_fn - callback filter for dma_request_channel
 * @chan: channel to be reviewed
 * @filter_param: opaque parameter passed through dma_request_channel
 *
 * When this optional parameter is specified in a call to dma_request_channel a
 * suitable channel is passed to this routine for further dispositioning before
 * being returned.  Where 'suitable' indicates a non-busy channel that
 * satisfies the given capability mask.  It returns 'true' to indicate that the
 * channel is suitable.
 */
typedef bool (*dma_filter_fn)(struct dma_chan *chan, void *filter_param);

typedef void (*dma_async_tx_callback)(void *dma_async_param);
/**
 * struct dma_async_tx_descriptor - async transaction descriptor
 * ---dma generic offload fields---
 * @cookie: tracking cookie for this transaction, set to -EBUSY if
 *	this tx is sitting on a dependency list
 * @flags: flags to augment operation preparation, control completion, and
 * 	communicate status
 * @phys: physical address of the descriptor
 * @tx_list: driver common field for operations that require multiple
 *	descriptors
 * @chan: target channel for this operation
 * @tx_submit: set the prepared descriptor(s) to be executed by the engine
 * @callback: routine to call after this operation is complete
 * @callback_param: general parameter to pass to the callback routine
 * ---async_tx api specific fields---
 * @next: at completion submit this descriptor
 * @parent: pointer to the next level up in the dependency chain
 * @lock: protect the parent and next pointers
 */
struct dma_async_tx_descriptor {
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags; /* not a 'long' to pack with cookie */
	dma_addr_t phys;
	struct list_head tx_list;
	struct dma_chan *chan;
	dma_cookie_t (*tx_submit)(struct dma_async_tx_descriptor *tx);
	dma_async_tx_callback callback;
	void *callback_param;
	struct dma_async_tx_descriptor *next;
	struct dma_async_tx_descriptor *parent;
	spinlock_t lock;
};

/**
 * struct dma_device - info on the entity supplying DMA services
 * @chancnt: how many DMA channels are supported
 * @privatecnt: how many DMA channels are requested by dma_request_channel
 * @channels: the list of struct dma_chan
 * @global_node: list_head for global dma_device_list
 * @cap_mask: one or more dma_capability flags
 * @max_xor: maximum number of xor sources, 0 if no capability
 * @dev_id: unique device ID
 * @dev: struct device reference for dma mapping api
 * @device_alloc_chan_resources: allocate resources and return the
 *	number of allocated descriptors
 * @device_free_chan_resources: release DMA channel's resources
 * @device_prep_dma_memcpy: prepares a memcpy operation
 * @device_prep_dma_xor: prepares a xor operation
 * @device_prep_dma_zero_sum: prepares a zero_sum operation
 * @device_prep_dma_memset: prepares a memset operation
 * @device_prep_dma_interrupt: prepares an end of chain interrupt operation
 * @device_prep_slave_sg: prepares a slave dma operation
 * @device_terminate_all: terminate all pending operations
 * @device_is_tx_complete: poll for transaction completion
 * @device_issue_pending: push pending transactions to hardware
 */
struct dma_device {

	unsigned int chancnt;
	unsigned int privatecnt;
	struct list_head channels;
	struct list_head global_node;
	dma_cap_mask_t  cap_mask;
	int max_xor;

	int dev_id;
	struct device *dev;

	int (*device_alloc_chan_resources)(struct dma_chan *chan);
	void (*device_free_chan_resources)(struct dma_chan *chan);

	struct dma_async_tx_descriptor *(*device_prep_dma_memcpy)(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_xor)(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
		unsigned int src_cnt, size_t len, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_zero_sum)(
		struct dma_chan *chan, dma_addr_t *src,	unsigned int src_cnt,
		size_t len, u32 *result, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_memset)(
		struct dma_chan *chan, dma_addr_t dest, int value, size_t len,
		unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_interrupt)(
		struct dma_chan *chan, unsigned long flags);

	struct dma_async_tx_descriptor *(*device_prep_slave_sg)(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags);
	void (*device_terminate_all)(struct dma_chan *chan);

	enum dma_status (*device_is_tx_complete)(struct dma_chan *chan,
			dma_cookie_t cookie, dma_cookie_t *last,
			dma_cookie_t *used);
	void (*device_issue_pending)(struct dma_chan *chan);
};

/* --- public DMA engine API --- */

#ifdef CONFIG_DMA_ENGINE
void dmaengine_get(void);
void dmaengine_put(void);
#else
static inline void dmaengine_get(void)
{
}
static inline void dmaengine_put(void)
{
}
#endif

#ifdef CONFIG_NET_DMA
#define net_dmaengine_get()	dmaengine_get()
#define net_dmaengine_put()	dmaengine_put()
#else
static inline void net_dmaengine_get(void)
{
}
static inline void net_dmaengine_put(void)
{
}
#endif

#ifdef CONFIG_ASYNC_TX_DMA
#define async_dmaengine_get()	dmaengine_get()
#define async_dmaengine_put()	dmaengine_put()
#define async_dma_find_channel(type) dma_find_channel(type)
#else
static inline void async_dmaengine_get(void)
{
}
static inline void async_dmaengine_put(void)
{
}
static inline struct dma_chan *
async_dma_find_channel(enum dma_transaction_type type)
{
	return NULL;
}
#endif

dma_cookie_t dma_async_memcpy_buf_to_buf(struct dma_chan *chan,
	void *dest, void *src, size_t len);
dma_cookie_t dma_async_memcpy_buf_to_pg(struct dma_chan *chan,
	struct page *page, unsigned int offset, void *kdata, size_t len);
dma_cookie_t dma_async_memcpy_pg_to_pg(struct dma_chan *chan,
	struct page *dest_pg, unsigned int dest_off, struct page *src_pg,
	unsigned int src_off, size_t len);
void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan);

static inline void async_tx_ack(struct dma_async_tx_descriptor *tx)
{
	tx->flags |= DMA_CTRL_ACK;
}

static inline void async_tx_clear_ack(struct dma_async_tx_descriptor *tx)
{
	tx->flags &= ~DMA_CTRL_ACK;
}

static inline bool async_tx_test_ack(struct dma_async_tx_descriptor *tx)
{
	return (tx->flags & DMA_CTRL_ACK) == DMA_CTRL_ACK;
}

#define first_dma_cap(mask) __first_dma_cap(&(mask))
static inline int __first_dma_cap(const dma_cap_mask_t *srcp)
{
	return min_t(int, DMA_TX_TYPE_END,
		find_first_bit(srcp->bits, DMA_TX_TYPE_END));
}

#define next_dma_cap(n, mask) __next_dma_cap((n), &(mask))
static inline int __next_dma_cap(int n, const dma_cap_mask_t *srcp)
{
	return min_t(int, DMA_TX_TYPE_END,
		find_next_bit(srcp->bits, DMA_TX_TYPE_END, n+1));
}

#define dma_cap_set(tx, mask) __dma_cap_set((tx), &(mask))
static inline void
__dma_cap_set(enum dma_transaction_type tx_type, dma_cap_mask_t *dstp)
{
	set_bit(tx_type, dstp->bits);
}

#define dma_cap_clear(tx, mask) __dma_cap_clear((tx), &(mask))
static inline void
__dma_cap_clear(enum dma_transaction_type tx_type, dma_cap_mask_t *dstp)
{
	clear_bit(tx_type, dstp->bits);
}

#define dma_cap_zero(mask) __dma_cap_zero(&(mask))
static inline void __dma_cap_zero(dma_cap_mask_t *dstp)
{
	bitmap_zero(dstp->bits, DMA_TX_TYPE_END);
}

#define dma_has_cap(tx, mask) __dma_has_cap((tx), &(mask))
static inline int
__dma_has_cap(enum dma_transaction_type tx_type, dma_cap_mask_t *srcp)
{
	return test_bit(tx_type, srcp->bits);
}

#define for_each_dma_cap_mask(cap, mask) \
	for ((cap) = first_dma_cap(mask);	\
		(cap) < DMA_TX_TYPE_END;	\
		(cap) = next_dma_cap((cap), (mask)))

/**
 * dma_async_issue_pending - flush pending transactions to HW
 * @chan: target DMA channel
 *
 * This allows drivers to push copies to HW in batches,
 * reducing MMIO writes where possible.
 */
static inline void dma_async_issue_pending(struct dma_chan *chan)
{
	chan->device->device_issue_pending(chan);
}

#define dma_async_memcpy_issue_pending(chan) dma_async_issue_pending(chan)

/**
 * dma_async_is_tx_complete - poll for transaction completion
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @last: returns last completed cookie, can be NULL
 * @used: returns last issued cookie, can be NULL
 *
 * If @last and @used are passed in, upon return they reflect the driver
 * internal state and can be used with dma_async_is_complete() to check
 * the status of multiple cookies without re-checking hardware state.
 */
static inline enum dma_status dma_async_is_tx_complete(struct dma_chan *chan,
	dma_cookie_t cookie, dma_cookie_t *last, dma_cookie_t *used)
{
	return chan->device->device_is_tx_complete(chan, cookie, last, used);
}

#define dma_async_memcpy_complete(chan, cookie, last, used)\
	dma_async_is_tx_complete(chan, cookie, last, used)

/**
 * dma_async_is_complete - test a cookie against chan state
 * @cookie: transaction identifier to test status of
 * @last_complete: last know completed transaction
 * @last_used: last cookie value handed out
 *
 * dma_async_is_complete() is used in dma_async_memcpy_complete()
 * the test logic is separated for lightweight testing of multiple cookies
 */
static inline enum dma_status dma_async_is_complete(dma_cookie_t cookie,
			dma_cookie_t last_complete, dma_cookie_t last_used)
{
	if (last_complete <= last_used) {
		if ((cookie <= last_complete) || (cookie > last_used))
			return DMA_SUCCESS;
	} else {
		if ((cookie <= last_complete) && (cookie > last_used))
			return DMA_SUCCESS;
	}
	return DMA_IN_PROGRESS;
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie);
#ifdef CONFIG_DMA_ENGINE
enum dma_status dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx);
void dma_issue_pending_all(void);
#else
static inline enum dma_status dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	return DMA_SUCCESS;
}
static inline void dma_issue_pending_all(void)
{
	do { } while (0);
}
#endif

/* --- DMA device --- */

int dma_async_device_register(struct dma_device *device);
void dma_async_device_unregister(struct dma_device *device);
void dma_run_dependencies(struct dma_async_tx_descriptor *tx);
struct dma_chan *dma_find_channel(enum dma_transaction_type tx_type);
#define dma_request_channel(mask, x, y) __dma_request_channel(&(mask), x, y)
struct dma_chan *__dma_request_channel(dma_cap_mask_t *mask, dma_filter_fn fn, void *fn_param);
void dma_release_channel(struct dma_chan *chan);

/* --- Helper iov-locking functions --- */

struct dma_page_list {
	char __user *base_address;
	int nr_pages;
	struct page **pages;
};

struct dma_pinned_list {
	int nr_iovecs;
	struct dma_page_list page_list[0];
};

struct dma_pinned_list *dma_pin_iovec_pages(struct iovec *iov, size_t len);
void dma_unpin_iovec_pages(struct dma_pinned_list* pinned_list);

dma_cookie_t dma_memcpy_to_iovec(struct dma_chan *chan, struct iovec *iov,
	struct dma_pinned_list *pinned_list, unsigned char *kdata, size_t len);
dma_cookie_t dma_memcpy_pg_to_iovec(struct dma_chan *chan, struct iovec *iov,
	struct dma_pinned_list *pinned_list, struct page *page,
	unsigned int offset, size_t len);

#endif /* DMAENGINE_H */
