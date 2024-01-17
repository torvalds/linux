// SPDX-License-Identifier: GPL-2.0
/*
 * Shared application/kernel submission and completion ring pairs, for
 * supporting fast/efficient IO.
 *
 * A note on the read/write ordering memory barriers that are matched between
 * the application and kernel side.
 *
 * After the application reads the CQ ring tail, it must use an
 * appropriate smp_rmb() to pair with the smp_wmb() the kernel uses
 * before writing the tail (using smp_load_acquire to read the tail will
 * do). It also needs a smp_mb() before updating CQ head (ordering the
 * entry load(s) with the head store), pairing with an implicit barrier
 * through a control-dependency in io_get_cqe (smp_store_release to
 * store head will do). Failure to do so could lead to reading invalid
 * CQ entries.
 *
 * Likewise, the application must use an appropriate smp_wmb() before
 * writing the SQ tail (ordering SQ entry stores with the tail store),
 * which pairs with smp_load_acquire in io_get_sqring (smp_store_release
 * to store the tail will do). And it needs a barrier ordering the SQ
 * head load before writing new SQ entries (smp_load_acquire to read
 * head will do).
 *
 * When using the SQ poll thread (IORING_SETUP_SQPOLL), the application
 * needs to check the SQ flags for IORING_SQ_NEED_WAKEUP *after*
 * updating the SQ tail; a full memory barrier smp_mb() is needed
 * between.
 *
 * Also see the examples in the liburing library:
 *
 *	git://git.kernel.dk/liburing
 *
 * io_uring also uses READ/WRITE_ONCE() for _any_ store or load that happens
 * from data shared between the kernel and application. This is done both
 * for ordering purposes, but also to ensure that once a value is loaded from
 * data that the application could potentially modify, it remains stable.
 *
 * Copyright (C) 2018-2019 Jens Axboe
 * Copyright (c) 2018-2019 Christoph Hellwig
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <net/compat.h>
#include <linux/refcount.h>
#include <linux/uio.h>
#include <linux/bits.h>

#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/bvec.h>
#include <linux/net.h>
#include <net/sock.h>
#include <net/af_unix.h>
#include <linux/anon_inodes.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>
#include <linux/highmem.h>
#include <linux/fsnotify.h>
#include <linux/fadvise.h>
#include <linux/task_work.h>
#include <linux/io_uring.h>
#include <linux/io_uring/cmd.h>
#include <linux/audit.h>
#include <linux/security.h>
#include <asm/shmparam.h>

#define CREATE_TRACE_POINTS
#include <trace/events/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io-wq.h"

#include "io_uring.h"
#include "opdef.h"
#include "refs.h"
#include "tctx.h"
#include "register.h"
#include "sqpoll.h"
#include "fdinfo.h"
#include "kbuf.h"
#include "rsrc.h"
#include "cancel.h"
#include "net.h"
#include "notif.h"
#include "waitid.h"
#include "futex.h"

#include "timeout.h"
#include "poll.h"
#include "rw.h"
#include "alloc_cache.h"

#define IORING_MAX_ENTRIES	32768
#define IORING_MAX_CQ_ENTRIES	(2 * IORING_MAX_ENTRIES)

#define SQE_COMMON_FLAGS (IOSQE_FIXED_FILE | IOSQE_IO_LINK | \
			  IOSQE_IO_HARDLINK | IOSQE_ASYNC)

#define SQE_VALID_FLAGS	(SQE_COMMON_FLAGS | IOSQE_BUFFER_SELECT | \
			IOSQE_IO_DRAIN | IOSQE_CQE_SKIP_SUCCESS)

#define IO_REQ_CLEAN_FLAGS (REQ_F_BUFFER_SELECTED | REQ_F_NEED_CLEANUP | \
				REQ_F_POLLED | REQ_F_INFLIGHT | REQ_F_CREDS | \
				REQ_F_ASYNC_DATA)

#define IO_REQ_CLEAN_SLOW_FLAGS (REQ_F_REFCOUNT | REQ_F_LINK | REQ_F_HARDLINK |\
				 IO_REQ_CLEAN_FLAGS)

#define IO_TCTX_REFS_CACHE_NR	(1U << 10)

#define IO_COMPL_BATCH			32
#define IO_REQ_ALLOC_BATCH		8

enum {
	IO_CHECK_CQ_OVERFLOW_BIT,
	IO_CHECK_CQ_DROPPED_BIT,
};

struct io_defer_entry {
	struct list_head	list;
	struct io_kiocb		*req;
	u32			seq;
};

/* requests with any of those set should undergo io_disarm_next() */
#define IO_DISARM_MASK (REQ_F_ARM_LTIMEOUT | REQ_F_LINK_TIMEOUT | REQ_F_FAIL)
#define IO_REQ_LINK_FLAGS (REQ_F_LINK | REQ_F_HARDLINK)

static bool io_uring_try_cancel_requests(struct io_ring_ctx *ctx,
					 struct task_struct *task,
					 bool cancel_all);

static void io_queue_sqe(struct io_kiocb *req);

struct kmem_cache *req_cachep;

static int __read_mostly sysctl_io_uring_disabled;
static int __read_mostly sysctl_io_uring_group = -1;

#ifdef CONFIG_SYSCTL
static struct ctl_table kernel_io_uring_disabled_table[] = {
	{
		.procname	= "io_uring_disabled",
		.data		= &sysctl_io_uring_disabled,
		.maxlen		= sizeof(sysctl_io_uring_disabled),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "io_uring_group",
		.data		= &sysctl_io_uring_group,
		.maxlen		= sizeof(gid_t),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{},
};
#endif

static inline void io_submit_flush_completions(struct io_ring_ctx *ctx)
{
	if (!wq_list_empty(&ctx->submit_state.compl_reqs) ||
	    ctx->submit_state.cqes_count)
		__io_submit_flush_completions(ctx);
}

static inline unsigned int __io_cqring_events(struct io_ring_ctx *ctx)
{
	return ctx->cached_cq_tail - READ_ONCE(ctx->rings->cq.head);
}

static inline unsigned int __io_cqring_events_user(struct io_ring_ctx *ctx)
{
	return READ_ONCE(ctx->rings->cq.tail) - READ_ONCE(ctx->rings->cq.head);
}

static bool io_match_linked(struct io_kiocb *head)
{
	struct io_kiocb *req;

	io_for_each_link(req, head) {
		if (req->flags & REQ_F_INFLIGHT)
			return true;
	}
	return false;
}

/*
 * As io_match_task() but protected against racing with linked timeouts.
 * User must not hold timeout_lock.
 */
bool io_match_task_safe(struct io_kiocb *head, struct task_struct *task,
			bool cancel_all)
{
	bool matched;

	if (task && head->task != task)
		return false;
	if (cancel_all)
		return true;

	if (head->flags & REQ_F_LINK_TIMEOUT) {
		struct io_ring_ctx *ctx = head->ctx;

		/* protect against races with linked timeouts */
		spin_lock_irq(&ctx->timeout_lock);
		matched = io_match_linked(head);
		spin_unlock_irq(&ctx->timeout_lock);
	} else {
		matched = io_match_linked(head);
	}
	return matched;
}

static inline void req_fail_link_node(struct io_kiocb *req, int res)
{
	req_set_fail(req);
	io_req_set_res(req, res, 0);
}

static inline void io_req_add_to_cache(struct io_kiocb *req, struct io_ring_ctx *ctx)
{
	wq_stack_add_head(&req->comp_list, &ctx->submit_state.free_list);
}

static __cold void io_ring_ctx_ref_free(struct percpu_ref *ref)
{
	struct io_ring_ctx *ctx = container_of(ref, struct io_ring_ctx, refs);

	complete(&ctx->ref_comp);
}

static __cold void io_fallback_req_func(struct work_struct *work)
{
	struct io_ring_ctx *ctx = container_of(work, struct io_ring_ctx,
						fallback_work.work);
	struct llist_node *node = llist_del_all(&ctx->fallback_llist);
	struct io_kiocb *req, *tmp;
	struct io_tw_state ts = { .locked = true, };

	percpu_ref_get(&ctx->refs);
	mutex_lock(&ctx->uring_lock);
	llist_for_each_entry_safe(req, tmp, node, io_task_work.node)
		req->io_task_work.func(req, &ts);
	if (WARN_ON_ONCE(!ts.locked))
		return;
	io_submit_flush_completions(ctx);
	mutex_unlock(&ctx->uring_lock);
	percpu_ref_put(&ctx->refs);
}

static int io_alloc_hash_table(struct io_hash_table *table, unsigned bits)
{
	unsigned hash_buckets = 1U << bits;
	size_t hash_size = hash_buckets * sizeof(table->hbs[0]);

	table->hbs = kmalloc(hash_size, GFP_KERNEL);
	if (!table->hbs)
		return -ENOMEM;

	table->hash_bits = bits;
	init_hash_table(table, hash_buckets);
	return 0;
}

static __cold struct io_ring_ctx *io_ring_ctx_alloc(struct io_uring_params *p)
{
	struct io_ring_ctx *ctx;
	int hash_bits;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	xa_init(&ctx->io_bl_xa);

	/*
	 * Use 5 bits less than the max cq entries, that should give us around
	 * 32 entries per hash list if totally full and uniformly spread, but
	 * don't keep too many buckets to not overconsume memory.
	 */
	hash_bits = ilog2(p->cq_entries) - 5;
	hash_bits = clamp(hash_bits, 1, 8);
	if (io_alloc_hash_table(&ctx->cancel_table, hash_bits))
		goto err;
	if (io_alloc_hash_table(&ctx->cancel_table_locked, hash_bits))
		goto err;
	if (percpu_ref_init(&ctx->refs, io_ring_ctx_ref_free,
			    0, GFP_KERNEL))
		goto err;

	ctx->flags = p->flags;
	init_waitqueue_head(&ctx->sqo_sq_wait);
	INIT_LIST_HEAD(&ctx->sqd_list);
	INIT_LIST_HEAD(&ctx->cq_overflow_list);
	INIT_LIST_HEAD(&ctx->io_buffers_cache);
	INIT_HLIST_HEAD(&ctx->io_buf_list);
	io_alloc_cache_init(&ctx->rsrc_node_cache, IO_NODE_ALLOC_CACHE_MAX,
			    sizeof(struct io_rsrc_node));
	io_alloc_cache_init(&ctx->apoll_cache, IO_ALLOC_CACHE_MAX,
			    sizeof(struct async_poll));
	io_alloc_cache_init(&ctx->netmsg_cache, IO_ALLOC_CACHE_MAX,
			    sizeof(struct io_async_msghdr));
	io_futex_cache_init(ctx);
	init_completion(&ctx->ref_comp);
	xa_init_flags(&ctx->personalities, XA_FLAGS_ALLOC1);
	mutex_init(&ctx->uring_lock);
	init_waitqueue_head(&ctx->cq_wait);
	init_waitqueue_head(&ctx->poll_wq);
	init_waitqueue_head(&ctx->rsrc_quiesce_wq);
	spin_lock_init(&ctx->completion_lock);
	spin_lock_init(&ctx->timeout_lock);
	INIT_WQ_LIST(&ctx->iopoll_list);
	INIT_LIST_HEAD(&ctx->io_buffers_comp);
	INIT_LIST_HEAD(&ctx->defer_list);
	INIT_LIST_HEAD(&ctx->timeout_list);
	INIT_LIST_HEAD(&ctx->ltimeout_list);
	INIT_LIST_HEAD(&ctx->rsrc_ref_list);
	init_llist_head(&ctx->work_llist);
	INIT_LIST_HEAD(&ctx->tctx_list);
	ctx->submit_state.free_list.next = NULL;
	INIT_WQ_LIST(&ctx->locked_free_list);
	INIT_HLIST_HEAD(&ctx->waitid_list);
#ifdef CONFIG_FUTEX
	INIT_HLIST_HEAD(&ctx->futex_list);
#endif
	INIT_DELAYED_WORK(&ctx->fallback_work, io_fallback_req_func);
	INIT_WQ_LIST(&ctx->submit_state.compl_reqs);
	INIT_HLIST_HEAD(&ctx->cancelable_uring_cmd);
	return ctx;
err:
	kfree(ctx->cancel_table.hbs);
	kfree(ctx->cancel_table_locked.hbs);
	kfree(ctx->io_bl);
	xa_destroy(&ctx->io_bl_xa);
	kfree(ctx);
	return NULL;
}

static void io_account_cq_overflow(struct io_ring_ctx *ctx)
{
	struct io_rings *r = ctx->rings;

	WRITE_ONCE(r->cq_overflow, READ_ONCE(r->cq_overflow) + 1);
	ctx->cq_extra--;
}

static bool req_need_defer(struct io_kiocb *req, u32 seq)
{
	if (unlikely(req->flags & REQ_F_IO_DRAIN)) {
		struct io_ring_ctx *ctx = req->ctx;

		return seq + READ_ONCE(ctx->cq_extra) != ctx->cached_cq_tail;
	}

	return false;
}

static void io_clean_op(struct io_kiocb *req)
{
	if (req->flags & REQ_F_BUFFER_SELECTED) {
		spin_lock(&req->ctx->completion_lock);
		io_put_kbuf_comp(req);
		spin_unlock(&req->ctx->completion_lock);
	}

	if (req->flags & REQ_F_NEED_CLEANUP) {
		const struct io_cold_def *def = &io_cold_defs[req->opcode];

		if (def->cleanup)
			def->cleanup(req);
	}
	if ((req->flags & REQ_F_POLLED) && req->apoll) {
		kfree(req->apoll->double_poll);
		kfree(req->apoll);
		req->apoll = NULL;
	}
	if (req->flags & REQ_F_INFLIGHT) {
		struct io_uring_task *tctx = req->task->io_uring;

		atomic_dec(&tctx->inflight_tracked);
	}
	if (req->flags & REQ_F_CREDS)
		put_cred(req->creds);
	if (req->flags & REQ_F_ASYNC_DATA) {
		kfree(req->async_data);
		req->async_data = NULL;
	}
	req->flags &= ~IO_REQ_CLEAN_FLAGS;
}

static inline void io_req_track_inflight(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_INFLIGHT)) {
		req->flags |= REQ_F_INFLIGHT;
		atomic_inc(&req->task->io_uring->inflight_tracked);
	}
}

static struct io_kiocb *__io_prep_linked_timeout(struct io_kiocb *req)
{
	if (WARN_ON_ONCE(!req->link))
		return NULL;

	req->flags &= ~REQ_F_ARM_LTIMEOUT;
	req->flags |= REQ_F_LINK_TIMEOUT;

	/* linked timeouts should have two refs once prep'ed */
	io_req_set_refcount(req);
	__io_req_set_refcount(req->link, 2);
	return req->link;
}

static inline struct io_kiocb *io_prep_linked_timeout(struct io_kiocb *req)
{
	if (likely(!(req->flags & REQ_F_ARM_LTIMEOUT)))
		return NULL;
	return __io_prep_linked_timeout(req);
}

static noinline void __io_arm_ltimeout(struct io_kiocb *req)
{
	io_queue_linked_timeout(__io_prep_linked_timeout(req));
}

static inline void io_arm_ltimeout(struct io_kiocb *req)
{
	if (unlikely(req->flags & REQ_F_ARM_LTIMEOUT))
		__io_arm_ltimeout(req);
}

static void io_prep_async_work(struct io_kiocb *req)
{
	const struct io_issue_def *def = &io_issue_defs[req->opcode];
	struct io_ring_ctx *ctx = req->ctx;

	if (!(req->flags & REQ_F_CREDS)) {
		req->flags |= REQ_F_CREDS;
		req->creds = get_current_cred();
	}

	req->work.list.next = NULL;
	req->work.flags = 0;
	req->work.cancel_seq = atomic_read(&ctx->cancel_seq);
	if (req->flags & REQ_F_FORCE_ASYNC)
		req->work.flags |= IO_WQ_WORK_CONCURRENT;

	if (req->file && !(req->flags & REQ_F_FIXED_FILE))
		req->flags |= io_file_get_flags(req->file);

	if (req->file && (req->flags & REQ_F_ISREG)) {
		bool should_hash = def->hash_reg_file;

		/* don't serialize this request if the fs doesn't need it */
		if (should_hash && (req->file->f_flags & O_DIRECT) &&
		    (req->file->f_mode & FMODE_DIO_PARALLEL_WRITE))
			should_hash = false;
		if (should_hash || (ctx->flags & IORING_SETUP_IOPOLL))
			io_wq_hash_work(&req->work, file_inode(req->file));
	} else if (!req->file || !S_ISBLK(file_inode(req->file)->i_mode)) {
		if (def->unbound_nonreg_file)
			req->work.flags |= IO_WQ_WORK_UNBOUND;
	}
}

static void io_prep_async_link(struct io_kiocb *req)
{
	struct io_kiocb *cur;

	if (req->flags & REQ_F_LINK_TIMEOUT) {
		struct io_ring_ctx *ctx = req->ctx;

		spin_lock_irq(&ctx->timeout_lock);
		io_for_each_link(cur, req)
			io_prep_async_work(cur);
		spin_unlock_irq(&ctx->timeout_lock);
	} else {
		io_for_each_link(cur, req)
			io_prep_async_work(cur);
	}
}

