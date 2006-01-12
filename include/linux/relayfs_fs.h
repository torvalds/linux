/*
 * linux/include/linux/relayfs_fs.h
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * RelayFS definitions and declarations
 */

#ifndef _LINUX_RELAYFS_FS_H
#define _LINUX_RELAYFS_FS_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kref.h>

/*
 * Tracks changes to rchan/rchan_buf structs
 */
#define RELAYFS_CHANNEL_VERSION		6

/*
 * Per-cpu relay channel buffer
 */
struct rchan_buf
{
	void *start;			/* start of channel buffer */
	void *data;			/* start of current sub-buffer */
	size_t offset;			/* current offset into sub-buffer */
	size_t subbufs_produced;	/* count of sub-buffers produced */
	size_t subbufs_consumed;	/* count of sub-buffers consumed */
	struct rchan *chan;		/* associated channel */
	wait_queue_head_t read_wait;	/* reader wait queue */
	struct work_struct wake_readers; /* reader wake-up work struct */
	struct dentry *dentry;		/* channel file dentry */
	struct kref kref;		/* channel buffer refcount */
	struct page **page_array;	/* array of current buffer pages */
	unsigned int page_count;	/* number of current buffer pages */
	unsigned int finalized;		/* buffer has been finalized */
	size_t *padding;		/* padding counts per sub-buffer */
	size_t prev_padding;		/* temporary variable */
	size_t bytes_consumed;		/* bytes consumed in cur read subbuf */
	unsigned int cpu;		/* this buf's cpu */
} ____cacheline_aligned;

/*
 * Relay channel data structure
 */
struct rchan
{
	u32 version;			/* the version of this struct */
	size_t subbuf_size;		/* sub-buffer size */
	size_t n_subbufs;		/* number of sub-buffers per buffer */
	size_t alloc_size;		/* total buffer size allocated */
	struct rchan_callbacks *cb;	/* client callbacks */
	struct kref kref;		/* channel refcount */
	void *private_data;		/* for user-defined data */
	size_t last_toobig;		/* tried to log event > subbuf size */
	struct rchan_buf *buf[NR_CPUS]; /* per-cpu channel buffers */
};

/*
 * Relay channel client callbacks
 */
struct rchan_callbacks
{
	/*
	 * subbuf_start - called on buffer-switch to a new sub-buffer
	 * @buf: the channel buffer containing the new sub-buffer
	 * @subbuf: the start of the new sub-buffer
	 * @prev_subbuf: the start of the previous sub-buffer
	 * @prev_padding: unused space at the end of previous sub-buffer
	 *
	 * The client should return 1 to continue logging, 0 to stop
	 * logging.
	 *
	 * NOTE: subbuf_start will also be invoked when the buffer is
	 *       created, so that the first sub-buffer can be initialized
	 *       if necessary.  In this case, prev_subbuf will be NULL.
	 *
	 * NOTE: the client can reserve bytes at the beginning of the new
	 *       sub-buffer by calling subbuf_start_reserve() in this callback.
	 */
	int (*subbuf_start) (struct rchan_buf *buf,
			     void *subbuf,
			     void *prev_subbuf,
			     size_t prev_padding);

	/*
	 * buf_mapped - relayfs buffer mmap notification
	 * @buf: the channel buffer
	 * @filp: relayfs file pointer
	 *
	 * Called when a relayfs file is successfully mmapped
	 */
        void (*buf_mapped)(struct rchan_buf *buf,
			   struct file *filp);

	/*
	 * buf_unmapped - relayfs buffer unmap notification
	 * @buf: the channel buffer
	 * @filp: relayfs file pointer
	 *
	 * Called when a relayfs file is successfully unmapped
	 */
        void (*buf_unmapped)(struct rchan_buf *buf,
			     struct file *filp);
	/*
	 * create_buf_file - create file to represent a relayfs channel buffer
	 * @filename: the name of the file to create
	 * @parent: the parent of the file to create
	 * @mode: the mode of the file to create
	 * @buf: the channel buffer
	 * @is_global: outparam - set non-zero if the buffer should be global
	 *
	 * Called during relay_open(), once for each per-cpu buffer,
	 * to allow the client to create a file to be used to
	 * represent the corresponding channel buffer.  If the file is
	 * created outside of relayfs, the parent must also exist in
	 * that filesystem.
	 *
	 * The callback should return the dentry of the file created
	 * to represent the relay buffer.
	 *
	 * Setting the is_global outparam to a non-zero value will
	 * cause relay_open() to create a single global buffer rather
	 * than the default set of per-cpu buffers.
	 *
	 * See Documentation/filesystems/relayfs.txt for more info.
	 */
	struct dentry *(*create_buf_file)(const char *filename,
					  struct dentry *parent,
					  int mode,
					  struct rchan_buf *buf,
					  int *is_global);

