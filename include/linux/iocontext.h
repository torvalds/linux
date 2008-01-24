#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>

/*
 * This is the per-process anticipatory I/O scheduler state.
 */
struct as_io_context {
	spinlock_t lock;

	void (*dtor)(struct as_io_context *aic); /* destructor */
	void (*exit)(struct as_io_context *aic); /* called on task exit */

	unsigned long state;
	atomic_t nr_queued; /* queued reads & sync writes */
	atomic_t nr_dispatched; /* number of requests gone to the drivers */

	/* IO History tracking */
	/* Thinktime */
	unsigned long last_end_request;
	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;
	/* Layout pattern */
	unsigned int seek_samples;
	sector_t last_request_pos;
	u64 seek_total;
	sector_t seek_mean;
};

struct cfq_queue;
struct cfq_io_context {
	void *key;
	unsigned long dead_key;

	struct cfq_queue *cfqq[2];

	struct io_context *ioc;

	unsigned long last_end_request;
	sector_t last_request_pos;

	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;

	unsigned int seek_samples;
	u64 seek_total;
	sector_t seek_mean;

	struct list_head queue_list;

	void (*dtor)(struct io_context *); /* destructor */
	void (*exit)(struct io_context *); /* called on task exit */
};

/*
 * I/O subsystem state of the associated processes.  It is refcounted
 * and kmalloc'ed. These could be shared between processes.
 */
struct io_context {
	atomic_t refcount;
	atomic_t nr_tasks;

	/* all the fields below are protected by this lock */
	spinlock_t lock;

	unsigned short ioprio;
	unsigned short ioprio_changed;

	/*
	 * For request batching
	 */
	unsigned long last_waited; /* Time last woken after wait for request */
	int nr_batch_requests;     /* Number of requests left in the batch */

	struct as_io_context *aic;
	struct radix_tree_root radix_root;
	void *ioc_data;
};

static inline struct io_context *ioc_task_link(struct io_context *ioc)
{
	/*
	 * if ref count is zero, don't allow sharing (ioc is going away, it's
	 * a race).
	 */
	if (ioc && atomic_inc_not_zero(&ioc->refcount))
		return ioc;

	return NULL;
}

#endif