void io_queue_iowq(struct io_kiocb *req, struct io_tw_state *ts_dont_use)
{
	struct io_kiocb *link = io_prep_linked_timeout(req);
	struct io_uring_task *tctx = req->task->io_uring;

	BUG_ON(!tctx);
	BUG_ON(!tctx->io_wq);

	/* init ->work of the whole link before punting */
	io_prep_async_link(req);

	/*
	 * Not expected to happen, but if we do have a bug where this _can_
	 * happen, catch it here and ensure the request is marked as
	 * canceled. That will make io-wq go through the usual work cancel
	 * procedure rather than attempt to run this request (or create a new
	 * worker for it).
	 */
	if (WARN_ON_ONCE(!same_thread_group(req->task, current)))
		req->work.flags |= IO_WQ_WORK_CANCEL;

	trace_io_uring_queue_async_work(req, io_wq_is_hashed(&req->work));
	io_wq_enqueue(tctx->io_wq, &req->work);
	if (link)
		io_queue_linked_timeout(link);
}

static __cold void io_queue_deferred(struct io_ring_ctx *ctx)
{
	while (!list_empty(&ctx->defer_list)) {
		struct io_defer_entry *de = list_first_entry(&ctx->defer_list,
						struct io_defer_entry, list);

		if (req_need_defer(de->req, de->seq))
			break;
		list_del_init(&de->list);
		io_req_task_queue(de->req);
		kfree(de);
	}
}

void io_eventfd_ops(struct rcu_head *rcu)
{
	struct io_ev_fd *ev_fd = container_of(rcu, struct io_ev_fd, rcu);
	int ops = atomic_xchg(&ev_fd->ops, 0);

	if (ops & BIT(IO_EVENTFD_OP_SIGNAL_BIT))
		eventfd_signal_mask(ev_fd->cq_ev_fd, EPOLL_URING_WAKE);

	/* IO_EVENTFD_OP_FREE_BIT may not be set here depending on callback
	 * ordering in a race but if references are 0 we know we have to free
	 * it regardless.
	 */
	if (atomic_dec_and_test(&ev_fd->refs)) {
		eventfd_ctx_put(ev_fd->cq_ev_fd);
		kfree(ev_fd);
	}
}

static void io_eventfd_signal(struct io_ring_ctx *ctx)
{
	struct io_ev_fd *ev_fd = NULL;

	rcu_read_lock();
	/*
	 * rcu_dereference ctx->io_ev_fd once and use it for both for checking
	 * and eventfd_signal
	 */
	ev_fd = rcu_dereference(ctx->io_ev_fd);

	/*
	 * Check again if ev_fd exists incase an io_eventfd_unregister call
	 * completed between the NULL check of ctx->io_ev_fd at the start of
	 * the function and rcu_read_lock.
	 */
	if (unlikely(!ev_fd))
		goto out;
	if (READ_ONCE(ctx->rings->cq_flags) & IORING_CQ_EVENTFD_DISABLED)
		goto out;
	if (ev_fd->eventfd_async && !io_wq_current_is_worker())
		goto out;

	if (likely(eventfd_signal_allowed())) {
		eventfd_signal_mask(ev_fd->cq_ev_fd, EPOLL_URING_WAKE);
	} else {
		atomic_inc(&ev_fd->refs);
		if (!atomic_fetch_or(BIT(IO_EVENTFD_OP_SIGNAL_BIT), &ev_fd->ops))
			call_rcu_hurry(&ev_fd->rcu, io_eventfd_ops);
		else
			atomic_dec(&ev_fd->refs);
	}

out:
	rcu_read_unlock();
}

static void io_eventfd_flush_signal(struct io_ring_ctx *ctx)
{
	bool skip;

	spin_lock(&ctx->completion_lock);

	/*
	 * Eventfd should only get triggered when at least one event has been
	 * posted. Some applications rely on the eventfd notification count
	 * only changing IFF a new CQE has been added to the CQ ring. There's
	 * no depedency on 1:1 relationship between how many times this
	 * function is called (and hence the eventfd count) and number of CQEs
	 * posted to the CQ ring.
	 */
	skip = ctx->cached_cq_tail == ctx->evfd_last_cq_tail;
	ctx->evfd_last_cq_tail = ctx->cached_cq_tail;
	spin_unlock(&ctx->completion_lock);
	if (skip)
		return;

	io_eventfd_signal(ctx);
}

void __io_commit_cqring_flush(struct io_ring_ctx *ctx)
{
	if (ctx->poll_activated)
		io_poll_wq_wake(ctx);
	if (ctx->off_timeout_used)
		io_flush_timeouts(ctx);
	if (ctx->drain_active) {
		spin_lock(&ctx->completion_lock);
		io_queue_deferred(ctx);
		spin_unlock(&ctx->completion_lock);
	}
	if (ctx->has_evfd)
		io_eventfd_flush_signal(ctx);
}

static inline void __io_cq_lock(struct io_ring_ctx *ctx)
{
	if (!ctx->lockless_cq)
		spin_lock(&ctx->completion_lock);
}

static inline void io_cq_lock(struct io_ring_ctx *ctx)
	__acquires(ctx->completion_lock)
{
	spin_lock(&ctx->completion_lock);
}

static inline void __io_cq_unlock_post(struct io_ring_ctx *ctx)
{
	io_commit_cqring(ctx);
	if (!ctx->task_complete) {
		if (!ctx->lockless_cq)
			spin_unlock(&ctx->completion_lock);
		/* IOPOLL rings only need to wake up if it's also SQPOLL */
		if (!ctx->syscall_iopoll)
			io_cqring_wake(ctx);
	}
	io_commit_cqring_flush(ctx);
}

static void io_cq_unlock_post(struct io_ring_ctx *ctx)
	__releases(ctx->completion_lock)
{
	io_commit_cqring(ctx);
	spin_unlock(&ctx->completion_lock);
	io_cqring_wake(ctx);
	io_commit_cqring_flush(ctx);
}

/* Returns true if there are no backlogged entries after the flush */
static void io_cqring_overflow_kill(struct io_ring_ctx *ctx)
{
	struct io_overflow_cqe *ocqe;
	LIST_HEAD(list);

	spin_lock(&ctx->completion_lock);
	list_splice_init(&ctx->cq_overflow_list, &list);
	clear_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq);
	spin_unlock(&ctx->completion_lock);

	while (!list_empty(&list)) {
		ocqe = list_first_entry(&list, struct io_overflow_cqe, list);
		list_del(&ocqe->list);
		kfree(ocqe);
	}
}

static void __io_cqring_overflow_flush(struct io_ring_ctx *ctx)
{
	size_t cqe_size = sizeof(struct io_uring_cqe);

	if (__io_cqring_events(ctx) == ctx->cq_entries)
		return;

	if (ctx->flags & IORING_SETUP_CQE32)
		cqe_size <<= 1;

	io_cq_lock(ctx);
	while (!list_empty(&ctx->cq_overflow_list)) {
		struct io_uring_cqe *cqe;
		struct io_overflow_cqe *ocqe;

		if (!io_get_cqe_overflow(ctx, &cqe, true))
			break;
		ocqe = list_first_entry(&ctx->cq_overflow_list,
					struct io_overflow_cqe, list);
		memcpy(cqe, &ocqe->cqe, cqe_size);
		list_del(&ocqe->list);
		kfree(ocqe);
	}

	if (list_empty(&ctx->cq_overflow_list)) {
		clear_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq);
		atomic_andnot(IORING_SQ_CQ_OVERFLOW, &ctx->rings->sq_flags);
	}
	io_cq_unlock_post(ctx);
}

static void io_cqring_do_overflow_flush(struct io_ring_ctx *ctx)
{
	/* iopoll syncs against uring_lock, not completion_lock */
	if (ctx->flags & IORING_SETUP_IOPOLL)
		mutex_lock(&ctx->uring_lock);
	__io_cqring_overflow_flush(ctx);
	if (ctx->flags & IORING_SETUP_IOPOLL)
		mutex_unlock(&ctx->uring_lock);
}

static void io_cqring_overflow_flush(struct io_ring_ctx *ctx)
{
	if (test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq))
		io_cqring_do_overflow_flush(ctx);
}

/* can be called by any task */
static void io_put_task_remote(struct task_struct *task)
{
	struct io_uring_task *tctx = task->io_uring;

	percpu_counter_sub(&tctx->inflight, 1);
	if (unlikely(atomic_read(&tctx->in_cancel)))
		wake_up(&tctx->wait);
	put_task_struct(task);
}

/* used by a task to put its own references */
static void io_put_task_local(struct task_struct *task)
{
	task->io_uring->cached_refs++;
}

/* must to be called somewhat shortly after putting a request */
static inline void io_put_task(struct task_struct *task)
{
	if (likely(task == current))
		io_put_task_local(task);
	else
		io_put_task_remote(task);
}

void io_task_refs_refill(struct io_uring_task *tctx)
{
	unsigned int refill = -tctx->cached_refs + IO_TCTX_REFS_CACHE_NR;

	percpu_counter_add(&tctx->inflight, refill);
	refcount_add(refill, &current->usage);
	tctx->cached_refs += refill;
}

static __cold void io_uring_drop_tctx_refs(struct task_struct *task)
{
	struct io_uring_task *tctx = task->io_uring;
	unsigned int refs = tctx->cached_refs;

	if (refs) {
		tctx->cached_refs = 0;
		percpu_counter_sub(&tctx->inflight, refs);
		put_task_struct_many(task, refs);
	}
}

static bool io_cqring_event_overflow(struct io_ring_ctx *ctx, u64 user_data,
				     s32 res, u32 cflags, u64 extra1, u64 extra2)
{
	struct io_overflow_cqe *ocqe;
	size_t ocq_size = sizeof(struct io_overflow_cqe);
	bool is_cqe32 = (ctx->flags & IORING_SETUP_CQE32);

	lockdep_assert_held(&ctx->completion_lock);

	if (is_cqe32)
		ocq_size += sizeof(struct io_uring_cqe);

	ocqe = kmalloc(ocq_size, GFP_ATOMIC | __GFP_ACCOUNT);
	trace_io_uring_cqe_overflow(ctx, user_data, res, cflags, ocqe);
	if (!ocqe) {
		/*
		 * If we're in ring overflow flush mode, or in task cancel mode,
		 * or cannot allocate an overflow entry, then we need to drop it
		 * on the floor.
		 */
		io_account_cq_overflow(ctx);
		set_bit(IO_CHECK_CQ_DROPPED_BIT, &ctx->check_cq);
		return false;
	}
	if (list_empty(&ctx->cq_overflow_list)) {
		set_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq);
		atomic_or(IORING_SQ_CQ_OVERFLOW, &ctx->rings->sq_flags);

	}
	ocqe->cqe.user_data = user_data;
	ocqe->cqe.res = res;
	ocqe->cqe.flags = cflags;
	if (is_cqe32) {
		ocqe->cqe.big_cqe[0] = extra1;
		ocqe->cqe.big_cqe[1] = extra2;
	}
	list_add_tail(&ocqe->list, &ctx->cq_overflow_list);
	return true;
}

void io_req_cqe_overflow(struct io_kiocb *req)
{
	io_cqring_event_overflow(req->ctx, req->cqe.user_data,
				req->cqe.res, req->cqe.flags,
				req->big_cqe.extra1, req->big_cqe.extra2);
	memset(&req->big_cqe, 0, sizeof(req->big_cqe));
}

/*
 * writes to the cq entry need to come after reading head; the
 * control dependency is enough as we're using WRITE_ONCE to
 * fill the cq entry
 */
bool io_cqe_cache_refill(struct io_ring_ctx *ctx, bool overflow)
{
	struct io_rings *rings = ctx->rings;
	unsigned int off = ctx->cached_cq_tail & (ctx->cq_entries - 1);
	unsigned int free, queued, len;

	/*
	 * Posting into the CQ when there are pending overflowed CQEs may break
	 * ordering guarantees, which will affect links, F_MORE users and more.
	 * Force overflow the completion.
	 */
	if (!overflow && (ctx->check_cq & BIT(IO_CHECK_CQ_OVERFLOW_BIT)))
		return false;

	/* userspace may cheat modifying the tail, be safe and do min */
	queued = min(__io_cqring_events(ctx), ctx->cq_entries);
	free = ctx->cq_entries - queued;
	/* we need a contiguous range, limit based on the current array offset */
	len = min(free, ctx->cq_entries - off);
	if (!len)
		return false;

	if (ctx->flags & IORING_SETUP_CQE32) {
		off <<= 1;
		len <<= 1;
	}

	ctx->cqe_cached = &rings->cqes[off];
	ctx->cqe_sentinel = ctx->cqe_cached + len;
	return true;
}

static bool io_fill_cqe_aux(struct io_ring_ctx *ctx, u64 user_data, s32 res,
			      u32 cflags)
{
	struct io_uring_cqe *cqe;

	ctx->cq_extra++;

	/*
	 * If we can't get a cq entry, userspace overflowed the
	 * submission (by quite a lot). Increment the overflow count in
	 * the ring.
	 */
	if (likely(io_get_cqe(ctx, &cqe))) {
		trace_io_uring_complete(ctx, NULL, user_data, res, cflags, 0, 0);

		WRITE_ONCE(cqe->user_data, user_data);
		WRITE_ONCE(cqe->res, res);
		WRITE_ONCE(cqe->flags, cflags);

		if (ctx->flags & IORING_SETUP_CQE32) {
			WRITE_ONCE(cqe->big_cqe[0], 0);
			WRITE_ONCE(cqe->big_cqe[1], 0);
		}
		return true;
	}
	return false;
}

static void __io_flush_post_cqes(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	struct io_submit_state *state = &ctx->submit_state;
	unsigned int i;

	lockdep_assert_held(&ctx->uring_lock);
	for (i = 0; i < state->cqes_count; i++) {
		struct io_uring_cqe *cqe = &ctx->completion_cqes[i];

		if (!io_fill_cqe_aux(ctx, cqe->user_data, cqe->res, cqe->flags)) {
			if (ctx->lockless_cq) {
				spin_lock(&ctx->completion_lock);
				io_cqring_event_overflow(ctx, cqe->user_data,
							cqe->res, cqe->flags, 0, 0);
				spin_unlock(&ctx->completion_lock);
			} else {
				io_cqring_event_overflow(ctx, cqe->user_data,
							cqe->res, cqe->flags, 0, 0);
			}
		}
	}
	state->cqes_count = 0;
}

static bool __io_post_aux_cqe(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags,
			      bool allow_overflow)
{
	bool filled;

	io_cq_lock(ctx);
	filled = io_fill_cqe_aux(ctx, user_data, res, cflags);
	if (!filled && allow_overflow)
		filled = io_cqring_event_overflow(ctx, user_data, res, cflags, 0, 0);

	io_cq_unlock_post(ctx);
	return filled;
}

bool io_post_aux_cqe(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags)
{
	return __io_post_aux_cqe(ctx, user_data, res, cflags, true);
}

/*
 * A helper for multishot requests posting additional CQEs.
 * Should only be used from a task_work including IO_URING_F_MULTISHOT.
 */
bool io_fill_cqe_req_aux(struct io_kiocb *req, bool defer, s32 res, u32 cflags)
{
	struct io_ring_ctx *ctx = req->ctx;
	u64 user_data = req->cqe.user_data;
	struct io_uring_cqe *cqe;

	if (!defer)
		return __io_post_aux_cqe(ctx, user_data, res, cflags, false);

	lockdep_assert_held(&ctx->uring_lock);

	if (ctx->submit_state.cqes_count == ARRAY_SIZE(ctx->completion_cqes)) {
		__io_cq_lock(ctx);
		__io_flush_post_cqes(ctx);
		/* no need to flush - flush is deferred */
		__io_cq_unlock_post(ctx);
	}

	/* For defered completions this is not as strict as it is otherwise,
	 * however it's main job is to prevent unbounded posted completions,
	 * and in that it works just as well.
	 */
	if (test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq))
		return false;

	cqe = &ctx->completion_cqes[ctx->submit_state.cqes_count++];
	cqe->user_data = user_data;
	cqe->res = res;
	cqe->flags = cflags;
	return true;
}

static void __io_req_complete_post(struct io_kiocb *req, unsigned issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_rsrc_node *rsrc_node = NULL;

	io_cq_lock(ctx);
	if (!(req->flags & REQ_F_CQE_SKIP)) {
		if (!io_fill_cqe_req(ctx, req))
			io_req_cqe_overflow(req);
	}

	/*
	 * If we're the last reference to this request, add to our locked
	 * free_list cache.
	 */
	if (req_ref_put_and_test(req)) {
		if (req->flags & IO_REQ_LINK_FLAGS) {
			if (req->flags & IO_DISARM_MASK)
				io_disarm_next(req);
			if (req->link) {
				io_req_task_queue(req->link);
				req->link = NULL;
			}
		}
		io_put_kbuf_comp(req);
		if (unlikely(req->flags & IO_REQ_CLEAN_FLAGS))
			io_clean_op(req);
		io_put_file(req);

		rsrc_node = req->rsrc_node;
		/*
		 * Selected buffer deallocation in io_clean_op() assumes that
		 * we don't hold ->completion_lock. Clean them here to avoid
		 * deadlocks.
		 */
		io_put_task_remote(req->task);
		wq_list_add_head(&req->comp_list, &ctx->locked_free_list);
		ctx->locked_free_nr++;
	}
	io_cq_unlock_post(ctx);

	if (rsrc_node) {
		io_ring_submit_lock(ctx, issue_flags);
		io_put_rsrc_node(ctx, rsrc_node);
		io_ring_submit_unlock(ctx, issue_flags);
	}
}

