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
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/bitmap.h>
#include <asm/page.h>

/**
 * typedef dma_cookie_t - an opaque DMA cookie
 *
 * if dma_cookie_t is >0 it's a DMA request cookie, <0 it's an error code
 */
typedef s32 dma_cookie_t;
#define DMA_MIN_COOKIE	1
#define DMA_MAX_COOKIE	INT_MAX

#define dma_submit_error(cookie) ((cookie) < 0 ? 1 : 0)

/**
 * enum dma_status - DMA transaction status
 * @DMA_SUCCESS: transaction completed successfully
 * @DMA_IN_PROGRESS: transaction not yet processed
 * @DMA_PAUSED: transaction is paused
 * @DMA_ERROR: transaction failed
 */
enum dma_status {
	DMA_SUCCESS,
	DMA_IN_PROGRESS,
	DMA_PAUSED,
	DMA_ERROR,
};

/**
 * enum dma_transaction_type - DMA transaction types/indexes
 *
 * Note: The DMA_ASYNC_TX capability is not to be set by drivers.  It is
 * automatically set as dma devices are registered.
 */
enum dma_transaction_type {
	DMA_MEMCPY,
	DMA_XOR,
	DMA_PQ,
	DMA_XOR_VAL,
	DMA_PQ_VAL,
	DMA_MEMSET,
	DMA_INTERRUPT,
	DMA_SG,
	DMA_PRIVATE,
	DMA_ASYNC_TX,
	DMA_SLAVE,
	DMA_CYCLIC,
};

/* last transaction type for creation of the capabilities mask */
#define DMA_TX_TYPE_END (DMA_CYCLIC + 1)


/**
 * enum dma_ctrl_flags - DMA flags to augment operation preparation,
 *  control completion, and communicate status.
 * @DMA_PREP_INTERRUPT - trigger an interrupt (callback) upon completion of
 *  this transaction
 * @DMA_CTRL_ACK - if clear, the descriptor cannot be reused until the client
 *  acknowledges receipt, i.e. has has a chance to establish any dependency
 *  chains
 * @DMA_COMPL_SKIP_SRC_UNMAP - set to disable dma-unmapping the source buffer(s)
 * @DMA_COMPL_SKIP_DEST_UNMAP - set to disable dma-unmapping the destination(s)
 * @DMA_COMPL_SRC_UNMAP_SINGLE - set to do the source dma-unmapping as single
 * 	(if not set, do the source dma-unmapping as page)
 * @DMA_COMPL_DEST_UNMAP_SINGLE - set to do the destination dma-unmapping as single
 * 	(if not set, do the destination dma-unmapping as page)
 * @DMA_PREP_PQ_DISABLE_P - prevent generation of P while generating Q
 * @DMA_PREP_PQ_DISABLE_Q - prevent generation of Q while generating P
 * @DMA_PREP_CONTINUE - indicate to a driver that it is reusing buffers as
 *  sources that were the result of a previous operation, in the case of a PQ
 *  operation it continues the calculation with new sources
 * @DMA_PREP_FENCE - tell the driver that subsequent operations depend
 *  on the result of this operation
 */
enum dma_ctrl_flags {
	DMA_PREP_INTERRUPT = (1 << 0),
	DMA_CTRL_ACK = (1 << 1),
	DMA_COMPL_SKIP_SRC_UNMAP = (1 << 2),
	DMA_COMPL_SKIP_DEST_UNMAP = (1 << 3),
	DMA_COMPL_SRC_UNMAP_SINGLE = (1 << 4),
	DMA_COMPL_DEST_UNMAP_SINGLE = (1 << 5),
	DMA_PREP_PQ_DISABLE_P = (1 << 6),
	DMA_PREP_PQ_DISABLE_Q = (1 << 7),
	DMA_PREP_CONTINUE = (1 << 8),
	DMA_PREP_FENCE = (1 << 9),
};

