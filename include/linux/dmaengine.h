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

#ifdef CONFIG_DMA_ENGINE

#include <linux/device.h>
#include <linux/uio.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/rcupdate.h>

/**
 * enum dma_event - resource PNP/power managment events
 * @DMA_RESOURCE_SUSPEND: DMA device going into low power state
 * @DMA_RESOURCE_RESUME: DMA device returning to full power
 * @DMA_RESOURCE_ADDED: DMA device added to the system
 * @DMA_RESOURCE_REMOVED: DMA device removed from the system
 */
enum dma_event {
	DMA_RESOURCE_SUSPEND,
	DMA_RESOURCE_RESUME,
	DMA_RESOURCE_ADDED,
	DMA_RESOURCE_REMOVED,
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
 * @client: ptr to the client user of this chan, will be %NULL when unused
 * @device: ptr to the dma device who supplies this channel, always !%NULL
 * @cookie: last cookie value returned to client
 * @chan_id: channel ID for sysfs
 * @class_dev: class device for sysfs
 * @refcount: kref, used in "bigref" slow-mode
 * @slow_ref: indicates that the DMA channel is free
 * @rcu: the DMA channel's RCU head
 * @client_node: used to add this to the client chan list
 * @device_node: used to add this to the device chan list
 * @local: per-cpu pointer to a struct dma_chan_percpu
 */
struct dma_chan {
	struct dma_client *client;
	struct dma_device *device;
	dma_cookie_t cookie;

	/* sysfs */
	int chan_id;
	struct class_device class_dev;

	struct kref refcount;
	int slow_ref;
	struct rcu_head rcu;

	struct list_head client_node;
	struct list_head device_node;
	struct dma_chan_percpu *local;
};

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
 */
typedef void (*dma_event_callback) (struct dma_client *client,
		struct dma_chan *chan, enum dma_event event);

/**
 * struct dma_client - info on the entity making use of DMA services
 * @event_callback: func ptr to call when something happens
 * @chan_count: number of chans allocated
 * @chans_desired: number of chans requested. Can be +/- chan_count
 * @lock: protects access to the channels list
 * @channels: the list of DMA channels allocated
 * @global_node: list_head for global dma_client_list
 */
struct dma_client {
	dma_event_callback	event_callback;
	unsigned int		chan_count;
	unsigned int		chans_desired;

	spinlock_t		lock;
	struct list_head	channels;
	struct list_head	global_node;
};

/**
 * struct dma_device - info on the entity supplying DMA services
 * @chancnt: how many DMA channels are supported
 * @channels: the list of struct dma_chan
 * @global_node: list_head for global dma_device_list
 * @refcount: reference count
 * @done: IO completion struct
 * @dev_id: unique device ID
 * @device_alloc_chan_resources: allocate resources and return the
 *	number of allocated descriptors
 * @device_free_chan_resources: release DMA channel's resources
 * @device_memcpy_buf_to_buf: memcpy buf pointer to buf pointer
 * @device_memcpy_buf_to_pg: memcpy buf pointer to struct page
 * @device_memcpy_pg_to_pg: memcpy struct page/offset to struct page/offset
 * @device_memcpy_complete: poll the status of an IOAT DMA transaction
 * @device_memcpy_issue_pending: push appended descriptors to hardware
 */
struct dma_device {

	unsigned int chancnt;
	struct list_head channels;
	struct list_head global_node;

	struct kref refcount;
	struct completion done;

	int dev_id;