void io_req_complete_post(struct io_kiocb *req, unsigned issue_flags)
{
	if (req->ctx->task_complete && req->ctx->submitter_task != current) {
		req->io_task_work.func = io_req_task_complete;
		io_req_task_work_add(req);
	} else if (!(issue_flags & IO_URING_F_UNLOCKED) ||
		   !(req->ctx->flags & IORING_SETUP_IOPOLL)) {
		__io_req_complete_post(req, issue_flags);
	} else {
		struct io_ring_ctx *ctx = req->ctx;

		mutex_lock(&ctx->uring_lock);
		__io_req_complete_post(req, issue_flags & ~IO_URING_F_UNLOCKED);
		mutex_unlock(&ctx->uring_lock);
	}
}

void io_req_defer_failed(struct io_kiocb *req, s32 res)
	__must_hold(&ctx->uring_lock)
{
	const struct io_cold_def *def = &io_cold_defs[req->opcode];

	lockdep_assert_held(&req->ctx->uring_lock);

	req_set_fail(req);
	io_req_set_res(req, res, io_put_kbuf(req, IO_URING_F_UNLOCKED));
	if (def->fail)
		def->fail(req);
	io_req_complete_defer(req);
}

/*
 * Don't initialise the fields below on every allocation, but do that in
 * advance and keep them valid across allocations.
 */
static void io_preinit_req(struct io_kiocb *req, struct io_ring_ctx *ctx)
{
	req->ctx = ctx;
	req->link = NULL;
	req->async_data = NULL;
	/* not necessary, but safer to zero */
	memset(&req->cqe, 0, sizeof(req->cqe));
	memset(&req->big_cqe, 0, sizeof(req->big_cqe));
}

static void io_flush_cached_locked_reqs(struct io_ring_ctx *ctx,
					struct io_submit_state *state)
{
	spin_lock(&ctx->completion_lock);
	wq_list_splice(&ctx->locked_free_list, &state->free_list);
	ctx->locked_free_nr = 0;
	spin_unlock(&ctx->completion_lock);
}

/*
 * A request might get retired back into the request caches even before opcode
 * handlers and io_issue_sqe() are done with it, e.g. inline completion path.
 * Because of that, io_alloc_req() should be called only under ->uring_lock
 * and with extra caution to not get a request that is still worked on.
 */
__cold bool __io_alloc_req_refill(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;
	void *reqs[IO_REQ_ALLOC_BATCH];
	int ret, i;

	/*
	 * If we have more than a batch's worth of requests in our IRQ side
	 * locked cache, grab the lock and move them over to our submission
	 * side cache.
	 */
	if (data_race(ctx->locked_free_nr) > IO_COMPL_BATCH) {
		io_flush_cached_locked_reqs(ctx, &ctx->submit_state);
		if (!io_req_cache_empty(ctx))
			return true;
	}

	ret = kmem_cache_alloc_bulk(req_cachep, gfp, ARRAY_SIZE(reqs), reqs);

	/*
	 * Bulk alloc is all-or-nothing. If we fail to get a batch,
	 * retry single alloc to be on the safe side.
	 */
	if (unlikely(ret <= 0)) {
		reqs[0] = kmem_cache_alloc(req_cachep, gfp);
		if (!reqs[0])
			return false;
		ret = 1;
	}

	percpu_ref_get_many(&ctx->refs, ret);
	for (i = 0; i < ret; i++) {
		struct io_kiocb *req = reqs[i];

		io_preinit_req(req, ctx);
		io_req_add_to_cache(req, ctx);
	}
	return true;
}

__cold void io_free_req(struct io_kiocb *req)
{
	/* refs were already put, restore them for io_req_task_complete() */
	req->flags &= ~REQ_F_REFCOUNT;
	/* we only want to free it, don't post CQEs */
	req->flags |= REQ_F_CQE_SKIP;
	req->io_task_work.func = io_req_task_complete;
	io_req_task_work_add(req);
}

static void __io_req_find_next_prep(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	spin_lock(&ctx->completion_lock);
	io_disarm_next(req);
	spin_unlock(&ctx->completion_lock);
}

static inline struct io_kiocb *io_req_find_next(struct io_kiocb *req)
{
	struct io_kiocb *nxt;

	/*
	 * If LINK is set, we have dependent requests in this chain. If we
	 * didn't fail this request, queue the first one up, moving any other
	 * dependencies to the next request. In case of failure, fail the rest
	 * of the chain.
	 */
	if (unlikely(req->flags & IO_DISARM_MASK))
		__io_req_find_next_prep(req);
	nxt = req->link;
	req->link = NULL;
	return nxt;
}

static void ctx_flush_and_put(struct io_ring_ctx *ctx, struct io_tw_state *ts)
{
	if (!ctx)
		return;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_andnot(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
	if (ts->locked) {
		io_submit_flush_completions(ctx);
		mutex_unlock(&ctx->uring_lock);
		ts->locked = false;
	}
	percpu_ref_put(&ctx->refs);
}

static unsigned int handle_tw_list(struct llist_node *node,
				   struct io_ring_ctx **ctx,
				   struct io_tw_state *ts,
				   struct llist_node *last)
{
	unsigned int count = 0;

	while (node && node != last) {
		struct llist_node *next = node->next;
		struct io_kiocb *req = container_of(node, struct io_kiocb,
						    io_task_work.node);

		prefetch(container_of(next, struct io_kiocb, io_task_work.node));

		if (req->ctx != *ctx) {
			ctx_flush_and_put(*ctx, ts);
			*ctx = req->ctx;
			/* if not contended, grab and improve batching */
			ts->locked = mutex_trylock(&(*ctx)->uring_lock);
			percpu_ref_get(&(*ctx)->refs);
		}
		INDIRECT_CALL_2(req->io_task_work.func,
				io_poll_task_func, io_req_rw_complete,
				req, ts);
		node = next;
		count++;
		if (unlikely(need_resched())) {
			ctx_flush_and_put(*ctx, ts);
			*ctx = NULL;
			cond_resched();
		}
	}

	return count;
}

/**
 * io_llist_xchg - swap all entries in a lock-less list
 * @head:	the head of lock-less list to delete all entries
 * @new:	new entry as the head of the list
 *
 * If list is empty, return NULL, otherwise, return the pointer to the first entry.
 * The order of entries returned is from the newest to the oldest added one.
 */
static inline struct llist_node *io_llist_xchg(struct llist_head *head,
					       struct llist_node *new)
{
	return xchg(&head->first, new);
}

/**
 * io_llist_cmpxchg - possibly swap all entries in a lock-less list
 * @head:	the head of lock-less list to delete all entries
 * @old:	expected old value of the first entry of the list
 * @new:	new entry as the head of the list
 *
 * perform a cmpxchg on the first entry of the list.
 */

static inline struct llist_node *io_llist_cmpxchg(struct llist_head *head,
						  struct llist_node *old,
						  struct llist_node *new)
{
	return cmpxchg(&head->first, old, new);
}

static __cold void io_fallback_tw(struct io_uring_task *tctx, bool sync)
{
	struct llist_node *node = llist_del_all(&tctx->task_list);
	struct io_ring_ctx *last_ctx = NULL;
	struct io_kiocb *req;

	while (node) {
		req = container_of(node, struct io_kiocb, io_task_work.node);
		node = node->next;
		if (sync && last_ctx != req->ctx) {
			if (last_ctx) {
				flush_delayed_work(&last_ctx->fallback_work);
				percpu_ref_put(&last_ctx->refs);
			}
			last_ctx = req->ctx;
			percpu_ref_get(&last_ctx->refs);
		}
		if (llist_add(&req->io_task_work.node,
			      &req->ctx->fallback_llist))
			schedule_delayed_work(&req->ctx->fallback_work, 1);
	}

	if (last_ctx) {
		flush_delayed_work(&last_ctx->fallback_work);
		percpu_ref_put(&last_ctx->refs);
	}
}

void tctx_task_work(struct callback_head *cb)
{
	struct io_tw_state ts = {};
	struct io_ring_ctx *ctx = NULL;
	struct io_uring_task *tctx = container_of(cb, struct io_uring_task,
						  task_work);
	struct llist_node fake = {};
	struct llist_node *node;
	unsigned int loops = 0;
	unsigned int count = 0;

	if (unlikely(current->flags & PF_EXITING)) {
		io_fallback_tw(tctx, true);
		return;
	}

	do {
		loops++;
		node = io_llist_xchg(&tctx->task_list, &fake);
		count += handle_tw_list(node, &ctx, &ts, &fake);

		/* skip expensive cmpxchg if there are items in the list */
		if (READ_ONCE(tctx->task_list.first) != &fake)
			continue;
		if (ts.locked && !wq_list_empty(&ctx->submit_state.compl_reqs)) {
			io_submit_flush_completions(ctx);
			if (READ_ONCE(tctx->task_list.first) != &fake)
				continue;
		}
		node = io_llist_cmpxchg(&tctx->task_list, &fake, NULL);
	} while (node != &fake);

	ctx_flush_and_put(ctx, &ts);

	/* relaxed read is enough as only the task itself sets ->in_cancel */
	if (unlikely(atomic_read(&tctx->in_cancel)))
		io_uring_drop_tctx_refs(current);

	trace_io_uring_task_work_run(tctx, count, loops);
}

static inline void io_req_local_work_add(struct io_kiocb *req, unsigned flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned nr_wait, nr_tw, nr_tw_prev;
	struct llist_node *first;

	if (req->flags & (REQ_F_LINK | REQ_F_HARDLINK))
		flags &= ~IOU_F_TWQ_LAZY_WAKE;

	first = READ_ONCE(ctx->work_llist.first);
	do {
		nr_tw_prev = 0;
		if (first) {
			struct io_kiocb *first_req = container_of(first,
							struct io_kiocb,
							io_task_work.node);
			/*
			 * Might be executed at any moment, rely on
			 * SLAB_TYPESAFE_BY_RCU to keep it alive.
			 */
			nr_tw_prev = READ_ONCE(first_req->nr_tw);
		}
		nr_tw = nr_tw_prev + 1;
		/* Large enough to fail the nr_wait comparison below */
		if (!(flags & IOU_F_TWQ_LAZY_WAKE))
			nr_tw = -1U;

		req->nr_tw = nr_tw;
		req->io_task_work.node.next = first;
	} while (!try_cmpxchg(&ctx->work_llist.first, &first,
			      &req->io_task_work.node));

	if (!first) {
		if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
			atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
		if (ctx->has_evfd)
			io_eventfd_signal(ctx);
	}

	nr_wait = atomic_read(&ctx->cq_wait_nr);
	/* no one is waiting */
	if (!nr_wait)
		return;
	/* either not enough or the previous add has already woken it up */
	if (nr_wait > nr_tw || nr_tw_prev >= nr_wait)
		return;
	/* pairs with set_current_state() in io_cqring_wait() */
	smp_mb__after_atomic();
	wake_up_state(ctx->submitter_task, TASK_INTERRUPTIBLE);
}

static void io_req_normal_work_add(struct io_kiocb *req)
{
	struct io_uring_task *tctx = req->task->io_uring;
	struct io_ring_ctx *ctx = req->ctx;

	/* task_work already pending, we're done */
	if (!llist_add(&req->io_task_work.node, &tctx->task_list))
		return;

	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);

	if (likely(!task_work_add(req->task, &tctx->task_work, ctx->notify_method)))
		return;

	io_fallback_tw(tctx, false);
}

void __io_req_task_work_add(struct io_kiocb *req, unsigned flags)
{
	if (req->ctx->flags & IORING_SETUP_DEFER_TASKRUN) {
		rcu_read_lock();
		io_req_local_work_add(req, flags);
		rcu_read_unlock();
	} else {
		io_req_normal_work_add(req);
	}
}

static void __cold io_move_task_work_from_local(struct io_ring_ctx *ctx)
{
	struct llist_node *node;

	node = llist_del_all(&ctx->work_llist);
	while (node) {
		struct io_kiocb *req = container_of(node, struct io_kiocb,
						    io_task_work.node);

		node = node->next;
		io_req_normal_work_add(req);
	}
}

static int __io_run_local_work(struct io_ring_ctx *ctx, struct io_tw_state *ts)
{
	struct llist_node *node;
	unsigned int loops = 0;
	int ret = 0;

	if (WARN_ON_ONCE(ctx->submitter_task != current))
		return -EEXIST;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_andnot(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
again:
	/*
	 * llists are in reverse order, flip it back the right way before
	 * running the pending items.
	 */
	node = llist_reverse_order(io_llist_xchg(&ctx->work_llist, NULL));
	while (node) {
		struct llist_node *next = node->next;
		struct io_kiocb *req = container_of(node, struct io_kiocb,
						    io_task_work.node);
		prefetch(container_of(next, struct io_kiocb, io_task_work.node));
		INDIRECT_CALL_2(req->io_task_work.func,
				io_poll_task_func, io_req_rw_complete,
				req, ts);
		ret++;
		node = next;
	}
	loops++;

	if (!llist_empty(&ctx->work_llist))
		goto again;
	if (ts->locked) {
		io_submit_flush_completions(ctx);
		if (!llist_empty(&ctx->work_llist))
			goto again;
	}
	trace_io_uring_local_work_run(ctx, ret, loops);
	return ret;
}

static inline int io_run_local_work_locked(struct io_ring_ctx *ctx)
{
	struct io_tw_state ts = { .locked = true, };
	int ret;

	if (llist_empty(&ctx->work_llist))
		return 0;

	ret = __io_run_local_work(ctx, &ts);
	/* shouldn't happen! */
	if (WARN_ON_ONCE(!ts.locked))
		mutex_lock(&ctx->uring_lock);
	return ret;
}

static int io_run_local_work(struct io_ring_ctx *ctx)
{
	struct io_tw_state ts = {};
	int ret;

	ts.locked = mutex_trylock(&ctx->uring_lock);
	ret = __io_run_local_work(ctx, &ts);
	if (ts.locked)
		mutex_unlock(&ctx->uring_lock);

	return ret;
}

static void io_req_task_cancel(struct io_kiocb *req, struct io_tw_state *ts)
{
	io_tw_lock(req->ctx, ts);
	io_req_defer_failed(req, req->cqe.res);
}

void io_req_task_submit(struct io_kiocb *req, struct io_tw_state *ts)
{
	io_tw_lock(req->ctx, ts);
	/* req->task == current here, checking PF_EXITING is safe */
	if (unlikely(req->task->flags & PF_EXITING))
		io_req_defer_failed(req, -EFAULT);
	else if (req->flags & REQ_F_FORCE_ASYNC)
		io_queue_iowq(req, ts);
	else
		io_queue_sqe(req);
}

void io_req_task_queue_fail(struct io_kiocb *req, int ret)
{
	io_req_set_res(req, ret, 0);
	req->io_task_work.func = io_req_task_cancel;
	io_req_task_work_add(req);
}

void io_req_task_queue(struct io_kiocb *req)
{
	req->io_task_work.func = io_req_task_submit;
	io_req_task_work_add(req);
}

void io_queue_next(struct io_kiocb *req)
{
	struct io_kiocb *nxt = io_req_find_next(req);

	if (nxt)
		io_req_task_queue(nxt);
}

static void io_free_batch_list(struct io_ring_ctx *ctx,
			       struct io_wq_work_node *node)
	__must_hold(&ctx->uring_lock)
{
	do {
		struct io_kiocb *req = container_of(node, struct io_kiocb,
						    comp_list);

		if (unlikely(req->flags & IO_REQ_CLEAN_SLOW_FLAGS)) {
			if (req->flags & REQ_F_REFCOUNT) {
				node = req->comp_list.next;
				if (!req_ref_put_and_test(req))
					continue;
			}
			if ((req->flags & REQ_F_POLLED) && req->apoll) {
				struct async_poll *apoll = req->apoll;

				if (apoll->double_poll)
					kfree(apoll->double_poll);
				if (!io_alloc_cache_put(&ctx->apoll_cache, &apoll->cache))
					kfree(apoll);
				req->flags &= ~REQ_F_POLLED;
			}
			if (req->flags & IO_REQ_LINK_FLAGS)
				io_queue_next(req);
			if (unlikely(req->flags & IO_REQ_CLEAN_FLAGS))
				io_clean_op(req);
		}
		io_put_file(req);

		io_req_put_rsrc_locked(req, ctx);

		io_put_task(req->task);
		node = req->comp_list.next;
		io_req_add_to_cache(req, ctx);
	} while (node);
}

void __io_submit_flush_completions(struct io_ring_ctx *ctx)
	__must_hold(&ctx->uring_lock)
{
	struct io_submit_state *state = &ctx->submit_state;
	struct io_wq_work_node *node;

	__io_cq_lock(ctx);
	/* must come first to preserve CQE ordering in failure cases */
	if (state->cqes_count)
		__io_flush_post_cqes(ctx);
	__wq_list_for_each(node, &state->compl_reqs) {
		struct io_kiocb *req = container_of(node, struct io_kiocb,
					    comp_list);

		if (!(req->flags & REQ_F_CQE_SKIP) &&
		    unlikely(!io_fill_cqe_req(ctx, req))) {
			if (ctx->lockless_cq) {
				spin_lock(&ctx->completion_lock);
				io_req_cqe_overflow(req);
				spin_unlock(&ctx->completion_lock);
			} else {
				io_req_cqe_overflow(req);
			}
		}
	}
	__io_cq_unlock_post(ctx);

	if (!wq_list_empty(&ctx->submit_state.compl_reqs)) {
		io_free_batch_list(ctx, state->compl_reqs.first);
		INIT_WQ_LIST(&state->compl_reqs);
	}
}

static unsigned io_cqring_events(struct io_ring_ctx *ctx)
{
	/* See comment at the top of this file */
	smp_rmb();
	return __io_cqring_events(ctx);
}

/*
 * We can't just wait for polled events to come to us, we have to actively
 * find and complete them.
 */
static __cold void io_iopoll_try_reap_events(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_IOPOLL))
		return;

	mutex_lock(&ctx->uring_lock);
	while (!wq_list_empty(&ctx->iopoll_list)) {
		/* let it sleep and repeat later if can't complete a request */
		if (io_do_iopoll(ctx, true) == 0)
			break;
		/*
		 * Ensure we allow local-to-the-cpu processing to take place,
		 * in this case we need to ensure that we reap all events.
		 * Also let task_work, etc. to progress by releasing the mutex
		 */
		if (need_resched()) {
			mutex_unlock(&ctx->uring_lock);
			cond_resched();
			mutex_lock(&ctx->uring_lock);
		}
	}
	mutex_unlock(&ctx->uring_lock);
}