/**
 * enum dma_ctrl_cmd - DMA operations that can optionally be exercised
 * on a running channel.
 * @DMA_TERMINATE_ALL: terminate all ongoing transfers
 * @DMA_PAUSE: pause ongoing transfers
 * @DMA_RESUME: resume paused transfer
 * @DMA_SLAVE_CONFIG: this command is only implemented by DMA controllers
 * that need to runtime reconfigure the slave channels (as opposed to passing
 * configuration data in statically from the platform). An additional
 * argument of struct dma_slave_config must be passed in with this
 * command.
 * @FSLDMA_EXTERNAL_START: this command will put the Freescale DMA controller
 * into external start mode.
 */
enum dma_ctrl_cmd {
	DMA_TERMINATE_ALL,
	DMA_PAUSE,
	DMA_RESUME,
	DMA_SLAVE_CONFIG,
	FSLDMA_EXTERNAL_START,
};

/**
 * enum sum_check_bits - bit position of pq_check_flags
 */
enum sum_check_bits {
	SUM_CHECK_P = 0,
	SUM_CHECK_Q = 1,
};

/**
 * enum pq_check_flags - result of async_{xor,pq}_zero_sum operations
 * @SUM_CHECK_P_RESULT - 1 if xor zero sum error, 0 otherwise
 * @SUM_CHECK_Q_RESULT - 1 if reed-solomon zero sum error, 0 otherwise
 */