	int (*device_alloc_chan_resources)(struct dma_chan *chan);
	void (*device_free_chan_resources)(struct dma_chan *chan);
	dma_cookie_t (*device_memcpy_buf_to_buf)(struct dma_chan *chan,
			void *dest, void *src, size_t len);
	dma_cookie_t (*device_memcpy_buf_to_pg)(struct dma_chan *chan,
			struct page *page, unsigned int offset, void *kdata,
			size_t len);
	dma_cookie_t (*device_memcpy_pg_to_pg)(struct dma_chan *chan,
			struct page *dest_pg, unsigned int dest_off,
			struct page *src_pg, unsigned int src_off, size_t len);
	enum dma_status (*device_memcpy_complete)(struct dma_chan *chan,
			dma_cookie_t cookie, dma_cookie_t *last,
			dma_cookie_t *used);
	void (*device_memcpy_issue_pending)(struct dma_chan *chan);
};

/* --- public DMA engine API --- */

struct dma_client *dma_async_client_register(dma_event_callback event_callback);
void dma_async_client_unregister(struct dma_client *client);
void dma_async_client_chan_request(struct dma_client *client,
		unsigned int number);

/**
 * dma_async_memcpy_buf_to_buf - offloaded copy between virtual addresses
 * @chan: DMA channel to offload copy to
 * @dest: destination address (virtual)
 * @src: source address (virtual)
 * @len: length
 *
 * Both @dest and @src must be mappable to a bus address according to the
 * DMA mapping API rules for streaming mappings.
 * Both @dest and @src must stay memory resident (kernel memory or locked
 * user space pages).
 */
static inline dma_cookie_t dma_async_memcpy_buf_to_buf(struct dma_chan *chan,
	void *dest, void *src, size_t len)
{
	int cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return chan->device->device_memcpy_buf_to_buf(chan, dest, src, len);
}

/**
 * dma_async_memcpy_buf_to_pg - offloaded copy from address to page
 * @chan: DMA channel to offload copy to
 * @page: destination page
 * @offset: offset in page to copy to
 * @kdata: source address (virtual)
 * @len: length
 *
 * Both @page/@offset and @kdata must be mappable to a bus address according
 * to the DMA mapping API rules for streaming mappings.
 * Both @page/@offset and @kdata must stay memory resident (kernel memory or
 * locked user space pages)
 */
static inline dma_cookie_t dma_async_memcpy_buf_to_pg(struct dma_chan *chan,
	struct page *page, unsigned int offset, void *kdata, size_t len)
{
	int cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return chan->device->device_memcpy_buf_to_pg(chan, page, offset,
	                                             kdata, len);
}

/**
 * dma_async_memcpy_pg_to_pg - offloaded copy from page to page
 * @chan: DMA channel to offload copy to
 * @dest_pg: destination page
 * @dest_off: offset in page to copy to
 * @src_pg: source page
 * @src_off: offset in page to copy from
 * @len: length
 *
 * Both @dest_page/@dest_off and @src_page/@src_off must be mappable to a bus
 * address according to the DMA mapping API rules for streaming mappings.
 * Both @dest_page/@dest_off and @src_page/@src_off must stay memory resident
 * (kernel memory or locked user space pages).
 */
static inline dma_cookie_t dma_async_memcpy_pg_to_pg(struct dma_chan *chan,
	struct page *dest_pg, unsigned int dest_off, struct page *src_pg,
	unsigned int src_off, size_t len)
{
	int cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return chan->device->device_memcpy_pg_to_pg(chan, dest_pg, dest_off,
	                                            src_pg, src_off, len);
}

/**
 * dma_async_memcpy_issue_pending - flush pending copies to HW
 * @chan: target DMA channel
 *
 * This allows drivers to push copies to HW in batches,
 * reducing MMIO writes where possible.
 */
static inline void dma_async_memcpy_issue_pending(struct dma_chan *chan)
{
	return chan->device->device_memcpy_issue_pending(chan);
}

/**
 * dma_async_memcpy_complete - poll for transaction completion
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @last: returns last completed cookie, can be NULL
 * @used: returns last issued cookie, can be NULL
 *
 * If @last and @used are passed in, upon return they reflect the driver
 * internal state and can be used with dma_async_is_complete() to check
 * the status of multiple cookies without re-checking hardware state.
 */
static inline enum dma_status dma_async_memcpy_complete(struct dma_chan *chan,
	dma_cookie_t cookie, dma_cookie_t *last, dma_cookie_t *used)
{
	return chan->device->device_memcpy_complete(chan, cookie, last, used);
}

/**
 * dma_async_is_complete - test a cookie against chan state
 * @cookie: transaction identifier to test status of
 * @last_complete: last know completed transaction
 * @last_used: last cookie value handed out
 *
 * dma_async_is_complete() is used in dma_async_memcpy_complete()
 * the test logic is seperated for lightweight testing of multiple cookies
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


/* --- DMA device --- */

int dma_async_device_register(struct dma_device *device);
void dma_async_device_unregister(struct dma_device *device);

/* --- Helper iov-locking functions --- */

struct dma_page_list {
	char *base_address;
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

#endif /* CONFIG_DMA_ENGINE */
#endif /* DMAENGINE_H */
