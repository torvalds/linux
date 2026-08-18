/* SPDX-License-Identifier: GPL-2.0
 *
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
 */
#ifndef _FS_FUSE_DEV_I_H
#define _FS_FUSE_DEV_I_H

#include <linux/fuse.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/fs.h>

/* Ordinary requests have even IDs, while interrupts IDs are odd */
#define FUSE_INT_REQ_BIT (1ULL << 0)
#define FUSE_REQ_ID_STEP (1ULL << 1)

struct fuse_arg;
struct fuse_args;
struct fuse_pqueue;
struct fuse_iqueue;

/**
 * enum fuse_req_flag - Request flags
 *
 * @FR_ISREPLY:		set if the request has reply
 * @FR_FORCE:		force sending of the request even if interrupted
 * @FR_BACKGROUND:	request is sent in the background
 * @FR_WAITING:		request is counted as "waiting"
 * @FR_ABORTED:		the request was aborted
 * @FR_INTERRUPTED:	the request has been interrupted
 * @FR_LOCKED:		data is being copied to/from the request
 * @FR_PENDING:		request is not yet in userspace
 * @FR_SENT:		request is in userspace, waiting for an answer
 * @FR_FINISHED:	request is finished
 * @FR_PRIVATE:		request is on private list
 * @FR_ASYNC:		request is asynchronous
 * @FR_URING:		request is handled through fuse-io-uring
 */
enum fuse_req_flag {
	FR_ISREPLY,
	FR_FORCE,
	FR_BACKGROUND,
	FR_WAITING,
	FR_ABORTED,
	FR_INTERRUPTED,
	FR_LOCKED,
	FR_PENDING,
	FR_SENT,
	FR_FINISHED,
	FR_PRIVATE,
	FR_ASYNC,
	FR_URING,
};

/**
 * struct fuse_req - A request to the client
 *
 * .waitq.lock protects the following fields:
 *   - FR_ABORTED
 *   - FR_LOCKED (may also be modified under fpq->lock, tested under both)
 */
struct fuse_req {
	/**
	 * @list: This can be on either pending processing or io lists in
	 * fuse_conn
	 */
	struct list_head list;

	/** @intr_entry: Entry on the interrupts list  */
	struct list_head intr_entry;

	/** @args: Input/output arguments */
	struct fuse_args *args;

	/** @count: refcount */
	refcount_t count;

	/** @flags: Request flags, updated with test/set/clear_bit() */
	unsigned long flags;

	/** @in: The request input header */
	struct {
		/** @in.h: The request input header */
		struct fuse_in_header h;
	} in;

	/** @out: The request output header */
	struct {
		/** @out.h: The request output header */
		struct fuse_out_header h;
	} out;

	/** @waitq: Used to wake up the task waiting for completion of request */
	wait_queue_head_t waitq;

#if IS_ENABLED(CONFIG_VIRTIO_FS)
	/**
	 * @argbuf: virtio-fs's physically contiguous buffer for in and out
	 * args
	 */
	void *argbuf;
#endif

	/** @chan: fuse_chan this request belongs to */
	struct fuse_chan *chan;

#ifdef CONFIG_FUSE_IO_URING
	void *ring_entry;
	void *ring_queue;
#endif
	/** @create_time: When (in jiffies) the request was created */
	unsigned long create_time;
};

/* One forget request */
struct fuse_forget_link {
	struct fuse_forget_one forget_one;
	struct fuse_forget_link *next;
};

/**
 * struct fuse_iqueue_ops - Input queue callbacks
 *
 * Input queue signalling is device-specific.  For example, the /dev/fuse file
 * uses fiq->waitq and fasync to wake processes that are waiting on queue
 * readiness.  These callbacks allow other device types to respond to input
 * queue activity.
 */
struct fuse_iqueue_ops {
	/**
	 * @send_forget: Send one forget
	 */
	void (*send_forget)(struct fuse_iqueue *fiq, struct fuse_forget_link *link);

