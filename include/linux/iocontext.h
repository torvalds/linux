/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IOCONTEXT_H
#define IOCONTEXT_H

#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

enum {
	ICQ_EXITED		= 1 << 2,
	ICQ_DESTROYED		= 1 << 3,
};

/*
 * An io_cq (icq) is association between an io_context (ioc) and a
 * request_queue (q).  This is used by elevators which need to track
 * information per ioc - q pair.
 *
 * Elevator can request use of icq by setting elevator_type->icq_size and
 * ->icq_align.  Both size and align must be larger than that of struct
 * io_cq and elevator can use the tail area for private information.  The
 * recommended way to do this is defining a struct which contains io_cq as
 * the first member followed by private members and using its size and
 * align.  For example,
 *
 *	struct snail_io_cq {
 *		struct io_cq	icq;
 *		int		poke_snail;
 *		int		feed_snail;
 *	};
 *
 *	struct elevator_type snail_elv_type {
 *		.ops =		{ ... },
 *		.icq_size =	sizeof(struct snail_io_cq),
 *		.icq_align =	__alignof__(struct snail_io_cq),
 *		...
 *	};
 *
 * If icq_size is set, block core will manage icq's.  All requests will
 * have its ->elv.icq field set before elevator_ops->elevator_set_req_fn()
 * is called and be holding a reference to the associated io_context.
 *
 * Whenever a new icq is created, elevator_ops->elevator_init_icq_fn() is
 * called and, on destruction, ->elevator_exit_icq_fn().  Both functions
 * are called with both the associated io_context and queue locks held.
 *
 * Elevator is allowed to lookup icq using ioc_lookup_icq() while holding
 * queue lock but the returned icq is valid only until the queue lock is
 * released.  Elevators can not and should not try to create or destroy
 * icq's.
 *
 * As icq's are linked from both ioc and q, the locking rules are a bit
 * complex.
 *
 * - ioc lock nests inside q lock.
 *
 * - ioc->icq_list and icq->ioc_node are protected by ioc lock.
 *   q->icq_list and icq->q_node by q lock.
 *
 * - ioc->icq_tree and ioc->icq_hint are protected by ioc lock, while icq
 *   itself is protected by q lock.  However, both the indexes and icq
 *   itself are also RCU managed and lookup can be performed holding only
 *   the q lock.
 *
 * - icq's are not reference counted.  They are destroyed when either the
 *   ioc or q goes away.  Each request with icq set holds an extra
 *   reference to ioc to ensure it stays until the request is completed.
 *
 * - Linking and unlinking icq's are performed while holding both ioc and q
 *   locks.  Due to the lock ordering, q exit is simple but ioc exit
 *   requires reverse-order double lock dance.
 */
struct io_cq {
	struct request_queue	*q;
	struct io_context	*ioc;

	/*
	 * q_node and ioc_node link io_cq through icq_list of q and ioc
	 * respectively.  Both fields are unused once ioc_exit_icq() is
	 * called and shared with __rcu_icq_cache and __rcu_head which are
	 * used for RCU free of io_cq.
	 */
	union {
		struct list_head	q_node;
		struct kmem_cache	*__rcu_icq_cache;
	};
	union {
		struct hlist_node	ioc_node;
		struct rcu_head		__rcu_head;
	};

	unsigned int		flags;
};

/*
 * I/O subsystem state of the associated processes.  It is refcounted
 * and kmalloc'ed. These could be shared between processes.
 */
struct io_context {
	atomic_long_t refcount;
	atomic_t active_ref;
	atomic_t nr_tasks;

	/* all the fields below are protected by this lock */
	spinlock_t lock;

	unsigned short ioprio;

	struct radix_tree_root	icq_tree;
	struct io_cq __rcu	*icq_hint;
	struct hlist_head	icq_list;

	struct work_struct release_work;
};

/**
 * get_io_context_active - get active reference on ioc
 * @ioc: ioc of interest
 *
 * Only iocs with active reference can issue new IOs.  This function
 * acquires an active reference on @ioc.  The caller must already have an
 * active reference on @ioc.
 */
static inline void get_io_context_active(struct io_context *ioc)
{
	WARN_ON_ONCE(atomic_long_read(&ioc->refcount) <= 0);
	WARN_ON_ONCE(atomic_read(&ioc->active_ref) <= 0);
	atomic_long_inc(&ioc->refcount);
	atomic_inc(&ioc->active_ref);
}

static inline void ioc_task_link(struct io_context *ioc)
{
	get_io_context_active(ioc);

	WARN_ON_ONCE(atomic_read(&ioc->nr_tasks) <= 0);
	atomic_inc(&ioc->nr_tasks);
}

struct task_struct;
#ifdef CONFIG_BLOCK
void put_io_context(struct io_context *ioc);
void put_io_context_active(struct io_context *ioc);
void exit_io_context(struct task_struct *task);
struct io_context *get_task_io_context(struct task_struct *task,
				       gfp_t gfp_flags, int node);
#else
struct io_context;
static inline void put_io_context(struct io_context *ioc) { }
static inline void exit_io_context(struct task_struct *task) { }
#endif

#endif
