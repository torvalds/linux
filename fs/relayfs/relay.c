/*
 * Public API and common code for RelayFS.
 *
 * See Documentation/filesystems/relayfs.txt for an overview of relayfs.
 *
 * Copyright (C) 2002-2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour (karim@opersys.com)
 *
 * This file is released under the GPL.
 */

#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/relayfs_fs.h>
#include "relay.h"
#include "buffers.h"

/**
 *	relay_buf_empty - boolean, is the channel buffer empty?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is empty, 0 otherwise.
 */
int relay_buf_empty(struct rchan_buf *buf)
{
	return (buf->subbufs_produced - buf->subbufs_consumed) ? 0 : 1;
}

/**
 *	relay_buf_full - boolean, is the channel buffer full?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is full, 0 otherwise.
 */
int relay_buf_full(struct rchan_buf *buf)
{
	size_t ready = buf->subbufs_produced - buf->subbufs_consumed;
	return (ready >= buf->chan->n_subbufs) ? 1 : 0;
}

/*
 * High-level relayfs kernel API and associated functions.
 */

/*
 * rchan_callback implementations defining default channel behavior.  Used
 * in place of corresponding NULL values in client callback struct.
 */

/*
 * subbuf_start() default callback.  Does nothing.
 */
static int subbuf_start_default_callback (struct rchan_buf *buf,
					  void *subbuf,
					  void *prev_subbuf,
					  size_t prev_padding)
{
	if (relay_buf_full(buf))
		return 0;

	return 1;
}

/*
 * buf_mapped() default callback.  Does nothing.
 */
static void buf_mapped_default_callback(struct rchan_buf *buf,
					struct file *filp)
{
}

/*
 * buf_unmapped() default callback.  Does nothing.
 */
static void buf_unmapped_default_callback(struct rchan_buf *buf,
					  struct file *filp)
{
}

/*
 * create_buf_file_create() default callback.  Creates file to represent buf.
 */
static struct dentry *create_buf_file_default_callback(const char *filename,
						       struct dentry *parent,
						       int mode,
						       struct rchan_buf *buf,
						       int *is_global)
{
	return relayfs_create_file(filename, parent, mode,
				   &relay_file_operations, buf);
}

/*
 * remove_buf_file() default callback.  Removes file representing relay buffer.
 */
static int remove_buf_file_default_callback(struct dentry *dentry)
{
	return relayfs_remove(dentry);
}

/* relay channel default callbacks */
static struct rchan_callbacks default_channel_callbacks = {
	.subbuf_start = subbuf_start_default_callback,
	.buf_mapped = buf_mapped_default_callback,
	.buf_unmapped = buf_unmapped_default_callback,
	.create_buf_file = create_buf_file_default_callback,
	.remove_buf_file = remove_buf_file_default_callback,
};

/**
 *	wakeup_readers - wake up readers waiting on a channel
 *	@private: the channel buffer
 *
 *	This is the work function used to defer reader waking.  The
 *	reason waking is deferred is that calling directly from write
 *	causes problems if you're writing from say the scheduler.
 */
static void wakeup_readers(void *private)
{
	struct rchan_buf *buf = private;
	wake_up_interruptible(&buf->read_wait);
}

/**
 *	__relay_reset - reset a channel buffer
 *	@buf: the channel buffer
 *	@init: 1 if this is a first-time initialization
 *
 *	See relay_reset for description of effect.
 */
static inline void __relay_reset(struct rchan_buf *buf, unsigned int init)
{
	size_t i;

	if (init) {
		init_waitqueue_head(&buf->read_wait);
		kref_init(&buf->kref);
		INIT_WORK(&buf->wake_readers, NULL, NULL);
	} else {
		cancel_delayed_work(&buf->wake_readers);
		flush_scheduled_work();
	}

	buf->subbufs_produced = 0;
	buf->subbufs_consumed = 0;
	buf->bytes_consumed = 0;
	buf->finalized = 0;
	buf->data = buf->start;
	buf->offset = 0;

	for (i = 0; i < buf->chan->n_subbufs; i++)
		buf->padding[i] = 0;

	buf->chan->cb->subbuf_start(buf, buf->data, NULL, 0);
}

/**
 *	relay_reset - reset the channel
 *	@chan: the channel
 *
 *	This has the effect of erasing all data from all channel buffers
 *	and restarting the channel in its initial state.  The buffers
 *	are not freed, so any mappings are still in effect.
 *
 *	NOTE: Care should be taken that the channel isn't actually
 *	being used by anything when this call is made.
 */