	/**
	 * @send_interrupt: Send interrupt for request
	 */
	void (*send_interrupt)(struct fuse_iqueue *fiq, struct fuse_req *req);

	/**
	 * @send_req: Send one request
	 */
	void (*send_req)(struct fuse_iqueue *fiq, struct fuse_req *req);

	/**
	 * @release: Clean up when fuse_iqueue is destroyed
	 */
	void (*release)(struct fuse_iqueue *fiq);
};

struct fuse_iqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accesses to members of this structure */
	spinlock_t lock;

	/** Readers of the connection are waiting on this */
	wait_queue_head_t waitq;

	/** The next unique request id */
	u64 reqctr;

	/** The list of pending requests */
	struct list_head pending;

	/** Pending interrupts */
	struct list_head interrupts;

	/** Queue of pending forgets */
	struct fuse_forget_link forget_list_head;
	struct fuse_forget_link *forget_list_tail;

	/** Batching of FORGET requests (positive indicates FORGET batch) */
	int forget_batch;

	/** O_ASYNC requests */
	struct fasync_struct *fasync;

	/** Device-specific callbacks */
	const struct fuse_iqueue_ops *ops;

	/** Device-specific state */
	void *priv;
};

struct fuse_chan {
	/** Lock protecting:
	    - devices
	    - connected
	    - ring
	    - ring->queues[qid]
	 */
	spinlock_t lock;

	/* back pointer: fc->chan->conn == fc */
	struct fuse_conn *conn;

	/** Input queue */
	struct fuse_iqueue iq;

	/** List of device instances belonging to this connection */
	struct list_head devices;

	/** Maximum number of outstanding background requests */
	unsigned max_background;

	/** Number of requests currently in the background */
	unsigned num_background;

	/** Number of background requests currently queued for userspace */
	unsigned active_background;

	/** The list of background requests set aside for later queuing */
	struct list_head bg_queue;

	/** Protects: max_background, num_background, active_background, bg_queue, blocked */
	spinlock_t bg_lock;

	/** Flag indicating that INIT reply has been received. Allocating
	 * any fuse request will be suspended until the flag is set */
	int initialized;

	/** Flag indicating if connection is blocked.  This will be
	    the case before the INIT reply is received, and if there
	    are too many outstading backgrounds requests */
	int blocked;

	/** waitq for blocked connection */
	wait_queue_head_t blocked_waitq;

	/** Connection established, cleared on umount, connection
	    abort and device release */
	unsigned connected;

	/** The number of requests waiting for completion */
	atomic_t num_waiting;

	/** Is interrupt not implemented by fs? */
	bool no_interrupt;

	/* Use io_uring for communication */
	unsigned int io_uring;

	/* Negotiated minor version */
	unsigned int minor;

	/* Maximum write size */
	unsigned int max_write;

	/* Maximum number of pages that can be used in a single request */
	unsigned int max_pages;

	/* Before being installed into fud, contains the preallocated pq array*/
	struct list_head *pq_prealloc;

	/** Connection aborted via sysfs, respond with ECONNABORTED on device I/O */
	bool abort_with_err;

#ifdef CONFIG_FUSE_IO_URING
	/**  uring connection information*/
	struct fuse_ring *ring;
#endif

	/** Only used if the connection opts into request timeouts */
	struct {
		/* Worker for checking if any requests have timed out */
		struct delayed_work work;

		/* Request timeout (in jiffies). 0 = no timeout */
		unsigned int req_timeout;
	} timeout;
};

#define FUSE_PQ_HASH_BITS 8
#define FUSE_PQ_HASH_SIZE (1 << FUSE_PQ_HASH_BITS)

struct fuse_pqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** Hash table of requests being processed */
	struct list_head *processing;

	/** The list of requests under I/O */
	struct list_head io;
};

/**
 * struct fuse_dev - Fuse device instance
 */
struct fuse_dev {
	/** @ref: Reference count of this object */
	refcount_t ref;

	/** @sync_init: Issue FUSE_INIT synchronously */
	bool sync_init;