enum sum_check_flags {
	SUM_CHECK_P_RESULT = (1 << SUM_CHECK_P),
	SUM_CHECK_Q_RESULT = (1 << SUM_CHECK_Q),
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
	struct dma_chan_percpu __percpu *local;
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

/**
 * enum dma_slave_buswidth - defines bus with of the DMA slave
 * device, source or target buses
 */
enum dma_slave_buswidth {
	DMA_SLAVE_BUSWIDTH_UNDEFINED = 0,
	DMA_SLAVE_BUSWIDTH_1_BYTE = 1,
	DMA_SLAVE_BUSWIDTH_2_BYTES = 2,
	DMA_SLAVE_BUSWIDTH_4_BYTES = 4,
	DMA_SLAVE_BUSWIDTH_8_BYTES = 8,
};

/**
 * struct dma_slave_config - dma slave channel runtime config
 * @direction: whether the data shall go in or out on this slave
 * channel, right now. DMA_TO_DEVICE and DMA_FROM_DEVICE are
 * legal values, DMA_BIDIRECTIONAL is not acceptable since we
 * need to differentiate source and target addresses.
 * @src_addr: this is the physical address where DMA slave data
 * should be read (RX), if the source is memory this argument is
 * ignored.
 * @dst_addr: this is the physical address where DMA slave data
 * should be written (TX), if the source is memory this argument
 * is ignored.
 * @src_addr_width: this is the width in bytes of the source (RX)
 * register where DMA data shall be read. If the source
 * is memory this may be ignored depending on architecture.
 * Legal values: 1, 2, 4, 8.
 * @dst_addr_width: same as src_addr_width but for destination
 * target (TX) mutatis mutandis.
 * @src_maxburst: the maximum number of words (note: words, as in
 * units of the src_addr_width member, not bytes) that can be sent
 * in one burst to the device. Typically something like half the
 * FIFO depth on I/O peripherals so you don't overflow it. This
 * may or may not be applicable on memory sources.
 * @dst_maxburst: same as src_maxburst but for destination target
 * mutatis mutandis.
 *
 * This struct is passed in as configuration data to a DMA engine
 * in order to set up a certain channel for DMA transport at runtime.
 * The DMA device/engine has to provide support for an additional
 * command in the channel config interface, DMA_SLAVE_CONFIG
 * and this struct will then be passed in as an argument to the
 * DMA engine device_control() function.
 *
 * The rationale for adding configuration information to this struct
 * is as follows: if it is likely that most DMA slave controllers in
 * the world will support the configuration option, then make it
 * generic. If not: if it is fixed so that it be sent in static from
 * the platform data, then prefer to do that. Else, if it is neither
 * fixed at runtime, nor generic enough (such as bus mastership on
 * some CPU family and whatnot) then create a custom slave config
 * struct and pass that, then make this config a member of that
 * struct, if applicable.
 */
struct dma_slave_config {
	enum dma_data_direction direction;
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	enum dma_slave_buswidth src_addr_width;
	enum dma_slave_buswidth dst_addr_width;
	u32 src_maxburst;
	u32 dst_maxburst;
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
	struct dma_chan *chan;
	dma_cookie_t (*tx_submit)(struct dma_async_tx_descriptor *tx);
	dma_async_tx_callback callback;
	void *callback_param;
#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
	struct dma_async_tx_descriptor *next;
	struct dma_async_tx_descriptor *parent;
	spinlock_t lock;
#endif
};

#ifndef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
static inline void txd_lock(struct dma_async_tx_descriptor *txd)
{
}
static inline void txd_unlock(struct dma_async_tx_descriptor *txd)
{
}
static inline void txd_chain(struct dma_async_tx_descriptor *txd, struct dma_async_tx_descriptor *next)
{
	BUG();
}
static inline void txd_clear_parent(struct dma_async_tx_descriptor *txd)
{
}
static inline void txd_clear_next(struct dma_async_tx_descriptor *txd)
{
}
static inline struct dma_async_tx_descriptor *txd_next(struct dma_async_tx_descriptor *txd)
{
	return NULL;
}
static inline struct dma_async_tx_descriptor *txd_parent(struct dma_async_tx_descriptor *txd)
{
	return NULL;
}

#else
static inline void txd_lock(struct dma_async_tx_descriptor *txd)
{
	spin_lock_bh(&txd->lock);
}
static inline void txd_unlock(struct dma_async_tx_descriptor *txd)
{
	spin_unlock_bh(&txd->lock);
}
static inline void txd_chain(struct dma_async_tx_descriptor *txd, struct dma_async_tx_descriptor *next)
{
	txd->next = next;
	next->parent = txd;
}
static inline void txd_clear_parent(struct dma_async_tx_descriptor *txd)
{
	txd->parent = NULL;
}
static inline void txd_clear_next(struct dma_async_tx_descriptor *txd)
{
	txd->next = NULL;
}
static inline struct dma_async_tx_descriptor *txd_parent(struct dma_async_tx_descriptor *txd)
{
	return txd->parent;
}
static inline struct dma_async_tx_descriptor *txd_next(struct dma_async_tx_descriptor *txd)
{
	return txd->next;
}
#endif

/**
 * struct dma_tx_state - filled in to report the status of
 * a transfer.
 * @last: last completed DMA cookie
 * @used: last issued DMA cookie (i.e. the one in progress)
 * @residue: the remaining number of bytes left to transmit
 *	on the selected transfer for states DMA_IN_PROGRESS and
 *	DMA_PAUSED if this is implemented in the driver, else 0
 */
struct dma_tx_state {
	dma_cookie_t last;
	dma_cookie_t used;
	u32 residue;
};

/**
 * struct dma_device - info on the entity supplying DMA services
 * @chancnt: how many DMA channels are supported
 * @privatecnt: how many DMA channels are requested by dma_request_channel
 * @channels: the list of struct dma_chan
 * @global_node: list_head for global dma_device_list
 * @cap_mask: one or more dma_capability flags
 * @max_xor: maximum number of xor sources, 0 if no capability
 * @max_pq: maximum number of PQ sources and PQ-continue capability
 * @copy_align: alignment shift for memcpy operations
 * @xor_align: alignment shift for xor operations
 * @pq_align: alignment shift for pq operations
 * @fill_align: alignment shift for memset operations
 * @dev_id: unique device ID
 * @dev: struct device reference for dma mapping api
 * @device_alloc_chan_resources: allocate resources and return the
 *	number of allocated descriptors
 * @device_free_chan_resources: release DMA channel's resources
 * @device_prep_dma_memcpy: prepares a memcpy operation
 * @device_prep_dma_xor: prepares a xor operation
 * @device_prep_dma_xor_val: prepares a xor validation operation
 * @device_prep_dma_pq: prepares a pq operation
 * @device_prep_dma_pq_val: prepares a pqzero_sum operation
 * @device_prep_dma_memset: prepares a memset operation
 * @device_prep_dma_interrupt: prepares an end of chain interrupt operation
 * @device_prep_slave_sg: prepares a slave dma operation
 * @device_prep_dma_cyclic: prepare a cyclic dma operation suitable for audio.
 *	The function takes a buffer of size buf_len. The callback function will
 *	be called after period_len bytes have been transferred.
 * @device_control: manipulate all pending operations on a channel, returns
 *	zero or error code
 * @device_tx_status: poll for transaction completion, the optional
 *	txstate parameter can be supplied with a pointer to get a
 *	struct with auxiliary transfer status information, otherwise the call
 *	will just return a simple status code
 * @device_issue_pending: push pending transactions to hardware
 */
struct dma_device {