void relay_reset(struct rchan *chan)
{
	unsigned int i;
	struct rchan_buf *prev = NULL;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i] || chan->buf[i] == prev)
			break;
		__relay_reset(chan->buf[i], 0);
		prev = chan->buf[i];
	}
}

/**
 *	relay_open_buf - create a new channel buffer in relayfs
 *
 *	Internal - used by relay_open().
 */
static struct rchan_buf *relay_open_buf(struct rchan *chan,
					const char *filename,
					struct dentry *parent,
					int *is_global)
{
	struct rchan_buf *buf;
	struct dentry *dentry;

	if (*is_global)
		return chan->buf[0];

 	buf = relay_create_buf(chan);
 	if (!buf)
 		return NULL;

	/* Create file in fs */
 	dentry = chan->cb->create_buf_file(filename, parent, S_IRUSR,
 					   buf, is_global);
 	if (!dentry) {
 		relay_destroy_buf(buf);
		return NULL;
 	}

	buf->dentry = dentry;
	__relay_reset(buf, 1);

	return buf;
}

/**
 *	relay_close_buf - close a channel buffer
 *	@buf: channel buffer
 *
 *	Marks the buffer finalized and restores the default callbacks.
 *	The channel buffer and channel buffer data structure are then freed
 *	automatically when the last reference is given up.
 */
static inline void relay_close_buf(struct rchan_buf *buf)
{
	buf->finalized = 1;
	buf->chan->cb = &default_channel_callbacks;
	cancel_delayed_work(&buf->wake_readers);
	flush_scheduled_work();
	kref_put(&buf->kref, relay_remove_buf);
}

static inline void setup_callbacks(struct rchan *chan,
				   struct rchan_callbacks *cb)
{
	if (!cb) {
		chan->cb = &default_channel_callbacks;
		return;
	}

	if (!cb->subbuf_start)
		cb->subbuf_start = subbuf_start_default_callback;
	if (!cb->buf_mapped)
		cb->buf_mapped = buf_mapped_default_callback;
	if (!cb->buf_unmapped)
		cb->buf_unmapped = buf_unmapped_default_callback;
	if (!cb->create_buf_file)
		cb->create_buf_file = create_buf_file_default_callback;
	if (!cb->remove_buf_file)
		cb->remove_buf_file = remove_buf_file_default_callback;
	chan->cb = cb;
}

/**
 *	relay_open - create a new relayfs channel
 *	@base_filename: base name of files to create
 *	@parent: dentry of parent directory, NULL for root directory
 *	@subbuf_size: size of sub-buffers
 *	@n_subbufs: number of sub-buffers
 *	@cb: client callback functions
 *
 *	Returns channel pointer if successful, NULL otherwise.
 *
 *	Creates a channel buffer for each cpu using the sizes and
 *	attributes specified.  The created channel buffer files
 *	will be named base_filename0...base_filenameN-1.  File
 *	permissions will be S_IRUSR.
 */
struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 size_t subbuf_size,
			 size_t n_subbufs,
			 struct rchan_callbacks *cb)
{
	unsigned int i;
	struct rchan *chan;
	char *tmpname;
	int is_global = 0;

	if (!base_filename)
		return NULL;

	if (!(subbuf_size && n_subbufs))
		return NULL;

	chan = kcalloc(1, sizeof(struct rchan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->version = RELAYFS_CHANNEL_VERSION;
	chan->n_subbufs = n_subbufs;
	chan->subbuf_size = subbuf_size;
	chan->alloc_size = FIX_SIZE(subbuf_size * n_subbufs);
	setup_callbacks(chan, cb);
	kref_init(&chan->kref);

	tmpname = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!tmpname)
		goto free_chan;

	for_each_online_cpu(i) {
		sprintf(tmpname, "%s%d", base_filename, i);
		chan->buf[i] = relay_open_buf(chan, tmpname, parent,
					      &is_global);
		chan->buf[i]->cpu = i;
		if (!chan->buf[i])
			goto free_bufs;
	}

	kfree(tmpname);
	return chan;

free_bufs:
	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i])
			break;
		relay_close_buf(chan->buf[i]);
		if (is_global)
			break;
	}
	kfree(tmpname);

free_chan:
	kref_put(&chan->kref, relay_destroy_channel);
	return NULL;
}