static int io_iopoll_check(struct io_ring_ctx *ctx, long min)
{
	unsigned int nr_events = 0;
	unsigned long check_cq;

	if (!io_allowed_run_tw(ctx))
		return -EEXIST;

	check_cq = READ_ONCE(ctx->check_cq);
	if (unlikely(check_cq)) {
		if (check_cq & BIT(IO_CHECK_CQ_OVERFLOW_BIT))
			__io_cqring_overflow_flush(ctx);
		/*
		 * Similarly do not spin if we have not informed the user of any
		 * dropped CQE.
		 */
		if (check_cq & BIT(IO_CHECK_CQ_DROPPED_BIT))
			return -EBADR;
	}
	/*
	 * Don't enter poll loop if we already have events pending.
	 * If we do, we can potentially be spinning for commands that
	 * already triggered a CQE (eg in error).
	 */
	if (io_cqring_events(ctx))
		return 0;

	do {
		int ret = 0;

		/*
		 * If a submit got punted to a workqueue, we can have the
		 * application entering polling for a command before it gets
		 * issued. That app will hold the uring_lock for the duration
		 * of the poll right here, so we need to take a breather every
		 * now and then to ensure that the issue has a chance to add
		 * the poll to the issued list. Otherwise we can spin here
		 * forever, while the workqueue is stuck trying to acquire the
		 * very same mutex.
		 */
		if (wq_list_empty(&ctx->iopoll_list) ||
		    io_task_work_pending(ctx)) {
			u32 tail = ctx->cached_cq_tail;

			(void) io_run_local_work_locked(ctx);

			if (task_work_pending(current) ||
			    wq_list_empty(&ctx->iopoll_list)) {
				mutex_unlock(&ctx->uring_lock);
				io_run_task_work();
				mutex_lock(&ctx->uring_lock);
			}
			/* some requests don't go through iopoll_list */
			if (tail != ctx->cached_cq_tail ||
			    wq_list_empty(&ctx->iopoll_list))
				break;
		}
		ret = io_do_iopoll(ctx, !min);
		if (unlikely(ret < 0))
			return ret;

		if (task_sigpending(current))
			return -EINTR;
		if (need_resched())
			break;

		nr_events += ret;
	} while (nr_events < min);

	return 0;
}

void io_req_task_complete(struct io_kiocb *req, struct io_tw_state *ts)
{
	if (ts->locked)
		io_req_complete_defer(req);
	else
		io_req_complete_post(req, IO_URING_F_UNLOCKED);
}

/*
 * After the iocb has been issued, it's safe to be found on the poll list.
 * Adding the kiocb to the list AFTER submission ensures that we don't
 * find it from a io_do_iopoll() thread before the issuer is done
 * accessing the kiocb cookie.
 */
static void io_iopoll_req_issued(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	const bool needs_lock = issue_flags & IO_URING_F_UNLOCKED;

	/* workqueue context doesn't hold uring_lock, grab it now */
	if (unlikely(needs_lock))
		mutex_lock(&ctx->uring_lock);

	/*
	 * Track whether we have multiple files in our lists. This will impact
	 * how we do polling eventually, not spinning if we're on potentially
	 * different devices.
	 */
	if (wq_list_empty(&ctx->iopoll_list)) {
		ctx->poll_multi_queue = false;
	} else if (!ctx->poll_multi_queue) {
		struct io_kiocb *list_req;

		list_req = container_of(ctx->iopoll_list.first, struct io_kiocb,
					comp_list);
		if (list_req->file != req->file)
			ctx->poll_multi_queue = true;
	}

	/*
	 * For fast devices, IO may have already completed. If it has, add
	 * it to the front so we find it first.
	 */
	if (READ_ONCE(req->iopoll_completed))
		wq_list_add_head(&req->comp_list, &ctx->iopoll_list);
	else
		wq_list_add_tail(&req->comp_list, &ctx->iopoll_list);

	if (unlikely(needs_lock)) {
		/*
		 * If IORING_SETUP_SQPOLL is enabled, sqes are either handle
		 * in sq thread task context or in io worker task context. If
		 * current task context is sq thread, we don't need to check
		 * whether should wake up sq thread.
		 */
		if ((ctx->flags & IORING_SETUP_SQPOLL) &&
		    wq_has_sleeper(&ctx->sq_data->wait))
			wake_up(&ctx->sq_data->wait);

		mutex_unlock(&ctx->uring_lock);
	}
}

unsigned int io_file_get_flags(struct file *file)
{
	unsigned int res = 0;

	if (S_ISREG(file_inode(file)->i_mode))
		res |= REQ_F_ISREG;
	if ((file->f_flags & O_NONBLOCK) || (file->f_mode & FMODE_NOWAIT))
		res |= REQ_F_SUPPORT_NOWAIT;
	return res;
}

bool io_alloc_async_data(struct io_kiocb *req)
{
	WARN_ON_ONCE(!io_cold_defs[req->opcode].async_size);
	req->async_data = kmalloc(io_cold_defs[req->opcode].async_size, GFP_KERNEL);
	if (req->async_data) {
		req->flags |= REQ_F_ASYNC_DATA;
		return false;
	}
	return true;
}

int io_req_prep_async(struct io_kiocb *req)
{
	const struct io_cold_def *cdef = &io_cold_defs[req->opcode];
	const struct io_issue_def *def = &io_issue_defs[req->opcode];

	/* assign early for deferred execution for non-fixed file */
	if (def->needs_file && !(req->flags & REQ_F_FIXED_FILE) && !req->file)
		req->file = io_file_get_normal(req, req->cqe.fd);
	if (!cdef->prep_async)
		return 0;
	if (WARN_ON_ONCE(req_has_async_data(req)))
		return -EFAULT;
	if (!def->manual_alloc) {
		if (io_alloc_async_data(req))
			return -EAGAIN;
	}
	return cdef->prep_async(req);
}

static u32 io_get_sequence(struct io_kiocb *req)
{
	u32 seq = req->ctx->cached_sq_head;
	struct io_kiocb *cur;

	/* need original cached_sq_head, but it was increased for each req */
	io_for_each_link(cur, req)
		seq--;
	return seq;
}

static __cold void io_drain_req(struct io_kiocb *req)
	__must_hold(&ctx->uring_lock)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_defer_entry *de;
	int ret;
	u32 seq = io_get_sequence(req);

	/* Still need defer if there is pending req in defer list. */
	spin_lock(&ctx->completion_lock);
	if (!req_need_defer(req, seq) && list_empty_careful(&ctx->defer_list)) {
		spin_unlock(&ctx->completion_lock);
queue:
		ctx->drain_active = false;
		io_req_task_queue(req);
		return;
	}
	spin_unlock(&ctx->completion_lock);

	io_prep_async_link(req);
	de = kmalloc(sizeof(*de), GFP_KERNEL);
	if (!de) {
		ret = -ENOMEM;
		io_req_defer_failed(req, ret);
		return;
	}

	spin_lock(&ctx->completion_lock);
	if (!req_need_defer(req, seq) && list_empty(&ctx->defer_list)) {
		spin_unlock(&ctx->completion_lock);
		kfree(de);
		goto queue;
	}

	trace_io_uring_defer(req);
	de->req = req;
	de->seq = seq;
	list_add_tail(&de->list, &ctx->defer_list);
	spin_unlock(&ctx->completion_lock);
}

static bool io_assign_file(struct io_kiocb *req, const struct io_issue_def *def,
			   unsigned int issue_flags)
{
	if (req->file || !def->needs_file)
		return true;

	if (req->flags & REQ_F_FIXED_FILE)
		req->file = io_file_get_fixed(req, req->cqe.fd, issue_flags);
	else
		req->file = io_file_get_normal(req, req->cqe.fd);

	return !!req->file;
}

static int io_issue_sqe(struct io_kiocb *req, unsigned int issue_flags)
{
	const struct io_issue_def *def = &io_issue_defs[req->opcode];
	const struct cred *creds = NULL;
	int ret;

	if (unlikely(!io_assign_file(req, def, issue_flags)))
		return -EBADF;

	if (unlikely((req->flags & REQ_F_CREDS) && req->creds != current_cred()))
		creds = override_creds(req->creds);

	if (!def->audit_skip)
		audit_uring_entry(req->opcode);

	ret = def->issue(req, issue_flags);

	if (!def->audit_skip)
		audit_uring_exit(!ret, ret);

	if (creds)
		revert_creds(creds);

	if (ret == IOU_OK) {
		if (issue_flags & IO_URING_F_COMPLETE_DEFER)
			io_req_complete_defer(req);
		else
			io_req_complete_post(req, issue_flags);

		return 0;
	}

	if (ret == IOU_ISSUE_SKIP_COMPLETE) {
		ret = 0;
		io_arm_ltimeout(req);

		/* If the op doesn't have a file, we're not polling for it */
		if ((req->ctx->flags & IORING_SETUP_IOPOLL) && def->iopoll_queue)
			io_iopoll_req_issued(req, issue_flags);
	}
	return ret;
}

int io_poll_issue(struct io_kiocb *req, struct io_tw_state *ts)
{
	io_tw_lock(req->ctx, ts);
	return io_issue_sqe(req, IO_URING_F_NONBLOCK|IO_URING_F_MULTISHOT|
				 IO_URING_F_COMPLETE_DEFER);
}

struct io_wq_work *io_wq_free_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;

	if (req_ref_put_and_test(req)) {
		if (req->flags & IO_REQ_LINK_FLAGS)
			nxt = io_req_find_next(req);
		io_free_req(req);
	}
	return nxt ? &nxt->work : NULL;
}

void io_wq_submit_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	const struct io_issue_def *def = &io_issue_defs[req->opcode];
	unsigned int issue_flags = IO_URING_F_UNLOCKED | IO_URING_F_IOWQ;
	bool needs_poll = false;
	int ret = 0, err = -ECANCELED;

	/* one will be dropped by ->io_wq_free_work() after returning to io-wq */
	if (!(req->flags & REQ_F_REFCOUNT))
		__io_req_set_refcount(req, 2);
	else
		req_ref_get(req);

	io_arm_ltimeout(req);

	/* either cancelled or io-wq is dying, so don't touch tctx->iowq */
	if (work->flags & IO_WQ_WORK_CANCEL) {
fail:
		io_req_task_queue_fail(req, err);
		return;
	}
	if (!io_assign_file(req, def, issue_flags)) {
		err = -EBADF;
		work->flags |= IO_WQ_WORK_CANCEL;
		goto fail;
	}

	if (req->flags & REQ_F_FORCE_ASYNC) {
		bool opcode_poll = def->pollin || def->pollout;

		if (opcode_poll && file_can_poll(req->file)) {
			needs_poll = true;
			issue_flags |= IO_URING_F_NONBLOCK;
		}
	}

	do {
		ret = io_issue_sqe(req, issue_flags);
		if (ret != -EAGAIN)
			break;

		/*
		 * If REQ_F_NOWAIT is set, then don't wait or retry with
		 * poll. -EAGAIN is final for that case.
		 */
		if (req->flags & REQ_F_NOWAIT)
			break;

		/*
		 * We can get EAGAIN for iopolled IO even though we're
		 * forcing a sync submission from here, since we can't
		 * wait for request slots on the block side.
		 */
		if (!needs_poll) {
			if (!(req->ctx->flags & IORING_SETUP_IOPOLL))
				break;
			if (io_wq_worker_stopped())
				break;
			cond_resched();
			continue;
		}

		if (io_arm_poll_handler(req, issue_flags) == IO_APOLL_OK)
			return;
		/* aborted or ready, in either case retry blocking */
		needs_poll = false;
		issue_flags &= ~IO_URING_F_NONBLOCK;
	} while (1);

	/* avoid locking problems by failing it from a clean context */
	if (ret < 0)
		io_req_task_queue_fail(req, ret);
}

inline struct file *io_file_get_fixed(struct io_kiocb *req, int fd,
				      unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_fixed_file *slot;
	struct file *file = NULL;

	io_ring_submit_lock(ctx, issue_flags);

	if (unlikely((unsigned int)fd >= ctx->nr_user_files))
		goto out;
	fd = array_index_nospec(fd, ctx->nr_user_files);
	slot = io_fixed_file_slot(&ctx->file_table, fd);
	file = io_slot_file(slot);
	req->flags |= io_slot_flags(slot);
	io_req_set_rsrc_node(req, ctx, 0);
out:
	io_ring_submit_unlock(ctx, issue_flags);
	return file;
}

struct file *io_file_get_normal(struct io_kiocb *req, int fd)
{
	struct file *file = fget(fd);

	trace_io_uring_file_get(req, fd);

	/* we don't allow fixed io_uring files */
	if (file && io_is_uring_fops(file))
		io_req_track_inflight(req);
	return file;
}

static void io_queue_async(struct io_kiocb *req, int ret)
	__must_hold(&req->ctx->uring_lock)
{
	struct io_kiocb *linked_timeout;

	if (ret != -EAGAIN || (req->flags & REQ_F_NOWAIT)) {
		io_req_defer_failed(req, ret);
		return;
	}

	linked_timeout = io_prep_linked_timeout(req);

	switch (io_arm_poll_handler(req, 0)) {
	case IO_APOLL_READY:
		io_kbuf_recycle(req, 0);
		io_req_task_queue(req);
		break;
	case IO_APOLL_ABORTED:
		io_kbuf_recycle(req, 0);
		io_queue_iowq(req, NULL);
		break;
	case IO_APOLL_OK:
		break;
	}

	if (linked_timeout)
		io_queue_linked_timeout(linked_timeout);
}

static inline void io_queue_sqe(struct io_kiocb *req)
	__must_hold(&req->ctx->uring_lock)
{
	int ret;

	ret = io_issue_sqe(req, IO_URING_F_NONBLOCK|IO_URING_F_COMPLETE_DEFER);

	/*
	 * We async punt it if the file wasn't marked NOWAIT, or if the file
	 * doesn't support non-blocking read/write attempts
	 */
	if (unlikely(ret))
		io_queue_async(req, ret);
}

static void io_queue_sqe_fallback(struct io_kiocb *req)
	__must_hold(&req->ctx->uring_lock)
{
	if (unlikely(req->flags & REQ_F_FAIL)) {
		/*
		 * We don't submit, fail them all, for that replace hardlinks
		 * with normal links. Extra REQ_F_LINK is tolerated.
		 */
		req->flags &= ~REQ_F_HARDLINK;
		req->flags |= REQ_F_LINK;
		io_req_defer_failed(req, req->cqe.res);
	} else {
		int ret = io_req_prep_async(req);

		if (unlikely(ret)) {
			io_req_defer_failed(req, ret);
			return;
		}

		if (unlikely(req->ctx->drain_active))
			io_drain_req(req);
		else
			io_queue_iowq(req, NULL);
	}
}

/*
 * Check SQE restrictions (opcode and flags).
 *
 * Returns 'true' if SQE is allowed, 'false' otherwise.
 */
static inline bool io_check_restriction(struct io_ring_ctx *ctx,
					struct io_kiocb *req,
					unsigned int sqe_flags)
{
	if (!test_bit(req->opcode, ctx->restrictions.sqe_op))
		return false;

	if ((sqe_flags & ctx->restrictions.sqe_flags_required) !=
	    ctx->restrictions.sqe_flags_required)
		return false;

	if (sqe_flags & ~(ctx->restrictions.sqe_flags_allowed |
			  ctx->restrictions.sqe_flags_required))
		return false;

	return true;
}

