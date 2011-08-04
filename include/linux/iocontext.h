#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>
#include <linux/rcupdate.h>

struct cfq_queue;
struct cfq_ttime {
	unsigned long last_end_request;

	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;
};

struct cfq_io_context {
	void *key;

	struct cfq_queue *cfqq[2];

	struct io_context *ioc;

	struct cfq_ttime ttime;

	struct list_head queue_list;
	struct hlist_node cic_list;

	void (*dtor)(struct io_context *); /* destructor */
	void (*exit)(struct io_context *); /* called on task exit */

	struct rcu_head rcu_head;
};

/*
 * I/O subsystem state of the associated processes.  It is refcounted
 * and kmalloc'ed. These could be shared between processes.
 */
struct io_context {
	atomic_long_t refcount;
	atomic_t nr_tasks;

	/* all the fields below are protected by this lock */
	spinlock_t lock;

	unsigned short ioprio;
	unsigned short ioprio_changed;

#if defined(CONFIG_BLK_CGROUP) || defined(CONFIG_BLK_CGROUP_MODULE)
	unsigned short cgroup_changed;
#endif

	/*
	 * For request batching
	 */
	int nr_batch_requests;     /* Number of requests left in the batch */
	unsigned long last_waited; /* Time last woken after wait for request */

	struct radix_tree_root radix_root;
	struct hlist_head cic_list;
	void __rcu *ioc_data;
};

static inline struct io_context *ioc_task_link(struct io_context *ioc)
{
	/*
	 * if ref count is zero, don't allow sharing (ioc is going away, it's
	 * a race).
	 */
	if (ioc && atomic_long_inc_not_zero(&ioc->refcount)) {
		atomic_inc(&ioc->nr_tasks);
		return ioc;
	}

	return NULL;
}

struct task_struct;
#ifdef CONFIG_BLOCK
int put_io_context(struct io_context *ioc);
void exit_io_context(struct task_struct *task);
struct io_context *get_io_context(gfp_t gfp_flags, int node);
struct io_context *alloc_io_context(gfp_t gfp_flags, int node);
#else
static inline void exit_io_context(struct task_struct *task)
{
}

struct io_context;
static inline int put_io_context(struct io_context *ioc)
{
	return 1;
}
#endif

#endif
