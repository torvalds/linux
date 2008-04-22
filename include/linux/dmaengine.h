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
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/rcupdate.h>
#include <linux/dma-mapping.h>

/**
 * enum dma_state - resource PNP/power management state
 * @DMA_RESOURCE_SUSPEND: DMA device going into low power state
 * @DMA_RESOURCE_RESUME: DMA device returning to full power
 * @DMA_RESOURCE_AVAILABLE: DMA device available to the system
 * @DMA_RESOURCE_REMOVED: DMA device removed from the system
 */
enum dma_state {
	DMA_RESOURCE_SUSPEND,
	DMA_RESOURCE_RESUME,
	DMA_RESOURCE_AVAILABLE,
	DMA_RESOURCE_REMOVED,
};

/**
 * enum dma_state_client - state of the channel in the client
 * @DMA_ACK: client would like to use, or was using this channel
 * @DMA_DUP: client has already seen this channel, or is not using this channel
 * @DMA_NAK: client does not want to see any more channels
 */
enum dma_state_client {
	DMA_ACK,
	DMA_DUP,
	DMA_NAK,
};

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
};

/* last transaction type for creation of the capabilities mask */
#define DMA_TX_TYPE_END (DMA_INTERRUPT + 1)

/**
 * enum dma_ctrl_flags - DMA flags to augment operation preparation,
 * 	control completion, and communicate status.
 * @DMA_PREP_INTERRUPT - trigger an interrupt (callback) upon completion of
 * 	this transaction
 * @DMA_CTRL_ACK - the descriptor cannot be reused until the client
 * 	acknowledges receipt, i.e. has has a chance to establish any
 * 	dependency chains
 */
enum dma_ctrl_flags {
	DMA_PREP_INTERRUPT = (1 << 0),
	DMA_CTRL_ACK = (1 << 1),
};

/**
 * dma_cap_mask_t - capabilities bitmap modeled after cpumask_t.
 * See linux/cpumask.h
 */
typedef struct { DECLARE_BITMAP(bits, DMA_TX_TYPE_END); } dma_cap_mask_t;

/**
 * struct dma_chan_percpu - the per-CPU part of struct dma_chan
 * @refcount: local_t used for open-coded "bigref" counting
 * @memcpy_count: transaction counter
 * @bytes_transferred: byte counter
 */

struct dma_chan_percpu {
	local_t refcount;
	/* stats */
	unsigned long memcpy_count;
	unsigned long bytes_transferred;
};

/**
 * struct dma_chan - devices supply DMA channels, clients use them
 * @device: ptr to the dma device who supplies this channel, always !%NULL
 * @cookie: last cookie value returned to client
 * @chan_id: channel ID for sysfs
 * @class_dev: class device for sysfs
 * @refcount: kref, used in "bigref" slow-mode
 * @slow_ref: indicates that the DMA channel is free
 * @rcu: the DMA channel's RCU head
 * @device_node: used to add this to the device chan list
 * @local: per-cpu pointer to a struct dma_chan_percpu
 */
struct dma_chan {
	struct dma_device *device;
	dma_cookie_t cookie;

	/* sysfs */
	int chan_id;
	struct device dev;

	struct kref refcount;
	int slow_ref;
	struct rcu_head rcu;

	struct list_head device_node;
	struct dma_chan_percpu *local;
};

#define to_dma_chan(p) container_of(p, struct dma_chan, dev)

void dma_chan_cleanup(struct kref *kref);

static inline void dma_chan_get(struct dma_chan *chan)
{
	if (unlikely(chan->slow_ref))
		kref_get(&chan->refcount);
	else {
		local_inc(&(per_cpu_ptr(chan->local, get_cpu())->refcount));
		put_cpu();
	}
}

static inline void dma_chan_put(struct dma_chan *chan)
{
	if (unlikely(chan->slow_ref))
		kref_put(&chan->refcount, dma_chan_cleanup);
	else {
		local_dec(&(per_cpu_ptr(chan->local, get_cpu())->refcount));
		put_cpu();
	}
}

/*
 * typedef dma_event_callback - function pointer to a DMA event callback
 * For each channel added to the system this routine is called for each client.
 * If the client would like to use the channel it returns '1' to signal (ack)
 * the dmaengine core to take out a reference on the channel and its
 * corresponding device.  A client must not 'ack' an available channel more
 * than once.  When a channel is removed all clients are notified.  If a client
 * is using the channel it must 'ack' the removal.  A client must not 'ack' a
 * removed channel more than once.
 * @client - 'this' pointer for the client context
 * @chan - channel to be acted upon
 * @state - available or removed
 */
struct dma_client;
typedef enum dma_state_client (*dma_event_callback) (struct dma_client *client,
		struct dma_chan *chan, enum dma_state state);

/**
 * struct dma_client - info on the entity making use of DMA services
 * @event_callback: func ptr to call when something happens
 * @cap_mask: only return channels that satisfy the requested capabilities
 *  a value of zero corresponds to any capability
 * @global_node: list_head for global dma_client_list
 */
struct dma_client {
	dma_event_callback	event_callback;
	dma_cap_mask_t		cap_mask;
	struct list_head	global_node;
};

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
 * @channels: the list of struct dma_chan
 * @global_node: list_head for global dma_device_list
 * @cap_mask: one or more dma_capability flags
 * @max_xor: maximum number of xor sources, 0 if no capability
 * @refcount: reference count
 * @done: IO completion struct
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
 * @device_issue_pending: push pending transactions to hardware
 */
struct dma_device {

	unsigned int chancnt;
	struct list_head channels;
	struct list_head global_node;
	dma_cap_mask_t  cap_mask;
	int max_xor;

	struct kref refcount;
	struct completion done;

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

	enum dma_status (*device_is_tx_complete)(struct dma_chan *chan,
			dma_cookie_t cookie, dma_cookie_t *last,
			dma_cookie_t *used);
	void (*device_issue_pending)(struct dma_chan *chan);
};

/* --- public DMA engine API --- */

void dma_async_client_register(struct dma_client *client);
void dma_async_client_unregister(struct dma_client *client);
void dma_async_client_chan_request(struct dma_client *client);
dma_cookie_t dma_async_memcpy_buf_to_buf(struct dma_chan *chan,
	void *dest, void *src, size_t len);
dma_cookie_t dma_async_memcpy_buf_to_pg(struct dma_chan *chan,
	struct page *page, unsigned int offset, void *kdata, size_t len);
dma_cookie_t dma_async_memcpy_pg_to_pg(struct dma_chan *chan,
	struct page *dest_pg, unsigned int dest_off, struct page *src_pg,
	unsigned int src_off, size_t len);
void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan);

static inline void
async_tx_ack(struct dma_async_tx_descriptor *tx)
{
	tx->flags |= DMA_CTRL_ACK;
}

static inline int
async_tx_test_ack(struct dma_async_tx_descriptor *tx)
{
	return tx->flags & DMA_CTRL_ACK;
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

/* --- DMA device --- */

int dma_async_device_register(struct dma_device *device);
void dma_async_device_unregister(struct dma_device *device);

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