static void io_init_req_drain(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *head = ctx->submit_state.link.head;

	ctx->drain_active = true;
	if (head) {
		/*
		 * If we need to drain a request in the middle of a link, drain
		 * the head request and the next request/link after the current
		 * link. Considering sequential execution of links,
		 * REQ_F_IO_DRAIN will be maintained for every request of our
		 * link.
		 */
		head->flags |= REQ_F_IO_DRAIN | REQ_F_FORCE_ASYNC;
		ctx->drain_next = true;
	}
}

static int io_init_req(struct io_ring_ctx *ctx, struct io_kiocb *req,
		       const struct io_uring_sqe *sqe)
	__must_hold(&ctx->uring_lock)
{
	const struct io_issue_def *def;
	unsigned int sqe_flags;
	int personality;
	u8 opcode;

	/* req is partially pre-initialised, see io_preinit_req() */
	req->opcode = opcode = READ_ONCE(sqe->opcode);
	/* same numerical values with corresponding REQ_F_*, safe to copy */
	req->flags = sqe_flags = READ_ONCE(sqe->flags);
	req->cqe.user_data = READ_ONCE(sqe->user_data);
	req->file = NULL;
	req->rsrc_node = NULL;
	req->task = current;

	if (unlikely(opcode >= IORING_OP_LAST)) {
		req->opcode = 0;
		return -EINVAL;
	}
	def = &io_issue_defs[opcode];
	if (unlikely(sqe_flags & ~SQE_COMMON_FLAGS)) {
		/* enforce forwards compatibility on users */
		if (sqe_flags & ~SQE_VALID_FLAGS)
			return -EINVAL;
		if (sqe_flags & IOSQE_BUFFER_SELECT) {
			if (!def->buffer_select)
				return -EOPNOTSUPP;
			req->buf_index = READ_ONCE(sqe->buf_group);
		}
		if (sqe_flags & IOSQE_CQE_SKIP_SUCCESS)
			ctx->drain_disabled = true;
		if (sqe_flags & IOSQE_IO_DRAIN) {
			if (ctx->drain_disabled)
				return -EOPNOTSUPP;
			io_init_req_drain(req);
		}
	}
	if (unlikely(ctx->restricted || ctx->drain_active || ctx->drain_next)) {
		if (ctx->restricted && !io_check_restriction(ctx, req, sqe_flags))
			return -EACCES;
		/* knock it to the slow queue path, will be drained there */
		if (ctx->drain_active)
			req->flags |= REQ_F_FORCE_ASYNC;
		/* if there is no link, we're at "next" request and need to drain */
		if (unlikely(ctx->drain_next) && !ctx->submit_state.link.head) {
			ctx->drain_next = false;
			ctx->drain_active = true;
			req->flags |= REQ_F_IO_DRAIN | REQ_F_FORCE_ASYNC;
		}
	}

	if (!def->ioprio && sqe->ioprio)
		return -EINVAL;
	if (!def->iopoll && (ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	if (def->needs_file) {
		struct io_submit_state *state = &ctx->submit_state;

		req->cqe.fd = READ_ONCE(sqe->fd);

		/*
		 * Plug now if we have more than 2 IO left after this, and the
		 * target is potentially a read/write to block based storage.
		 */
		if (state->need_plug && def->plug) {
			state->plug_started = true;
			state->need_plug = false;
			blk_start_plug_nr_ios(&state->plug, state->submit_nr);
		}
	}

	personality = READ_ONCE(sqe->personality);
	if (personality) {
		int ret;

		req->creds = xa_load(&ctx->personalities, personality);
		if (!req->creds)
			return -EINVAL;
		get_cred(req->creds);
		ret = security_uring_override_creds(req->creds);
		if (ret) {
			put_cred(req->creds);
			return ret;
		}
		req->flags |= REQ_F_CREDS;
	}

	return def->prep(req, sqe);
}

static __cold int io_submit_fail_init(const struct io_uring_sqe *sqe,
				      struct io_kiocb *req, int ret)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_submit_link *link = &ctx->submit_state.link;
	struct io_kiocb *head = link->head;

	trace_io_uring_req_failed(sqe, req, ret);

	/*
	 * Avoid breaking links in the middle as it renders links with SQPOLL
	 * unusable. Instead of failing eagerly, continue assembling the link if
	 * applicable and mark the head with REQ_F_FAIL. The link flushing code
	 * should find the flag and handle the rest.
	 */
	req_fail_link_node(req, ret);
	if (head && !(head->flags & REQ_F_FAIL))
		req_fail_link_node(head, -ECANCELED);

	if (!(req->flags & IO_REQ_LINK_FLAGS)) {
		if (head) {
			link->last->link = req;
			link->head = NULL;
			req = head;
		}
		io_queue_sqe_fallback(req);
		return ret;
	}

	if (head)
		link->last->link = req;
	else
		link->head = req;
	link->last = req;
	return 0;
}

static inline int io_submit_sqe(struct io_ring_ctx *ctx, struct io_kiocb *req,
			 const struct io_uring_sqe *sqe)
	__must_hold(&ctx->uring_lock)
{
	struct io_submit_link *link = &ctx->submit_state.link;
	int ret;

	ret = io_init_req(ctx, req, sqe);
	if (unlikely(ret))
		return io_submit_fail_init(sqe, req, ret);

	trace_io_uring_submit_req(req);

	/*
	 * If we already have a head request, queue this one for async
	 * submittal once the head completes. If we don't have a head but
	 * IOSQE_IO_LINK is set in the sqe, start a new head. This one will be
	 * submitted sync once the chain is complete. If none of those
	 * conditions are true (normal request), then just queue it.
	 */
	if (unlikely(link->head)) {
		ret = io_req_prep_async(req);
		if (unlikely(ret))
			return io_submit_fail_init(sqe, req, ret);

		trace_io_uring_link(req, link->head);
		link->last->link = req;
		link->last = req;

		if (req->flags & IO_REQ_LINK_FLAGS)
			return 0;
		/* last request of the link, flush it */
		req = link->head;
		link->head = NULL;
		if (req->flags & (REQ_F_FORCE_ASYNC | REQ_F_FAIL))
			goto fallback;

	} else if (unlikely(req->flags & (IO_REQ_LINK_FLAGS |
					  REQ_F_FORCE_ASYNC | REQ_F_FAIL))) {
		if (req->flags & IO_REQ_LINK_FLAGS) {
			link->head = req;
			link->last = req;
		} else {
fallback:
			io_queue_sqe_fallback(req);
		}
		return 0;
	}

	io_queue_sqe(req);
	return 0;
}

/*
 * Batched submission is done, ensure local IO is flushed out.
 */
static void io_submit_state_end(struct io_ring_ctx *ctx)
{
	struct io_submit_state *state = &ctx->submit_state;

	if (unlikely(state->link.head))
		io_queue_sqe_fallback(state->link.head);
	/* flush only after queuing links as they can generate completions */
	io_submit_flush_completions(ctx);
	if (state->plug_started)
		blk_finish_plug(&state->plug);
}

/*
 * Start submission side cache.
 */
static void io_submit_state_start(struct io_submit_state *state,
				  unsigned int max_ios)
{
	state->plug_started = false;
	state->need_plug = max_ios > 2;
	state->submit_nr = max_ios;
	/* set only head, no need to init link_last in advance */
	state->link.head = NULL;
}

static void io_commit_sqring(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;

	/*
	 * Ensure any loads from the SQEs are done at this point,
	 * since once we write the new head, the application could
	 * write new data to them.
	 */
	smp_store_release(&rings->sq.head, ctx->cached_sq_head);
}

/*
 * Fetch an sqe, if one is available. Note this returns a pointer to memory
 * that is mapped by userspace. This means that care needs to be taken to
 * ensure that reads are stable, as we cannot rely on userspace always
 * being a good citizen. If members of the sqe are validated and then later
 * used, it's important that those reads are done through READ_ONCE() to
 * prevent a re-load down the line.
 */
static bool io_get_sqe(struct io_ring_ctx *ctx, const struct io_uring_sqe **sqe)
{
	unsigned mask = ctx->sq_entries - 1;
	unsigned head = ctx->cached_sq_head++ & mask;

	if (!(ctx->flags & IORING_SETUP_NO_SQARRAY)) {
		head = READ_ONCE(ctx->sq_array[head]);
		if (unlikely(head >= ctx->sq_entries)) {
			/* drop invalid entries */
			spin_lock(&ctx->completion_lock);
			ctx->cq_extra--;
			spin_unlock(&ctx->completion_lock);
			WRITE_ONCE(ctx->rings->sq_dropped,
				   READ_ONCE(ctx->rings->sq_dropped) + 1);
			return false;
		}
	}

	/*
	 * The cached sq head (or cq tail) serves two purposes:
	 *
	 * 1) allows us to batch the cost of updating the user visible
	 *    head updates.
	 * 2) allows the kernel side to track the head on its own, even
	 *    though the application is the one updating it.
	 */

	/* double index for 128-byte SQEs, twice as long */
	if (ctx->flags & IORING_SETUP_SQE128)
		head <<= 1;
	*sqe = &ctx->sq_sqes[head];
	return true;
}

int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr)
	__must_hold(&ctx->uring_lock)
{
	unsigned int entries = io_sqring_entries(ctx);
	unsigned int left;
	int ret;

	if (unlikely(!entries))
		return 0;
	/* make sure SQ entry isn't read before tail */
	ret = left = min(nr, entries);
	io_get_task_refs(left);
	io_submit_state_start(&ctx->submit_state, left);

	do {
		const struct io_uring_sqe *sqe;
		struct io_kiocb *req;

		if (unlikely(!io_alloc_req(ctx, &req)))
			break;
		if (unlikely(!io_get_sqe(ctx, &sqe))) {
			io_req_add_to_cache(req, ctx);
			break;
		}

		/*
		 * Continue submitting even for sqe failure if the
		 * ring was setup with IORING_SETUP_SUBMIT_ALL
		 */
		if (unlikely(io_submit_sqe(ctx, req, sqe)) &&
		    !(ctx->flags & IORING_SETUP_SUBMIT_ALL)) {
			left--;
			break;
		}
	} while (--left);

	if (unlikely(left)) {
		ret -= left;
		/* try again if it submitted nothing and can't allocate a req */
		if (!ret && io_req_cache_empty(ctx))
			ret = -EAGAIN;
		current->io_uring->cached_refs += left;
	}

	io_submit_state_end(ctx);
	 /* Commit SQ ring head once we've consumed and submitted all SQEs */
	io_commit_sqring(ctx);
	return ret;
}

struct io_wait_queue {
	struct wait_queue_entry wq;
	struct io_ring_ctx *ctx;
	unsigned cq_tail;
	unsigned nr_timeouts;
	ktime_t timeout;
};

static inline bool io_has_work(struct io_ring_ctx *ctx)
{
	return test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq) ||
	       !llist_empty(&ctx->work_llist);
}

static inline bool io_should_wake(struct io_wait_queue *iowq)
{
	struct io_ring_ctx *ctx = iowq->ctx;
	int dist = READ_ONCE(ctx->rings->cq.tail) - (int) iowq->cq_tail;

	/*
	 * Wake up if we have enough events, or if a timeout occurred since we
	 * started waiting. For timeouts, we always want to return to userspace,
	 * regardless of event count.
	 */
	return dist >= 0 || atomic_read(&ctx->cq_timeouts) != iowq->nr_timeouts;
}

static int io_wake_function(struct wait_queue_entry *curr, unsigned int mode,
			    int wake_flags, void *key)
{
	struct io_wait_queue *iowq = container_of(curr, struct io_wait_queue, wq);

	/*
	 * Cannot safely flush overflowed CQEs from here, ensure we wake up
	 * the task, and the next invocation will do it.
	 */
	if (io_should_wake(iowq) || io_has_work(iowq->ctx))
		return autoremove_wake_function(curr, mode, wake_flags, key);
	return -1;
}

int io_run_task_work_sig(struct io_ring_ctx *ctx)
{
	if (!llist_empty(&ctx->work_llist)) {
		__set_current_state(TASK_RUNNING);
		if (io_run_local_work(ctx) > 0)
			return 0;
	}
	if (io_run_task_work() > 0)
		return 0;
	if (task_sigpending(current))
		return -EINTR;
	return 0;
}

static bool current_pending_io(void)
{
	struct io_uring_task *tctx = current->io_uring;

	if (!tctx)
		return false;
	return percpu_counter_read_positive(&tctx->inflight);
}

/* when returns >0, the caller should retry */
static inline int io_cqring_wait_schedule(struct io_ring_ctx *ctx,
					  struct io_wait_queue *iowq)
{
	int io_wait, ret;

	if (unlikely(READ_ONCE(ctx->check_cq)))
		return 1;
	if (unlikely(!llist_empty(&ctx->work_llist)))
		return 1;
	if (unlikely(test_thread_flag(TIF_NOTIFY_SIGNAL)))
		return 1;
	if (unlikely(task_sigpending(current)))
		return -EINTR;
	if (unlikely(io_should_wake(iowq)))
		return 0;

	/*
	 * Mark us as being in io_wait if we have pending requests, so cpufreq
	 * can take into account that the task is waiting for IO - turns out
	 * to be important for low QD IO.
	 */
	io_wait = current->in_iowait;
	if (current_pending_io())
		current->in_iowait = 1;
	ret = 0;
	if (iowq->timeout == KTIME_MAX)
		schedule();
	else if (!schedule_hrtimeout(&iowq->timeout, HRTIMER_MODE_ABS))
		ret = -ETIME;
	current->in_iowait = io_wait;
	return ret;
}

/*
 * Wait until events become available, if we don't already have some. The
 * application must reap them itself, as they reside on the shared cq ring.
 */
static int io_cqring_wait(struct io_ring_ctx *ctx, int min_events,
			  const sigset_t __user *sig, size_t sigsz,
			  struct __kernel_timespec __user *uts)
{
	struct io_wait_queue iowq;
	struct io_rings *rings = ctx->rings;
	int ret;

	if (!io_allowed_run_tw(ctx))
		return -EEXIST;
	if (!llist_empty(&ctx->work_llist))
		io_run_local_work(ctx);
	io_run_task_work();
	io_cqring_overflow_flush(ctx);
	/* if user messes with these they will just get an early return */
	if (__io_cqring_events_user(ctx) >= min_events)
		return 0;

	if (sig) {
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			ret = set_compat_user_sigmask((const compat_sigset_t __user *)sig,
						      sigsz);
		else
#endif
			ret = set_user_sigmask(sig, sigsz);

		if (ret)
			return ret;
	}

	init_waitqueue_func_entry(&iowq.wq, io_wake_function);
	iowq.wq.private = current;
	INIT_LIST_HEAD(&iowq.wq.entry);
	iowq.ctx = ctx;
	iowq.nr_timeouts = atomic_read(&ctx->cq_timeouts);
	iowq.cq_tail = READ_ONCE(ctx->rings->cq.head) + min_events;
	iowq.timeout = KTIME_MAX;

	if (uts) {
		struct timespec64 ts;

		if (get_timespec64(&ts, uts))
			return -EFAULT;
		iowq.timeout = ktime_add_ns(timespec64_to_ktime(ts), ktime_get_ns());
	}

	trace_io_uring_cqring_wait(ctx, min_events);
	do {
		unsigned long check_cq;

		if (ctx->flags & IORING_SETUP_DEFER_TASKRUN) {
			int nr_wait = (int) iowq.cq_tail - READ_ONCE(ctx->rings->cq.tail);

			atomic_set(&ctx->cq_wait_nr, nr_wait);
			set_current_state(TASK_INTERRUPTIBLE);
		} else {
			prepare_to_wait_exclusive(&ctx->cq_wait, &iowq.wq,
							TASK_INTERRUPTIBLE);
		}

		ret = io_cqring_wait_schedule(ctx, &iowq);
		__set_current_state(TASK_RUNNING);
		atomic_set(&ctx->cq_wait_nr, 0);

		/*
		 * Run task_work after scheduling and before io_should_wake().
		 * If we got woken because of task_work being processed, run it
		 * now rather than let the caller do another wait loop.
		 */
		io_run_task_work();
		if (!llist_empty(&ctx->work_llist))
			io_run_local_work(ctx);

		/*
		 * Non-local task_work will be run on exit to userspace, but
		 * if we're using DEFER_TASKRUN, then we could have waited
		 * with a timeout for a number of requests. If the timeout
		 * hits, we could have some requests ready to process. Ensure
		 * this break is _after_ we have run task_work, to avoid
		 * deferring running potentially pending requests until the
		 * next time we wait for events.
		 */
		if (ret < 0)
			break;

		check_cq = READ_ONCE(ctx->check_cq);
		if (unlikely(check_cq)) {
			/* let the caller flush overflows, retry */
			if (check_cq & BIT(IO_CHECK_CQ_OVERFLOW_BIT))
				io_cqring_do_overflow_flush(ctx);
			if (check_cq & BIT(IO_CHECK_CQ_DROPPED_BIT)) {
				ret = -EBADR;
				break;
			}
		}

		if (io_should_wake(&iowq)) {
			ret = 0;
			break;
		}
		cond_resched();
	} while (1);

	if (!(ctx->flags & IORING_SETUP_DEFER_TASKRUN))
		finish_wait(&ctx->cq_wait, &iowq.wq);
	restore_saved_sigmask_unless(ret == -EINTR);