	unsigned int chancnt;
	unsigned int privatecnt;
	struct list_head channels;
	struct list_head global_node;
	dma_cap_mask_t  cap_mask;
	unsigned short max_xor;
	unsigned short max_pq;
	u8 copy_align;
	u8 xor_align;
	u8 pq_align;
	u8 fill_align;
	#define DMA_HAS_PQ_CONTINUE (1 << 15)

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
	struct dma_async_tx_descriptor *(*device_prep_dma_xor_val)(
		struct dma_chan *chan, dma_addr_t *src,	unsigned int src_cnt,
		size_t len, enum sum_check_flags *result, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_pq)(
		struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf,
		size_t len, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_pq_val)(
		struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf, size_t len,
		enum sum_check_flags *pqres, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_memset)(
		struct dma_chan *chan, dma_addr_t dest, int value, size_t len,
		unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_interrupt)(
		struct dma_chan *chan, unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_sg)(
		struct dma_chan *chan,
		struct scatterlist *dst_sg, unsigned int dst_nents,
		struct scatterlist *src_sg, unsigned int src_nents,
		unsigned long flags);

	struct dma_async_tx_descriptor *(*device_prep_slave_sg)(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags);
	struct dma_async_tx_descriptor *(*device_prep_dma_cyclic)(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_data_direction direction);
	int (*device_control)(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		unsigned long arg);

	enum dma_status (*device_tx_status)(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate);
	void (*device_issue_pending)(struct dma_chan *chan);
};

static inline int dmaengine_device_control(struct dma_chan *chan,
					   enum dma_ctrl_cmd cmd,
					   unsigned long arg)
{
	return chan->device->device_control(chan, cmd, arg);
}

static inline int dmaengine_slave_config(struct dma_chan *chan,
					  struct dma_slave_config *config)
{
	return dmaengine_device_control(chan, DMA_SLAVE_CONFIG,
			(unsigned long)config);
}

static inline struct dma_async_tx_descriptor *dmaengine_prep_slave_single(
	struct dma_chan *chan, void *buf, size_t len,
	enum dma_data_direction dir, unsigned long flags)
{
	struct scatterlist sg;
	sg_init_one(&sg, buf, len);

	return chan->device->device_prep_slave_sg(chan, &sg, 1, dir, flags);
}

static inline int dmaengine_terminate_all(struct dma_chan *chan)
{
	return dmaengine_device_control(chan, DMA_TERMINATE_ALL, 0);
}

static inline int dmaengine_pause(struct dma_chan *chan)
{
	return dmaengine_device_control(chan, DMA_PAUSE, 0);
}

static inline int dmaengine_resume(struct dma_chan *chan)
{
	return dmaengine_device_control(chan, DMA_RESUME, 0);
}

static inline dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *desc)
{
	return desc->tx_submit(desc);
}

static inline bool dmaengine_check_align(u8 align, size_t off1, size_t off2, size_t len)
{
	size_t mask;

	if (!align)
		return true;
	mask = (1 << align) - 1;
	if (mask & (off1 | off2 | len))
		return false;
	return true;
}

static inline bool is_dma_copy_aligned(struct dma_device *dev, size_t off1,
				       size_t off2, size_t len)
{
	return dmaengine_check_align(dev->copy_align, off1, off2, len);
}

static inline bool is_dma_xor_aligned(struct dma_device *dev, size_t off1,
				      size_t off2, size_t len)
{
	return dmaengine_check_align(dev->xor_align, off1, off2, len);
}

static inline bool is_dma_pq_aligned(struct dma_device *dev, size_t off1,
				     size_t off2, size_t len)
{
	return dmaengine_check_align(dev->pq_align, off1, off2, len);
}