	/** @chan: Fuse channel for this device */
	struct fuse_chan *chan;

	/** @pq: Processing queue */
	struct fuse_pqueue pq;

	/** @entry: list entry on fch->devices */
	struct list_head entry;
};

struct fuse_copy_state {
	struct fuse_req *req;
	struct iov_iter *iter;
	struct pipe_buffer *pipebufs;
	struct pipe_buffer *currbuf;
	struct pipe_inode_info *pipe;
	unsigned long nr_segs;
	struct page *pg;
	unsigned int len;
	unsigned int offset;
	bool write:1;
	bool move_folios:1;
	bool is_uring:1;
	struct {
		unsigned int copied_sz; /* copied size into the user buffer */
	} ring;
};

/* fud->chan gets assigned to this value when /dev/fuse is closed */
#define FUSE_DEV_CHAN_DISCONNECTED ((struct fuse_chan *) 1)

/*
 * Lockless access is OK, because fud->chan is set once during mount and is valid
 * until the file is released.
 *
 * fud->chan is set to FUSE_DEV_CHAN_DISCONNECTED only after the containing file is
 * released, so result is safe to dereference in most cases.  Exceptions are:
 * fuse_dev_put() and fuse_fill_super_common().
 */
static inline struct fuse_chan *fuse_dev_chan_get(struct fuse_dev *fud)
{
	/* Pairs with xchg() in fuse_dev_install() */
	return smp_load_acquire(&fud->chan);
}

static inline struct fuse_dev *fuse_file_to_fud(struct file *file)
{
	return file->private_data;
}

static inline struct fuse_dev *__fuse_get_dev(struct file *file)
{
	struct fuse_dev *fud = fuse_file_to_fud(file);

	if (!fuse_dev_chan_get(fud))
		return NULL;

	return fud;
}

void fuse_iqueue_init(struct fuse_iqueue *fiq, const struct fuse_iqueue_ops *ops, void *priv);

struct fuse_dev *fuse_get_dev(struct file *file);

unsigned int fuse_req_hash(u64 unique);
struct fuse_req *fuse_request_find(struct fuse_pqueue *fpq, u64 unique);

void fuse_dev_end_requests(struct list_head *head);
void fuse_request_bg_finish(struct fuse_chan *fch, struct fuse_req *req);

void fuse_copy_init(struct fuse_copy_state *cs, bool write,
			   struct iov_iter *iter);
/*
 * Return the number of bytes in an arguments list
 */
unsigned int fuse_len_args(unsigned int numargs, struct fuse_arg *args);

int fuse_copy_args(struct fuse_copy_state *cs, unsigned int numargs,
		   unsigned int argpages, struct fuse_arg *args,
		   int zeroing);
int fuse_copy_out_args(struct fuse_copy_state *cs, struct fuse_args *args,
		       unsigned int nbytes);
void fuse_dev_queue_forget(struct fuse_iqueue *fiq,
			   struct fuse_forget_link *forget);
void fuse_dev_queue_interrupt(struct fuse_iqueue *fiq, struct fuse_req *req);
bool fuse_remove_pending_req(struct fuse_req *req, spinlock_t *lock);

bool fuse_request_expired(struct fuse_chan *fch, struct list_head *list);

/*
 * Assign a unique id to a fuse request
 */
void fuse_request_assign_unique(struct fuse_iqueue *fiq, struct fuse_req *req);

/*
 * Get the next unique ID for a request
 */
u64 fuse_get_unique(struct fuse_iqueue *fiq);

struct fuse_dev *fuse_dev_alloc_install(struct fuse_chan *fch);
struct fuse_dev *fuse_dev_alloc(void);

int fuse_dev_release(struct inode *inode, struct file *file);

struct list_head *fuse_pqueue_alloc(void);

/*
 * Initialize the fuse processing queue
 */
void fuse_pqueue_init(struct fuse_pqueue *fpq);

/*
 * End a finished request
 */
void fuse_request_end(struct fuse_req *req);

#endif