	return READ_ONCE(rings->cq.head) == READ_ONCE(rings->cq.tail) ? ret : 0;
}

void io_mem_free(void *ptr)
{
	if (!ptr)
		return;

	folio_put(virt_to_folio(ptr));
}

static void io_pages_free(struct page ***pages, int npages)
{
	struct page **page_array;
	int i;

	if (!pages)
		return;

	page_array = *pages;
	if (!page_array)
		return;

	for (i = 0; i < npages; i++)
		unpin_user_page(page_array[i]);
	kvfree(page_array);
	*pages = NULL;
}

static void *__io_uaddr_map(struct page ***pages, unsigned short *npages,
			    unsigned long uaddr, size_t size)
{
	struct page **page_array;
	unsigned int nr_pages;
	void *page_addr;
	int ret, i;

	*npages = 0;

	if (uaddr & (PAGE_SIZE - 1) || !size)
		return ERR_PTR(-EINVAL);

	nr_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (nr_pages > USHRT_MAX)
		return ERR_PTR(-EINVAL);
	page_array = kvmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!page_array)
		return ERR_PTR(-ENOMEM);

	ret = pin_user_pages_fast(uaddr, nr_pages, FOLL_WRITE | FOLL_LONGTERM,
					page_array);
	if (ret != nr_pages) {
err:
		io_pages_free(&page_array, ret > 0 ? ret : 0);
		return ret < 0 ? ERR_PTR(ret) : ERR_PTR(-EFAULT);
	}

	page_addr = page_address(page_array[0]);
	for (i = 0; i < nr_pages; i++) {
		ret = -EINVAL;

		/*
		 * Can't support mapping user allocated ring memory on 32-bit
		 * archs where it could potentially reside in highmem. Just
		 * fail those with -EINVAL, just like we did on kernels that
		 * didn't support this feature.
		 */
		if (PageHighMem(page_array[i]))
			goto err;

		/*
		 * No support for discontig pages for now, should either be a
		 * single normal page, or a huge page. Later on we can add
		 * support for remapping discontig pages, for now we will
		 * just fail them with EINVAL.
		 */
		if (page_address(page_array[i]) != page_addr)
			goto err;
		page_addr += PAGE_SIZE;
	}

	*pages = page_array;
	*npages = nr_pages;
	return page_to_virt(page_array[0]);
}

static void *io_rings_map(struct io_ring_ctx *ctx, unsigned long uaddr,
			  size_t size)
{
	return __io_uaddr_map(&ctx->ring_pages, &ctx->n_ring_pages, uaddr,
				size);
}

static void *io_sqes_map(struct io_ring_ctx *ctx, unsigned long uaddr,
			 size_t size)
{
	return __io_uaddr_map(&ctx->sqe_pages, &ctx->n_sqe_pages, uaddr,
				size);
}

static void io_rings_free(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_NO_MMAP)) {
		io_mem_free(ctx->rings);
		io_mem_free(ctx->sq_sqes);
		ctx->rings = NULL;
		ctx->sq_sqes = NULL;
	} else {
		io_pages_free(&ctx->ring_pages, ctx->n_ring_pages);
		ctx->n_ring_pages = 0;
		io_pages_free(&ctx->sqe_pages, ctx->n_sqe_pages);
		ctx->n_sqe_pages = 0;
	}
}

void *io_mem_alloc(size_t size)
{
	gfp_t gfp = GFP_KERNEL_ACCOUNT | __GFP_ZERO | __GFP_NOWARN | __GFP_COMP;
	void *ret;

	ret = (void *) __get_free_pages(gfp, get_order(size));
	if (ret)
		return ret;
	return ERR_PTR(-ENOMEM);
}

static unsigned long rings_size(struct io_ring_ctx *ctx, unsigned int sq_entries,
				unsigned int cq_entries, size_t *sq_offset)
{
	struct io_rings *rings;
	size_t off, sq_array_size;

	off = struct_size(rings, cqes, cq_entries);
	if (off == SIZE_MAX)
		return SIZE_MAX;
	if (ctx->flags & IORING_SETUP_CQE32) {
		if (check_shl_overflow(off, 1, &off))
			return SIZE_MAX;
	}

#ifdef CONFIG_SMP
	off = ALIGN(off, SMP_CACHE_BYTES);
	if (off == 0)
		return SIZE_MAX;
#endif

	if (ctx->flags & IORING_SETUP_NO_SQARRAY) {
		if (sq_offset)
			*sq_offset = SIZE_MAX;
		return off;
	}

	if (sq_offset)
		*sq_offset = off;

	sq_array_size = array_size(sizeof(u32), sq_entries);
	if (sq_array_size == SIZE_MAX)
		return SIZE_MAX;

	if (check_add_overflow(off, sq_array_size, &off))
		return SIZE_MAX;

	return off;
}

static void io_req_caches_free(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;
	int nr = 0;

	mutex_lock(&ctx->uring_lock);
	io_flush_cached_locked_reqs(ctx, &ctx->submit_state);

	while (!io_req_cache_empty(ctx)) {
		req = io_extract_req(ctx);
		kmem_cache_free(req_cachep, req);
		nr++;
	}
	if (nr)
		percpu_ref_put_many(&ctx->refs, nr);
	mutex_unlock(&ctx->uring_lock);
}

static void io_rsrc_node_cache_free(struct io_cache_entry *entry)
{
	kfree(container_of(entry, struct io_rsrc_node, cache));
}

static __cold void io_ring_ctx_free(struct io_ring_ctx *ctx)
{
	io_sq_thread_finish(ctx);
	/* __io_rsrc_put_work() may need uring_lock to progress, wait w/o it */
	if (WARN_ON_ONCE(!list_empty(&ctx->rsrc_ref_list)))
		return;

	mutex_lock(&ctx->uring_lock);
	if (ctx->buf_data)
		__io_sqe_buffers_unregister(ctx);
	if (ctx->file_data)
		__io_sqe_files_unregister(ctx);
	io_cqring_overflow_kill(ctx);
	io_eventfd_unregister(ctx);
	io_alloc_cache_free(&ctx->apoll_cache, io_apoll_cache_free);
	io_alloc_cache_free(&ctx->netmsg_cache, io_netmsg_cache_free);
	io_futex_cache_free(ctx);
	io_destroy_buffers(ctx);
	mutex_unlock(&ctx->uring_lock);
	if (ctx->sq_creds)
		put_cred(ctx->sq_creds);
	if (ctx->submitter_task)
		put_task_struct(ctx->submitter_task);

	/* there are no registered resources left, nobody uses it */
	if (ctx->rsrc_node)
		io_rsrc_node_destroy(ctx, ctx->rsrc_node);

	WARN_ON_ONCE(!list_empty(&ctx->rsrc_ref_list));
	WARN_ON_ONCE(!list_empty(&ctx->ltimeout_list));

	io_alloc_cache_free(&ctx->rsrc_node_cache, io_rsrc_node_cache_free);
	if (ctx->mm_account) {
		mmdrop(ctx->mm_account);
		ctx->mm_account = NULL;
	}
	io_rings_free(ctx);
	io_kbuf_mmap_list_free(ctx);

	percpu_ref_exit(&ctx->refs);
	free_uid(ctx->user);
	io_req_caches_free(ctx);
	if (ctx->hash_map)
		io_wq_put_hash(ctx->hash_map);
	kfree(ctx->cancel_table.hbs);
	kfree(ctx->cancel_table_locked.hbs);
	kfree(ctx->io_bl);
	xa_destroy(&ctx->io_bl_xa);
	kfree(ctx);
}

static __cold void io_activate_pollwq_cb(struct callback_head *cb)
{
	struct io_ring_ctx *ctx = container_of(cb, struct io_ring_ctx,
					       poll_wq_task_work);

	mutex_lock(&ctx->uring_lock);
	ctx->poll_activated = true;
	mutex_unlock(&ctx->uring_lock);

	/*
	 * Wake ups for some events between start of polling and activation
	 * might've been lost due to loose synchronisation.
	 */
	wake_up_all(&ctx->poll_wq);
	percpu_ref_put(&ctx->refs);
}

__cold void io_activate_pollwq(struct io_ring_ctx *ctx)
{
	spin_lock(&ctx->completion_lock);
	/* already activated or in progress */
	if (ctx->poll_activated || ctx->poll_wq_task_work.func)
		goto out;
	if (WARN_ON_ONCE(!ctx->task_complete))
		goto out;
	if (!ctx->submitter_task)
		goto out;
	/*
	 * with ->submitter_task only the submitter task completes requests, we
	 * only need to sync with it, which is done by injecting a tw
	 */
	init_task_work(&ctx->poll_wq_task_work, io_activate_pollwq_cb);
	percpu_ref_get(&ctx->refs);
	if (task_work_add(ctx->submitter_task, &ctx->poll_wq_task_work, TWA_SIGNAL))
		percpu_ref_put(&ctx->refs);
out:
	spin_unlock(&ctx->completion_lock);
}

static __poll_t io_uring_poll(struct file *file, poll_table *wait)
{
	struct io_ring_ctx *ctx = file->private_data;
	__poll_t mask = 0;

	if (unlikely(!ctx->poll_activated))
		io_activate_pollwq(ctx);

	poll_wait(file, &ctx->poll_wq, wait);
	/*
	 * synchronizes with barrier from wq_has_sleeper call in
	 * io_commit_cqring
	 */
	smp_rmb();
	if (!io_sqring_full(ctx))
		mask |= EPOLLOUT | EPOLLWRNORM;

	/*
	 * Don't flush cqring overflow list here, just do a simple check.
	 * Otherwise there could possible be ABBA deadlock:
	 *      CPU0                    CPU1
	 *      ----                    ----
	 * lock(&ctx->uring_lock);
	 *                              lock(&ep->mtx);
	 *                              lock(&ctx->uring_lock);
	 * lock(&ep->mtx);
	 *
	 * Users may get EPOLLIN meanwhile seeing nothing in cqring, this
	 * pushes them to do the flush.
	 */

	if (__io_cqring_events_user(ctx) || io_has_work(ctx))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

struct io_tctx_exit {
	struct callback_head		task_work;
	struct completion		completion;
	struct io_ring_ctx		*ctx;
};

static __cold void io_tctx_exit_cb(struct callback_head *cb)
{
	struct io_uring_task *tctx = current->io_uring;
	struct io_tctx_exit *work;

	work = container_of(cb, struct io_tctx_exit, task_work);
	/*
	 * When @in_cancel, we're in cancellation and it's racy to remove the
	 * node. It'll be removed by the end of cancellation, just ignore it.
	 * tctx can be NULL if the queueing of this task_work raced with
	 * work cancelation off the exec path.
	 */
	if (tctx && !atomic_read(&tctx->in_cancel))
		io_uring_del_tctx_node((unsigned long)work->ctx);
	complete(&work->completion);
}

static __cold bool io_cancel_ctx_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	return req->ctx == data;
}

static __cold void io_ring_exit_work(struct work_struct *work)
{
	struct io_ring_ctx *ctx = container_of(work, struct io_ring_ctx, exit_work);
	unsigned long timeout = jiffies + HZ * 60 * 5;
	unsigned long interval = HZ / 20;
	struct io_tctx_exit exit;
	struct io_tctx_node *node;
	int ret;

	/*
	 * If we're doing polled IO and end up having requests being
	 * submitted async (out-of-line), then completions can come in while
	 * we're waiting for refs to drop. We need to reap these manually,
	 * as nobody else will be looking for them.
	 */
	do {
		if (test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq)) {
			mutex_lock(&ctx->uring_lock);
			io_cqring_overflow_kill(ctx);
			mutex_unlock(&ctx->uring_lock);
		}

		if (ctx->flags & IORING_SETUP_DEFER_TASKRUN)
			io_move_task_work_from_local(ctx);

		while (io_uring_try_cancel_requests(ctx, NULL, true))
			cond_resched();

		if (ctx->sq_data) {
			struct io_sq_data *sqd = ctx->sq_data;
			struct task_struct *tsk;

			io_sq_thread_park(sqd);
			tsk = sqd->thread;
			if (tsk && tsk->io_uring && tsk->io_uring->io_wq)
				io_wq_cancel_cb(tsk->io_uring->io_wq,
						io_cancel_ctx_cb, ctx, true);
			io_sq_thread_unpark(sqd);
		}

		io_req_caches_free(ctx);

		if (WARN_ON_ONCE(time_after(jiffies, timeout))) {
			/* there is little hope left, don't run it too often */
			interval = HZ * 60;
		}
		/*
		 * This is really an uninterruptible wait, as it has to be
		 * complete. But it's also run from a kworker, which doesn't
		 * take signals, so it's fine to make it interruptible. This
		 * avoids scenarios where we knowingly can wait much longer
		 * on completions, for example if someone does a SIGSTOP on
		 * a task that needs to finish task_work to make this loop
		 * complete. That's a synthetic situation that should not
		 * cause a stuck task backtrace, and hence a potential panic
		 * on stuck tasks if that is enabled.
		 */
	} while (!wait_for_completion_interruptible_timeout(&ctx->ref_comp, interval));

	init_completion(&exit.completion);
	init_task_work(&exit.task_work, io_tctx_exit_cb);
	exit.ctx = ctx;

	mutex_lock(&ctx->uring_lock);
	while (!list_empty(&ctx->tctx_list)) {
		WARN_ON_ONCE(time_after(jiffies, timeout));

		node = list_first_entry(&ctx->tctx_list, struct io_tctx_node,
					ctx_node);
		/* don't spin on a single task if cancellation failed */
		list_rotate_left(&ctx->tctx_list);
		ret = task_work_add(node->task, &exit.task_work, TWA_SIGNAL);
		if (WARN_ON_ONCE(ret))
			continue;

		mutex_unlock(&ctx->uring_lock);
		/*
		 * See comment above for
		 * wait_for_completion_interruptible_timeout() on why this
		 * wait is marked as interruptible.
		 */
		wait_for_completion_interruptible(&exit.completion);
		mutex_lock(&ctx->uring_lock);
	}
	mutex_unlock(&ctx->uring_lock);
	spin_lock(&ctx->completion_lock);
	spin_unlock(&ctx->completion_lock);

	/* pairs with RCU read section in io_req_local_work_add() */
	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN)
		synchronize_rcu();

	io_ring_ctx_free(ctx);
}

static __cold void io_ring_ctx_wait_and_kill(struct io_ring_ctx *ctx)
{
	unsigned long index;
	struct creds *creds;

	mutex_lock(&ctx->uring_lock);
	percpu_ref_kill(&ctx->refs);
	xa_for_each(&ctx->personalities, index, creds)
		io_unregister_personality(ctx, index);
	if (ctx->rings)
		io_poll_remove_all(ctx, NULL, true);
	mutex_unlock(&ctx->uring_lock);

	/*
	 * If we failed setting up the ctx, we might not have any rings
	 * and therefore did not submit any requests
	 */
	if (ctx->rings)
		io_kill_timeouts(ctx, NULL, true);

	flush_delayed_work(&ctx->fallback_work);

	INIT_WORK(&ctx->exit_work, io_ring_exit_work);
	/*
	 * Use system_unbound_wq to avoid spawning tons of event kworkers
	 * if we're exiting a ton of rings at the same time. It just adds
	 * noise and overhead, there's no discernable change in runtime
	 * over using system_wq.
	 */
	queue_work(system_unbound_wq, &ctx->exit_work);
}

static int io_uring_release(struct inode *inode, struct file *file)
{
	struct io_ring_ctx *ctx = file->private_data;

	file->private_data = NULL;
	io_ring_ctx_wait_and_kill(ctx);
	return 0;
}

struct io_task_cancel {
	struct task_struct *task;
	bool all;
};

static bool io_cancel_task_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_task_cancel *cancel = data;

	return io_match_task_safe(req, cancel->task, cancel->all);
}

static __cold bool io_cancel_defer_files(struct io_ring_ctx *ctx,
					 struct task_struct *task,
					 bool cancel_all)
{
	struct io_defer_entry *de;
	LIST_HEAD(list);

	spin_lock(&ctx->completion_lock);
	list_for_each_entry_reverse(de, &ctx->defer_list, list) {
		if (io_match_task_safe(de->req, task, cancel_all)) {
			list_cut_position(&list, &ctx->defer_list, &de->list);
			break;
		}
	}
	spin_unlock(&ctx->completion_lock);
	if (list_empty(&list))
		return false;

	while (!list_empty(&list)) {
		de = list_first_entry(&list, struct io_defer_entry, list);
		list_del_init(&de->list);
		io_req_task_queue_fail(de->req, -ECANCELED);
		kfree(de);
	}
	return true;
}

static __cold bool io_uring_try_cancel_iowq(struct io_ring_ctx *ctx)
{
	struct io_tctx_node *node;
	enum io_wq_cancel cret;
	bool ret = false;

	mutex_lock(&ctx->uring_lock);
	list_for_each_entry(node, &ctx->tctx_list, ctx_node) {
		struct io_uring_task *tctx = node->task->io_uring;

		/*
		 * io_wq will stay alive while we hold uring_lock, because it's
		 * killed after ctx nodes, which requires to take the lock.
		 */
		if (!tctx || !tctx->io_wq)
			continue;
		cret = io_wq_cancel_cb(tctx->io_wq, io_cancel_ctx_cb, ctx, true);
		ret |= (cret != IO_WQ_CANCEL_NOTFOUND);
	}
	mutex_unlock(&ctx->uring_lock);

	return ret;
}