static inline bool is_dma_fill_aligned(struct dma_device *dev, size_t off1,
				       size_t off2, size_t len)
{
	return dmaengine_check_align(dev->fill_align, off1, off2, len);
}

static inline void
dma_set_maxpq(struct dma_device *dma, int maxpq, int has_pq_continue)
{
	dma->max_pq = maxpq;
	if (has_pq_continue)
		dma->max_pq |= DMA_HAS_PQ_CONTINUE;
}

static inline bool dmaf_continue(enum dma_ctrl_flags flags)
{
	return (flags & DMA_PREP_CONTINUE) == DMA_PREP_CONTINUE;
}

static inline bool dmaf_p_disabled_continue(enum dma_ctrl_flags flags)
{
	enum dma_ctrl_flags mask = DMA_PREP_CONTINUE | DMA_PREP_PQ_DISABLE_P;

	return (flags & mask) == mask;
}

static inline bool dma_dev_has_pq_continue(struct dma_device *dma)
{
	return (dma->max_pq & DMA_HAS_PQ_CONTINUE) == DMA_HAS_PQ_CONTINUE;
}

static inline unsigned short dma_dev_to_maxpq(struct dma_device *dma)
{
	return dma->max_pq & ~DMA_HAS_PQ_CONTINUE;
}

/* dma_maxpq - reduce maxpq in the face of continued operations
 * @dma - dma device with PQ capability
 * @flags - to check if DMA_PREP_CONTINUE and DMA_PREP_PQ_DISABLE_P are set
 *
 * When an engine does not support native continuation we need 3 extra
 * source slots to reuse P and Q with the following coefficients:
 * 1/ {00} * P : remove P from Q', but use it as a source for P'
 * 2/ {01} * Q : use Q to continue Q' calculation
 * 3/ {00} * Q : subtract Q from P' to cancel (2)
 *
 * In the case where P is disabled we only need 1 extra source:
 * 1/ {01} * Q : use Q to continue Q' calculation
 */
static inline int dma_maxpq(struct dma_device *dma, enum dma_ctrl_flags flags)
{
	if (dma_dev_has_pq_continue(dma) || !dmaf_continue(flags))
		return dma_dev_to_maxpq(dma);
	else if (dmaf_p_disabled_continue(flags))
		return dma_dev_to_maxpq(dma) - 1;
	else if (dmaf_continue(flags))
		return dma_dev_to_maxpq(dma) - 3;
	BUG();
}

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
#ifndef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
#define async_dma_find_channel(type) dma_find_channel(DMA_ASYNC_TX)
#else
#define async_dma_find_channel(type) dma_find_channel(type)
#endif /* CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH */
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
#endif /* CONFIG_ASYNC_TX_DMA */

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
	struct dma_tx_state state;
	enum dma_status status;

	status = chan->device->device_tx_status(chan, cookie, &state);
	if (last)
		*last = state.last;
	if (used)
		*used = state.used;
	return status;
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

static inline void
dma_set_tx_state(struct dma_tx_state *st, dma_cookie_t last, dma_cookie_t used, u32 residue)
{
	if (st) {
		st->last = last;
		st->used = used;
		st->residue = residue;
	}
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie);
#ifdef CONFIG_DMA_ENGINE
enum dma_status dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx);
void dma_issue_pending_all(void);
struct dma_chan *__dma_request_channel(dma_cap_mask_t *mask, dma_filter_fn fn, void *fn_param);
void dma_release_channel(struct dma_chan *chan);
#else
static inline enum dma_status dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	return DMA_SUCCESS;
}
static inline void dma_issue_pending_all(void)
{
}
static inline struct dma_chan *__dma_request_channel(dma_cap_mask_t *mask,
					      dma_filter_fn fn, void *fn_param)
{
	return NULL;
}
static inline void dma_release_channel(struct dma_chan *chan)
{
}
#endif

/* --- DMA device --- */

int dma_async_device_register(struct dma_device *device);
void dma_async_device_unregister(struct dma_device *device);
void dma_run_dependencies(struct dma_async_tx_descriptor *tx);
struct dma_chan *dma_find_channel(enum dma_transaction_type tx_type);
#define dma_request_channel(mask, x, y) __dma_request_channel(&(mask), x, y)

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