/**
 *	relay_switch_subbuf - switch to a new sub-buffer
 *	@buf: channel buffer
 *	@length: size of current event
 *
 *	Returns either the length passed in or 0 if full.

 *	Performs sub-buffer-switch tasks such as invoking callbacks,
 *	updating padding counts, waking up readers, etc.
 */
size_t relay_switch_subbuf(struct rchan_buf *buf, size_t length)
{
	void *old, *new;
	size_t old_subbuf, new_subbuf;

	if (unlikely(length > buf->chan->subbuf_size))
		goto toobig;

	if (buf->offset != buf->chan->subbuf_size + 1) {
		buf->prev_padding = buf->chan->subbuf_size - buf->offset;
		old_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
		buf->padding[old_subbuf] = buf->prev_padding;
		buf->subbufs_produced++;
		if (waitqueue_active(&buf->read_wait)) {
			PREPARE_WORK(&buf->wake_readers, wakeup_readers, buf);
			schedule_delayed_work(&buf->wake_readers, 1);
		}
	}

	old = buf->data;
	new_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
	new = buf->start + new_subbuf * buf->chan->subbuf_size;
	buf->offset = 0;
	if (!buf->chan->cb->subbuf_start(buf, new, old, buf->prev_padding)) {
		buf->offset = buf->chan->subbuf_size + 1;
		return 0;
	}
	buf->data = new;
	buf->padding[new_subbuf] = 0;

	if (unlikely(length + buf->offset > buf->chan->subbuf_size))
		goto toobig;

	return length;

toobig:
	buf->chan->last_toobig = length;
	return 0;
}

/**
 *	relay_subbufs_consumed - update the buffer's sub-buffers-consumed count
 *	@chan: the channel
 *	@cpu: the cpu associated with the channel buffer to update
 *	@subbufs_consumed: number of sub-buffers to add to current buf's count
 *
 *	Adds to the channel buffer's consumed sub-buffer count.
 *	subbufs_consumed should be the number of sub-buffers newly consumed,
 *	not the total consumed.
 *
 *	NOTE: kernel clients don't need to call this function if the channel
 *	mode is 'overwrite'.
 */
void relay_subbufs_consumed(struct rchan *chan,
			    unsigned int cpu,
			    size_t subbufs_consumed)
{
	struct rchan_buf *buf;

	if (!chan)
		return;

	if (cpu >= NR_CPUS || !chan->buf[cpu])
		return;

	buf = chan->buf[cpu];
	buf->subbufs_consumed += subbufs_consumed;
	if (buf->subbufs_consumed > buf->subbufs_produced)
		buf->subbufs_consumed = buf->subbufs_produced;
}

/**
 *	relay_destroy_channel - free the channel struct
 *
 *	Should only be called from kref_put().
 */
void relay_destroy_channel(struct kref *kref)
{
	struct rchan *chan = container_of(kref, struct rchan, kref);
	kfree(chan);
}

/**
 *	relay_close - close the channel
 *	@chan: the channel
 *
 *	Closes all channel buffers and frees the channel.
 */
void relay_close(struct rchan *chan)
{
	unsigned int i;
	struct rchan_buf *prev = NULL;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i] || chan->buf[i] == prev)
			break;
		relay_close_buf(chan->buf[i]);
		prev = chan->buf[i];
	}

	if (chan->last_toobig)
		printk(KERN_WARNING "relayfs: one or more items not logged "
		       "[item size (%Zd) > sub-buffer size (%Zd)]\n",
		       chan->last_toobig, chan->subbuf_size);

	kref_put(&chan->kref, relay_destroy_channel);
}

/**
 *	relay_flush - close the channel
 *	@chan: the channel
 *
 *	Flushes all channel buffers i.e. forces buffer switch.
 */
void relay_flush(struct rchan *chan)
{
	unsigned int i;
	struct rchan_buf *prev = NULL;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i] || chan->buf[i] == prev)
			break;
		relay_switch_subbuf(chan->buf[i], 0);
		prev = chan->buf[i];
	}
}

EXPORT_SYMBOL_GPL(relay_open);
EXPORT_SYMBOL_GPL(relay_close);
EXPORT_SYMBOL_GPL(relay_flush);
EXPORT_SYMBOL_GPL(relay_reset);
EXPORT_SYMBOL_GPL(relay_subbufs_consumed);
EXPORT_SYMBOL_GPL(relay_switch_subbuf);
EXPORT_SYMBOL_GPL(relay_buf_full);