static bool io_uring_try_cancel_uring_cmd(struct io_ring_ctx *ctx,
		struct task_struct *task, bool cancel_all)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool ret = false;

	lockdep_assert_held(&ctx->uring_lock);

	hlist_for_each_entry_safe(req, tmp, &ctx->cancelable_uring_cmd,
			hash_node) {
		struct io_uring_cmd *cmd = io_kiocb_to_cmd(req,
				struct io_uring_cmd);
		struct file *file = req->file;

		if (!cancel_all && req->task != task)
			continue;

		if (cmd->flags & IORING_URING_CMD_CANCELABLE) {
			/* ->sqe isn't available if no async data */
			if (!req_has_async_data(req))
				cmd->sqe = NULL;
			file->f_op->uring_cmd(cmd, IO_URING_F_CANCEL);
			ret = true;
		}
	}
	io_submit_flush_completions(ctx);

	return ret;
}

static __cold bool io_uring_try_cancel_requests(struct io_ring_ctx *ctx,
						struct task_struct *task,
						bool cancel_all)
{
	struct io_task_cancel cancel = { .task = task, .all = cancel_all, };
	struct io_uring_task *tctx = task ? task->io_uring : NULL;
	enum io_wq_cancel cret;
	bool ret = false;

	/* set it so io_req_local_work_add() would wake us up */
	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN) {
		atomic_set(&ctx->cq_wait_nr, 1);
		smp_mb();
	}

	/* failed during ring init, it couldn't have issued any requests */
	if (!ctx->rings)
		return false;

	if (!task) {
		ret |= io_uring_try_cancel_iowq(ctx);
	} else if (tctx && tctx->io_wq) {
		/*
		 * Cancels requests of all rings, not only @ctx, but
		 * it's fine as the task is in exit/exec.
		 */
		cret = io_wq_cancel_cb(tctx->io_wq, io_cancel_task_cb,
				       &cancel, true);
		ret |= (cret != IO_WQ_CANCEL_NOTFOUND);
	}

	/* SQPOLL thread does its own polling */
	if ((!(ctx->flags & IORING_SETUP_SQPOLL) && cancel_all) ||
	    (ctx->sq_data && ctx->sq_data->thread == current)) {
		while (!wq_list_empty(&ctx->iopoll_list)) {
			io_iopoll_try_reap_events(ctx);
			ret = true;
			cond_resched();
		}
	}

	if ((ctx->flags & IORING_SETUP_DEFER_TASKRUN) &&
	    io_allowed_defer_tw_run(ctx))
		ret |= io_run_local_work(ctx) > 0;
	ret |= io_cancel_defer_files(ctx, task, cancel_all);
	mutex_lock(&ctx->uring_lock);
	ret |= io_poll_remove_all(ctx, task, cancel_all);
	ret |= io_waitid_remove_all(ctx, task, cancel_all);
	ret |= io_futex_remove_all(ctx, task, cancel_all);
	ret |= io_uring_try_cancel_uring_cmd(ctx, task, cancel_all);
	mutex_unlock(&ctx->uring_lock);
	ret |= io_kill_timeouts(ctx, task, cancel_all);
	if (task)
		ret |= io_run_task_work() > 0;
	return ret;
}

static s64 tctx_inflight(struct io_uring_task *tctx, bool tracked)
{
	if (tracked)
		return atomic_read(&tctx->inflight_tracked);
	return percpu_counter_sum(&tctx->inflight);
}

/*
 * Find any io_uring ctx that this task has registered or done IO on, and cancel
 * requests. @sqd should be not-null IFF it's an SQPOLL thread cancellation.
 */
__cold void io_uring_cancel_generic(bool cancel_all, struct io_sq_data *sqd)
{
	struct io_uring_task *tctx = current->io_uring;
	struct io_ring_ctx *ctx;
	struct io_tctx_node *node;
	unsigned long index;
	s64 inflight;
	DEFINE_WAIT(wait);

	WARN_ON_ONCE(sqd && sqd->thread != current);

	if (!current->io_uring)
		return;
	if (tctx->io_wq)
		io_wq_exit_start(tctx->io_wq);

	atomic_inc(&tctx->in_cancel);
	do {
		bool loop = false;

		io_uring_drop_tctx_refs(current);
		/* read completions before cancelations */
		inflight = tctx_inflight(tctx, !cancel_all);
		if (!inflight)
			break;

		if (!sqd) {
			xa_for_each(&tctx->xa, index, node) {
				/* sqpoll task will cancel all its requests */
				if (node->ctx->sq_data)
					continue;
				loop |= io_uring_try_cancel_requests(node->ctx,
							current, cancel_all);
			}
		} else {
			list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
				loop |= io_uring_try_cancel_requests(ctx,
								     current,
								     cancel_all);
		}

		if (loop) {
			cond_resched();
			continue;
		}

		prepare_to_wait(&tctx->wait, &wait, TASK_INTERRUPTIBLE);
		io_run_task_work();
		io_uring_drop_tctx_refs(current);
		xa_for_each(&tctx->xa, index, node) {
			if (!llist_empty(&node->ctx->work_llist)) {
				WARN_ON_ONCE(node->ctx->submitter_task &&
					     node->ctx->submitter_task != current);
				goto end_wait;
			}
		}
		/*
		 * If we've seen completions, retry without waiting. This
		 * avoids a race where a completion comes in before we did
		 * prepare_to_wait().
		 */
		if (inflight == tctx_inflight(tctx, !cancel_all))
			schedule();
end_wait:
		finish_wait(&tctx->wait, &wait);
	} while (1);

	io_uring_clean_tctx(tctx);
	if (cancel_all) {
		/*
		 * We shouldn't run task_works after cancel, so just leave
		 * ->in_cancel set for normal exit.
		 */
		atomic_dec(&tctx->in_cancel);
		/* for exec all current's requests should be gone, kill tctx */
		__io_uring_free(current);
	}
}

void __io_uring_cancel(bool cancel_all)
{
	io_uring_cancel_generic(cancel_all, NULL);
}

static void *io_uring_validate_mmap_request(struct file *file,
					    loff_t pgoff, size_t sz)
{
	struct io_ring_ctx *ctx = file->private_data;
	loff_t offset = pgoff << PAGE_SHIFT;
	struct page *page;
	void *ptr;

	switch (offset & IORING_OFF_MMAP_MASK) {
	case IORING_OFF_SQ_RING:
	case IORING_OFF_CQ_RING:
		/* Don't allow mmap if the ring was setup without it */
		if (ctx->flags & IORING_SETUP_NO_MMAP)
			return ERR_PTR(-EINVAL);
		ptr = ctx->rings;
		break;
	case IORING_OFF_SQES:
		/* Don't allow mmap if the ring was setup without it */
		if (ctx->flags & IORING_SETUP_NO_MMAP)
			return ERR_PTR(-EINVAL);
		ptr = ctx->sq_sqes;
		break;
	case IORING_OFF_PBUF_RING: {
		unsigned int bgid;

		bgid = (offset & ~IORING_OFF_MMAP_MASK) >> IORING_OFF_PBUF_SHIFT;
		rcu_read_lock();
		ptr = io_pbuf_get_address(ctx, bgid);
		rcu_read_unlock();
		if (!ptr)
			return ERR_PTR(-EINVAL);
		break;
		}
	default:
		return ERR_PTR(-EINVAL);
	}

	page = virt_to_head_page(ptr);
	if (sz > page_size(page))
		return ERR_PTR(-EINVAL);

	return ptr;
}

#ifdef CONFIG_MMU

static __cold int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t sz = vma->vm_end - vma->vm_start;
	unsigned long pfn;
	void *ptr;

	ptr = io_uring_validate_mmap_request(file, vma->vm_pgoff, sz);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	pfn = virt_to_phys(ptr) >> PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start, pfn, sz, vma->vm_page_prot);
}

static unsigned long io_uring_mmu_get_unmapped_area(struct file *filp,
			unsigned long addr, unsigned long len,
			unsigned long pgoff, unsigned long flags)
{
	void *ptr;

	/*
	 * Do not allow to map to user-provided address to avoid breaking the
	 * aliasing rules. Userspace is not able to guess the offset address of
	 * kernel kmalloc()ed memory area.
	 */
	if (addr)
		return -EINVAL;

	ptr = io_uring_validate_mmap_request(filp, pgoff, len);
	if (IS_ERR(ptr))
		return -ENOMEM;

	/*
	 * Some architectures have strong cache aliasing requirements.
	 * For such architectures we need a coherent mapping which aliases
	 * kernel memory *and* userspace memory. To achieve that:
	 * - use a NULL file pointer to reference physical memory, and
	 * - use the kernel virtual address of the shared io_uring context
	 *   (instead of the userspace-provided address, which has to be 0UL
	 *   anyway).
	 * - use the same pgoff which the get_unmapped_area() uses to
	 *   calculate the page colouring.
	 * For architectures without such aliasing requirements, the
	 * architecture will return any suitable mapping because addr is 0.
	 */
	filp = NULL;
	flags |= MAP_SHARED;
	pgoff = 0;	/* has been translated to ptr above */
#ifdef SHM_COLOUR
	addr = (uintptr_t) ptr;
	pgoff = addr >> PAGE_SHIFT;
#else
	addr = 0UL;
#endif
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}

#else /* !CONFIG_MMU */

static int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	return is_nommu_shared_mapping(vma->vm_flags) ? 0 : -EINVAL;
}

static unsigned int io_uring_nommu_mmap_capabilities(struct file *file)
{
	return NOMMU_MAP_DIRECT | NOMMU_MAP_READ | NOMMU_MAP_WRITE;
}

static unsigned long io_uring_nommu_get_unmapped_area(struct file *file,
	unsigned long addr, unsigned long len,
	unsigned long pgoff, unsigned long flags)
{
	void *ptr;

	ptr = io_uring_validate_mmap_request(file, pgoff, len);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	return (unsigned long) ptr;
}

#endif /* !CONFIG_MMU */

static int io_validate_ext_arg(unsigned flags, const void __user *argp, size_t argsz)
{
	if (flags & IORING_ENTER_EXT_ARG) {
		struct io_uring_getevents_arg arg;

		if (argsz != sizeof(arg))
			return -EINVAL;
		if (copy_from_user(&arg, argp, sizeof(arg)))
			return -EFAULT;
	}
	return 0;
}

static int io_get_ext_arg(unsigned flags, const void __user *argp, size_t *argsz,
			  struct __kernel_timespec __user **ts,
			  const sigset_t __user **sig)
{
	struct io_uring_getevents_arg arg;

	/*
	 * If EXT_ARG isn't set, then we have no timespec and the argp pointer
	 * is just a pointer to the sigset_t.
	 */
	if (!(flags & IORING_ENTER_EXT_ARG)) {
		*sig = (const sigset_t __user *) argp;
		*ts = NULL;
		return 0;
	}

	/*
	 * EXT_ARG is set - ensure we agree on the size of it and copy in our
	 * timespec and sigset_t pointers if good.
	 */
	if (*argsz != sizeof(arg))
		return -EINVAL;
	if (copy_from_user(&arg, argp, sizeof(arg)))
		return -EFAULT;
	if (arg.pad)
		return -EINVAL;
	*sig = u64_to_user_ptr(arg.sigmask);
	*argsz = arg.sigmask_sz;
	*ts = u64_to_user_ptr(arg.ts);
	return 0;
}

SYSCALL_DEFINE6(io_uring_enter, unsigned int, fd, u32, to_submit,
		u32, min_complete, u32, flags, const void __user *, argp,
		size_t, argsz)
{
	struct io_ring_ctx *ctx;
	struct file *file;
	long ret;

	if (unlikely(flags & ~(IORING_ENTER_GETEVENTS | IORING_ENTER_SQ_WAKEUP |
			       IORING_ENTER_SQ_WAIT | IORING_ENTER_EXT_ARG |
			       IORING_ENTER_REGISTERED_RING)))
		return -EINVAL;

	/*
	 * Ring fd has been registered via IORING_REGISTER_RING_FDS, we
	 * need only dereference our task private array to find it.
	 */
	if (flags & IORING_ENTER_REGISTERED_RING) {
		struct io_uring_task *tctx = current->io_uring;

		if (unlikely(!tctx || fd >= IO_RINGFD_REG_MAX))
			return -EINVAL;
		fd = array_index_nospec(fd, IO_RINGFD_REG_MAX);
		file = tctx->registered_rings[fd];
		if (unlikely(!file))
			return -EBADF;
	} else {
		file = fget(fd);
		if (unlikely(!file))
			return -EBADF;
		ret = -EOPNOTSUPP;
		if (unlikely(!io_is_uring_fops(file)))
			goto out;
	}

	ctx = file->private_data;
	ret = -EBADFD;
	if (unlikely(ctx->flags & IORING_SETUP_R_DISABLED))
		goto out;

	/*
	 * For SQ polling, the thread will do all submissions and completions.
	 * Just return the requested submit count, and wake the thread if
	 * we were asked to.
	 */
	ret = 0;
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		io_cqring_overflow_flush(ctx);

		if (unlikely(ctx->sq_data->thread == NULL)) {
			ret = -EOWNERDEAD;
			goto out;
		}
		if (flags & IORING_ENTER_SQ_WAKEUP)
			wake_up(&ctx->sq_data->wait);
		if (flags & IORING_ENTER_SQ_WAIT)
			io_sqpoll_wait_sq(ctx);

		ret = to_submit;
	} else if (to_submit) {
		ret = io_uring_add_tctx_node(ctx);
		if (unlikely(ret))
			goto out;

		mutex_lock(&ctx->uring_lock);
		ret = io_submit_sqes(ctx, to_submit);
		if (ret != to_submit) {
			mutex_unlock(&ctx->uring_lock);
			goto out;
		}
		if (flags & IORING_ENTER_GETEVENTS) {
			if (ctx->syscall_iopoll)
				goto iopoll_locked;
			/*
			 * Ignore errors, we'll soon call io_cqring_wait() and
			 * it should handle ownership problems if any.
			 */
			if (ctx->flags & IORING_SETUP_DEFER_TASKRUN)
				(void)io_run_local_work_locked(ctx);
		}
		mutex_unlock(&ctx->uring_lock);
	}

	if (flags & IORING_ENTER_GETEVENTS) {
		int ret2;

		if (ctx->syscall_iopoll) {
			/*
			 * We disallow the app entering submit/complete with
			 * polling, but we still need to lock the ring to
			 * prevent racing with polled issue that got punted to
			 * a workqueue.
			 */
			mutex_lock(&ctx->uring_lock);
iopoll_locked:
			ret2 = io_validate_ext_arg(flags, argp, argsz);
			if (likely(!ret2)) {
				min_complete = min(min_complete,
						   ctx->cq_entries);
				ret2 = io_iopoll_check(ctx, min_complete);
			}
			mutex_unlock(&ctx->uring_lock);
		} else {
			const sigset_t __user *sig;
			struct __kernel_timespec __user *ts;

			ret2 = io_get_ext_arg(flags, argp, &argsz, &ts, &sig);
			if (likely(!ret2)) {
				min_complete = min(min_complete,
						   ctx->cq_entries);
				ret2 = io_cqring_wait(ctx, min_complete, sig,
						      argsz, ts);
			}
		}

		if (!ret) {
			ret = ret2;

			/*
			 * EBADR indicates that one or more CQE were dropped.
			 * Once the user has been informed we can clear the bit
			 * as they are obviously ok with those drops.
			 */
			if (unlikely(ret2 == -EBADR))
				clear_bit(IO_CHECK_CQ_DROPPED_BIT,
					  &ctx->check_cq);
		}
	}
out:
	if (!(flags & IORING_ENTER_REGISTERED_RING))
		fput(file);
	return ret;
}

static const struct file_operations io_uring_fops = {
	.release	= io_uring_release,
	.mmap		= io_uring_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = io_uring_nommu_get_unmapped_area,
	.mmap_capabilities = io_uring_nommu_mmap_capabilities,
#else
	.get_unmapped_area = io_uring_mmu_get_unmapped_area,
#endif
	.poll		= io_uring_poll,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= io_uring_show_fdinfo,
#endif
};

bool io_is_uring_fops(struct file *file)
{
	return file->f_op == &io_uring_fops;
}

static __cold int io_allocate_scq_urings(struct io_ring_ctx *ctx,
					 struct io_uring_params *p)
{
	struct io_rings *rings;
	size_t size, sq_array_offset;
	void *ptr;

	/* make sure these are sane, as we already accounted them */
	ctx->sq_entries = p->sq_entries;
	ctx->cq_entries = p->cq_entries;

	size = rings_size(ctx, p->sq_entries, p->cq_entries, &sq_array_offset);
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	if (!(ctx->flags & IORING_SETUP_NO_MMAP))
		rings = io_mem_alloc(size);
	else
		rings = io_rings_map(ctx, p->cq_off.user_addr, size);

	if (IS_ERR(rings))
		return PTR_ERR(rings);