	/*
	 * remove_buf_file - remove file representing a relayfs channel buffer
	 * @dentry: the dentry of the file to remove
	 *
	 * Called during relay_close(), once for each per-cpu buffer,
	 * to allow the client to remove a file used to represent a
	 * channel buffer.
	 *
	 * The callback should return 0 if successful, negative if not.
	 */
	int (*remove_buf_file)(struct dentry *dentry);
};

/*
 * relayfs kernel API, fs/relayfs/relay.c
 */

struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 size_t subbuf_size,
			 size_t n_subbufs,
			 struct rchan_callbacks *cb);
extern void relay_close(struct rchan *chan);
extern void relay_flush(struct rchan *chan);
extern void relay_subbufs_consumed(struct rchan *chan,
				   unsigned int cpu,
				   size_t consumed);
extern void relay_reset(struct rchan *chan);
extern int relay_buf_full(struct rchan_buf *buf);

extern size_t relay_switch_subbuf(struct rchan_buf *buf,
				  size_t length);
extern struct dentry *relayfs_create_dir(const char *name,
					 struct dentry *parent);
extern int relayfs_remove_dir(struct dentry *dentry);
extern struct dentry *relayfs_create_file(const char *name,
					  struct dentry *parent,
					  int mode,
					  struct file_operations *fops,
					  void *data);
extern int relayfs_remove_file(struct dentry *dentry);

/**
 *	relay_write - write data into the channel
 *	@chan: relay channel
 *	@data: data to be written
 *	@length: number of bytes to write
 *
 *	Writes data into the current cpu's channel buffer.
 *
 *	Protects the buffer by disabling interrupts.  Use this
 *	if you might be logging from interrupt context.  Try
 *	__relay_write() if you know you	won't be logging from
 *	interrupt context.
 */
static inline void relay_write(struct rchan *chan,
			       const void *data,
			       size_t length)
{
	unsigned long flags;
	struct rchan_buf *buf;

	local_irq_save(flags);
	buf = chan->buf[smp_processor_id()];
	if (unlikely(buf->offset + length > chan->subbuf_size))
		length = relay_switch_subbuf(buf, length);
	memcpy(buf->data + buf->offset, data, length);
	buf->offset += length;
	local_irq_restore(flags);
}

/**
 *	__relay_write - write data into the channel
 *	@chan: relay channel
 *	@data: data to be written
 *	@length: number of bytes to write
 *
 *	Writes data into the current cpu's channel buffer.
 *
 *	Protects the buffer by disabling preemption.  Use
 *	relay_write() if you might be logging from interrupt
 *	context.
 */
static inline void __relay_write(struct rchan *chan,
				 const void *data,
				 size_t length)
{
	struct rchan_buf *buf;

	buf = chan->buf[get_cpu()];
	if (unlikely(buf->offset + length > buf->chan->subbuf_size))
		length = relay_switch_subbuf(buf, length);
	memcpy(buf->data + buf->offset, data, length);
	buf->offset += length;
	put_cpu();
}

/**
 *	relay_reserve - reserve slot in channel buffer
 *	@chan: relay channel
 *	@length: number of bytes to reserve
 *
 *	Returns pointer to reserved slot, NULL if full.
 *
 *	Reserves a slot in the current cpu's channel buffer.
 *	Does not protect the buffer at all - caller must provide
 *	appropriate synchronization.
 */
static inline void *relay_reserve(struct rchan *chan, size_t length)
{
	void *reserved;
	struct rchan_buf *buf = chan->buf[smp_processor_id()];

	if (unlikely(buf->offset + length > buf->chan->subbuf_size)) {
		length = relay_switch_subbuf(buf, length);
		if (!length)
			return NULL;
	}
	reserved = buf->data + buf->offset;
	buf->offset += length;

	return reserved;
}

/**
 *	subbuf_start_reserve - reserve bytes at the start of a sub-buffer
 *	@buf: relay channel buffer
 *	@length: number of bytes to reserve
 *
 *	Helper function used to reserve bytes at the beginning of
 *	a sub-buffer in the subbuf_start() callback.
 */
static inline void subbuf_start_reserve(struct rchan_buf *buf,
					size_t length)
{
	BUG_ON(length >= buf->chan->subbuf_size - 1);
	buf->offset = length;
}

/*
 * exported relay file operations, fs/relayfs/inode.c
 */
extern struct file_operations relay_file_operations;

#endif /* _LINUX_RELAYFS_FS_H */