	ctx->rings = rings;
	if (!(ctx->flags & IORING_SETUP_NO_SQARRAY))
		ctx->sq_array = (u32 *)((char *)rings + sq_array_offset);
	rings->sq_ring_mask = p->sq_entries - 1;
	rings->cq_ring_mask = p->cq_entries - 1;
	rings->sq_ring_entries = p->sq_entries;
	rings->cq_ring_entries = p->cq_entries;

	if (p->flags & IORING_SETUP_SQE128)
		size = array_size(2 * sizeof(struct io_uring_sqe), p->sq_entries);
	else
		size = array_size(sizeof(struct io_uring_sqe), p->sq_entries);
	if (size == SIZE_MAX) {
		io_rings_free(ctx);
		return -EOVERFLOW;
	}

	if (!(ctx->flags & IORING_SETUP_NO_MMAP))
		ptr = io_mem_alloc(size);
	else
		ptr = io_sqes_map(ctx, p->sq_off.user_addr, size);

	if (IS_ERR(ptr)) {
		io_rings_free(ctx);
		return PTR_ERR(ptr);
	}

	ctx->sq_sqes = ptr;
	return 0;
}

static int io_uring_install_fd(struct file *file)
{
	int fd;

	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;
	fd_install(fd, file);
	return fd;
}

/*
 * Allocate an anonymous fd, this is what constitutes the application
 * visible backing of an io_uring instance. The application mmaps this
 * fd to gain access to the SQ/CQ ring details.
 */
static struct file *io_uring_get_file(struct io_ring_ctx *ctx)
{
	/* Create a new inode so that the LSM can block the creation.  */
	return anon_inode_create_getfile("[io_uring]", &io_uring_fops, ctx,
					 O_RDWR | O_CLOEXEC, NULL);
}

static __cold int io_uring_create(unsigned entries, struct io_uring_params *p,
				  struct io_uring_params __user *params)
{
	struct io_ring_ctx *ctx;
	struct io_uring_task *tctx;
	struct file *file;
	int ret;

	if (!entries)
		return -EINVAL;
	if (entries > IORING_MAX_ENTRIES) {
		if (!(p->flags & IORING_SETUP_CLAMP))
			return -EINVAL;
		entries = IORING_MAX_ENTRIES;
	}

	if ((p->flags & IORING_SETUP_REGISTERED_FD_ONLY)
	    && !(p->flags & IORING_SETUP_NO_MMAP))
		return -EINVAL;

	/*
	 * Use twice as many entries for the CQ ring. It's possible for the
	 * application to drive a higher depth than the size of the SQ ring,
	 * since the sqes are only used at submission time. This allows for
	 * some flexibility in overcommitting a bit. If the application has
	 * set IORING_SETUP_CQSIZE, it will have passed in the desired number
	 * of CQ ring entries manually.
	 */
	p->sq_entries = roundup_pow_of_two(entries);
	if (p->flags & IORING_SETUP_CQSIZE) {
		/*
		 * If IORING_SETUP_CQSIZE is set, we do the same roundup
		 * to a power-of-two, if it isn't already. We do NOT impose
		 * any cq vs sq ring sizing.
		 */
		if (!p->cq_entries)
			return -EINVAL;
		if (p->cq_entries > IORING_MAX_CQ_ENTRIES) {
			if (!(p->flags & IORING_SETUP_CLAMP))
				return -EINVAL;
			p->cq_entries = IORING_MAX_CQ_ENTRIES;
		}
		p->cq_entries = roundup_pow_of_two(p->cq_entries);
		if (p->cq_entries < p->sq_entries)
			return -EINVAL;
	} else {
		p->cq_entries = 2 * p->sq_entries;
	}

	ctx = io_ring_ctx_alloc(p);
	if (!ctx)
		return -ENOMEM;

	if ((ctx->flags & IORING_SETUP_DEFER_TASKRUN) &&
	    !(ctx->flags & IORING_SETUP_IOPOLL) &&
	    !(ctx->flags & IORING_SETUP_SQPOLL))
		ctx->task_complete = true;

	if (ctx->task_complete || (ctx->flags & IORING_SETUP_IOPOLL))
		ctx->lockless_cq = true;

	/*
	 * lazy poll_wq activation relies on ->task_complete for synchronisation
	 * purposes, see io_activate_pollwq()
	 */
	if (!ctx->task_complete)
		ctx->poll_activated = true;

	/*
	 * When SETUP_IOPOLL and SETUP_SQPOLL are both enabled, user
	 * space applications don't need to do io completion events
	 * polling again, they can rely on io_sq_thread to do polling
	 * work, which can reduce cpu usage and uring_lock contention.
	 */
	if (ctx->flags & IORING_SETUP_IOPOLL &&
	    !(ctx->flags & IORING_SETUP_SQPOLL))
		ctx->syscall_iopoll = 1;

	ctx->compat = in_compat_syscall();
	if (!ns_capable_noaudit(&init_user_ns, CAP_IPC_LOCK))
		ctx->user = get_uid(current_user());

	/*
	 * For SQPOLL, we just need a wakeup, always. For !SQPOLL, if
	 * COOP_TASKRUN is set, then IPIs are never needed by the app.
	 */
	ret = -EINVAL;
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		/* IPI related flags don't make sense with SQPOLL */
		if (ctx->flags & (IORING_SETUP_COOP_TASKRUN |
				  IORING_SETUP_TASKRUN_FLAG |
				  IORING_SETUP_DEFER_TASKRUN))
			goto err;
		ctx->notify_method = TWA_SIGNAL_NO_IPI;
	} else if (ctx->flags & IORING_SETUP_COOP_TASKRUN) {
		ctx->notify_method = TWA_SIGNAL_NO_IPI;
	} else {
		if (ctx->flags & IORING_SETUP_TASKRUN_FLAG &&
		    !(ctx->flags & IORING_SETUP_DEFER_TASKRUN))
			goto err;
		ctx->notify_method = TWA_SIGNAL;
	}

	/*
	 * For DEFER_TASKRUN we require the completion task to be the same as the
	 * submission task. This implies that there is only one submitter, so enforce
	 * that.
	 */
	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN &&
	    !(ctx->flags & IORING_SETUP_SINGLE_ISSUER)) {
		goto err;
	}

	/*
	 * This is just grabbed for accounting purposes. When a process exits,
	 * the mm is exited and dropped before the files, hence we need to hang
	 * on to this mm purely for the purposes of being able to unaccount
	 * memory (locked/pinned vm). It's not used for anything else.
	 */
	mmgrab(current->mm);
	ctx->mm_account = current->mm;

	ret = io_allocate_scq_urings(ctx, p);
	if (ret)
		goto err;

	ret = io_sq_offload_create(ctx, p);
	if (ret)
		goto err;

	ret = io_rsrc_init(ctx);
	if (ret)
		goto err;

	p->sq_off.head = offsetof(struct io_rings, sq.head);
	p->sq_off.tail = offsetof(struct io_rings, sq.tail);
	p->sq_off.ring_mask = offsetof(struct io_rings, sq_ring_mask);
	p->sq_off.ring_entries = offsetof(struct io_rings, sq_ring_entries);
	p->sq_off.flags = offsetof(struct io_rings, sq_flags);
	p->sq_off.dropped = offsetof(struct io_rings, sq_dropped);
	if (!(ctx->flags & IORING_SETUP_NO_SQARRAY))
		p->sq_off.array = (char *)ctx->sq_array - (char *)ctx->rings;
	p->sq_off.resv1 = 0;
	if (!(ctx->flags & IORING_SETUP_NO_MMAP))
		p->sq_off.user_addr = 0;

	p->cq_off.head = offsetof(struct io_rings, cq.head);
	p->cq_off.tail = offsetof(struct io_rings, cq.tail);
	p->cq_off.ring_mask = offsetof(struct io_rings, cq_ring_mask);
	p->cq_off.ring_entries = offsetof(struct io_rings, cq_ring_entries);
	p->cq_off.overflow = offsetof(struct io_rings, cq_overflow);
	p->cq_off.cqes = offsetof(struct io_rings, cqes);
	p->cq_off.flags = offsetof(struct io_rings, cq_flags);
	p->cq_off.resv1 = 0;
	if (!(ctx->flags & IORING_SETUP_NO_MMAP))
		p->cq_off.user_addr = 0;

	p->features = IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP |
			IORING_FEAT_SUBMIT_STABLE | IORING_FEAT_RW_CUR_POS |
			IORING_FEAT_CUR_PERSONALITY | IORING_FEAT_FAST_POLL |
			IORING_FEAT_POLL_32BITS | IORING_FEAT_SQPOLL_NONFIXED |
			IORING_FEAT_EXT_ARG | IORING_FEAT_NATIVE_WORKERS |
			IORING_FEAT_RSRC_TAGS | IORING_FEAT_CQE_SKIP |
			IORING_FEAT_LINKED_FILE | IORING_FEAT_REG_REG_RING;

	if (copy_to_user(params, p, sizeof(*p))) {
		ret = -EFAULT;
		goto err;
	}

	if (ctx->flags & IORING_SETUP_SINGLE_ISSUER
	    && !(ctx->flags & IORING_SETUP_R_DISABLED))
		WRITE_ONCE(ctx->submitter_task, get_task_struct(current));

	file = io_uring_get_file(ctx);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err;
	}

	ret = __io_uring_add_tctx_node(ctx);
	if (ret)
		goto err_fput;
	tctx = current->io_uring;

	/*
	 * Install ring fd as the very last thing, so we don't risk someone
	 * having closed it before we finish setup
	 */
	if (p->flags & IORING_SETUP_REGISTERED_FD_ONLY)
		ret = io_ring_add_registered_file(tctx, file, 0, IO_RINGFD_REG_MAX);
	else
		ret = io_uring_install_fd(file);
	if (ret < 0)
		goto err_fput;

	trace_io_uring_create(ret, ctx, p->sq_entries, p->cq_entries, p->flags);
	return ret;
err:
	io_ring_ctx_wait_and_kill(ctx);
	return ret;
err_fput:
	fput(file);
	return ret;
}

/*
 * Sets up an aio uring context, and returns the fd. Applications asks for a
 * ring size, we return the actual sq/cq ring sizes (among other things) in the
 * params structure passed in.
 */
static long io_uring_setup(u32 entries, struct io_uring_params __user *params)
{
	struct io_uring_params p;
	int i;

	if (copy_from_user(&p, params, sizeof(p)))
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(p.resv); i++) {
		if (p.resv[i])
			return -EINVAL;
	}

	if (p.flags & ~(IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL |
			IORING_SETUP_SQ_AFF | IORING_SETUP_CQSIZE |
			IORING_SETUP_CLAMP | IORING_SETUP_ATTACH_WQ |
			IORING_SETUP_R_DISABLED | IORING_SETUP_SUBMIT_ALL |
			IORING_SETUP_COOP_TASKRUN | IORING_SETUP_TASKRUN_FLAG |
			IORING_SETUP_SQE128 | IORING_SETUP_CQE32 |
			IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN |
			IORING_SETUP_NO_MMAP | IORING_SETUP_REGISTERED_FD_ONLY |
			IORING_SETUP_NO_SQARRAY))
		return -EINVAL;

	return io_uring_create(entries, &p, params);
}

static inline bool io_uring_allowed(void)
{
	int disabled = READ_ONCE(sysctl_io_uring_disabled);
	kgid_t io_uring_group;

	if (disabled == 2)
		return false;

	if (disabled == 0 || capable(CAP_SYS_ADMIN))
		return true;

	io_uring_group = make_kgid(&init_user_ns, sysctl_io_uring_group);
	if (!gid_valid(io_uring_group))
		return false;

	return in_group_p(io_uring_group);
}

SYSCALL_DEFINE2(io_uring_setup, u32, entries,
		struct io_uring_params __user *, params)
{
	if (!io_uring_allowed())
		return -EPERM;

	return io_uring_setup(entries, params);
}

static int __init io_uring_init(void)
{
#define __BUILD_BUG_VERIFY_OFFSET_SIZE(stype, eoffset, esize, ename) do { \
	BUILD_BUG_ON(offsetof(stype, ename) != eoffset); \
	BUILD_BUG_ON(sizeof_field(stype, ename) != esize); \
} while (0)

#define BUILD_BUG_SQE_ELEM(eoffset, etype, ename) \
	__BUILD_BUG_VERIFY_OFFSET_SIZE(struct io_uring_sqe, eoffset, sizeof(etype), ename)
#define BUILD_BUG_SQE_ELEM_SIZE(eoffset, esize, ename) \
	__BUILD_BUG_VERIFY_OFFSET_SIZE(struct io_uring_sqe, eoffset, esize, ename)
	BUILD_BUG_ON(sizeof(struct io_uring_sqe) != 64);
	BUILD_BUG_SQE_ELEM(0,  __u8,   opcode);
	BUILD_BUG_SQE_ELEM(1,  __u8,   flags);
	BUILD_BUG_SQE_ELEM(2,  __u16,  ioprio);
	BUILD_BUG_SQE_ELEM(4,  __s32,  fd);
	BUILD_BUG_SQE_ELEM(8,  __u64,  off);
	BUILD_BUG_SQE_ELEM(8,  __u64,  addr2);
	BUILD_BUG_SQE_ELEM(8,  __u32,  cmd_op);
	BUILD_BUG_SQE_ELEM(12, __u32, __pad1);
	BUILD_BUG_SQE_ELEM(16, __u64,  addr);
	BUILD_BUG_SQE_ELEM(16, __u64,  splice_off_in);
	BUILD_BUG_SQE_ELEM(24, __u32,  len);
	BUILD_BUG_SQE_ELEM(28,     __kernel_rwf_t, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */   int, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */ __u32, rw_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fsync_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */ __u16,  poll_events);
	BUILD_BUG_SQE_ELEM(28, __u32,  poll32_events);
	BUILD_BUG_SQE_ELEM(28, __u32,  sync_range_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  msg_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  timeout_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  accept_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  cancel_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  open_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  statx_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fadvise_advice);
	BUILD_BUG_SQE_ELEM(28, __u32,  splice_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  rename_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  unlink_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  hardlink_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  xattr_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  msg_ring_flags);
	BUILD_BUG_SQE_ELEM(32, __u64,  user_data);
	BUILD_BUG_SQE_ELEM(40, __u16,  buf_index);
	BUILD_BUG_SQE_ELEM(40, __u16,  buf_group);
	BUILD_BUG_SQE_ELEM(42, __u16,  personality);
	BUILD_BUG_SQE_ELEM(44, __s32,  splice_fd_in);
	BUILD_BUG_SQE_ELEM(44, __u32,  file_index);
	BUILD_BUG_SQE_ELEM(44, __u16,  addr_len);
	BUILD_BUG_SQE_ELEM(46, __u16,  __pad3[0]);
	BUILD_BUG_SQE_ELEM(48, __u64,  addr3);
	BUILD_BUG_SQE_ELEM_SIZE(48, 0, cmd);
	BUILD_BUG_SQE_ELEM(56, __u64,  __pad2);

	BUILD_BUG_ON(sizeof(struct io_uring_files_update) !=
		     sizeof(struct io_uring_rsrc_update));
	BUILD_BUG_ON(sizeof(struct io_uring_rsrc_update) >
		     sizeof(struct io_uring_rsrc_update2));

	/* ->buf_index is u16 */
	BUILD_BUG_ON(offsetof(struct io_uring_buf_ring, bufs) != 0);
	BUILD_BUG_ON(offsetof(struct io_uring_buf, resv) !=
		     offsetof(struct io_uring_buf_ring, tail));

	/* should fit into one byte */
	BUILD_BUG_ON(SQE_VALID_FLAGS >= (1 << 8));
	BUILD_BUG_ON(SQE_COMMON_FLAGS >= (1 << 8));
	BUILD_BUG_ON((SQE_VALID_FLAGS | SQE_COMMON_FLAGS) != SQE_VALID_FLAGS);

	BUILD_BUG_ON(__REQ_F_LAST_BIT > 8 * sizeof(int));

	BUILD_BUG_ON(sizeof(atomic_t) != sizeof(u32));

	/* top 8bits are for internal use */
	BUILD_BUG_ON((IORING_URING_CMD_MASK & 0xff000000) != 0);

	io_uring_optable_init();

	/*
	 * Allow user copy in the per-command field, which starts after the
	 * file in io_kiocb and until the opcode field. The openat2 handling
	 * requires copying in user memory into the io_kiocb object in that
	 * range, and HARDENED_USERCOPY will complain if we haven't
	 * correctly annotated this range.
	 */
	req_cachep = kmem_cache_create_usercopy("io_kiocb",
				sizeof(struct io_kiocb), 0,
				SLAB_HWCACHE_ALIGN | SLAB_PANIC |
				SLAB_ACCOUNT | SLAB_TYPESAFE_BY_RCU,
				offsetof(struct io_kiocb, cmd.data),
				sizeof_field(struct io_kiocb, cmd.data), NULL);
	io_buf_cachep = kmem_cache_create("io_buffer", sizeof(struct io_buffer), 0,
					  SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT,
					  NULL);

#ifdef CONFIG_SYSCTL
	register_sysctl_init("kernel", kernel_io_uring_disabled_table);
#endif

	return 0;
};
__initcall(io_uring_init);
