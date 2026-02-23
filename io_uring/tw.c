// SPDX-License-Identifier: GPL-2.0
/*
 * Task work handling for io_uring
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/io_uring.h>
#include <linux/indirect_call_wrapper.h>

#include "io_uring.h"
#include "tctx.h"
#include "poll.h"
#include "rw.h"
#include "eventfd.h"
#include "wait.h"

void io_fallback_req_func(struct work_struct *work)
{
	struct io_ring_ctx *ctx = container_of(work, struct io_ring_ctx,
						fallback_work.work);
	struct llist_node *node = llist_del_all(&ctx->fallback_llist);
	struct io_kiocb *req, *tmp;
	struct io_tw_state ts = {};

	percpu_ref_get(&ctx->refs);
	mutex_lock(&ctx->uring_lock);
	ts.cancel = io_should_terminate_tw(ctx);
	llist_for_each_entry_safe(req, tmp, node, io_task_work.node)
		req->io_task_work.func((struct io_tw_req){req}, ts);
	io_submit_flush_completions(ctx);
	mutex_unlock(&ctx->uring_lock);
	percpu_ref_put(&ctx->refs);
}

static void ctx_flush_and_put(struct io_ring_ctx *ctx, io_tw_token_t tw)
{
	if (!ctx)
		return;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_andnot(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);

	io_submit_flush_completions(ctx);
	mutex_unlock(&ctx->uring_lock);
	percpu_ref_put(&ctx->refs);
}

/*
 * Run queued task_work, returning the number of entries processed in *count.
 * If more entries than max_entries are available, stop processing once this
 * is reached and return the rest of the list.
 */
struct llist_node *io_handle_tw_list(struct llist_node *node,
				     unsigned int *count,
				     unsigned int max_entries)
{
	struct io_ring_ctx *ctx = NULL;
	struct io_tw_state ts = { };

	do {
		struct llist_node *next = node->next;
		struct io_kiocb *req = container_of(node, struct io_kiocb,
						    io_task_work.node);

		if (req->ctx != ctx) {
			ctx_flush_and_put(ctx, ts);
			ctx = req->ctx;
			mutex_lock(&ctx->uring_lock);
			percpu_ref_get(&ctx->refs);
			ts.cancel = io_should_terminate_tw(ctx);
		}
		INDIRECT_CALL_2(req->io_task_work.func,
				io_poll_task_func, io_req_rw_complete,
				(struct io_tw_req){req}, ts);
		node = next;
		(*count)++;
		if (unlikely(need_resched())) {
			ctx_flush_and_put(ctx, ts);
			ctx = NULL;
			cond_resched();
		}
	} while (node && *count < max_entries);

	ctx_flush_and_put(ctx, ts);
	return node;
}

static __cold void __io_fallback_tw(struct llist_node *node, bool sync)
{
	struct io_ring_ctx *last_ctx = NULL;
	struct io_kiocb *req;

	while (node) {
		req = container_of(node, struct io_kiocb, io_task_work.node);
		node = node->next;
		if (last_ctx != req->ctx) {
			if (last_ctx) {
				if (sync)
					flush_delayed_work(&last_ctx->fallback_work);
				percpu_ref_put(&last_ctx->refs);
			}
			last_ctx = req->ctx;
			percpu_ref_get(&last_ctx->refs);
		}
		if (llist_add(&req->io_task_work.node, &last_ctx->fallback_llist))
			schedule_delayed_work(&last_ctx->fallback_work, 1);
	}

	if (last_ctx) {
		if (sync)
			flush_delayed_work(&last_ctx->fallback_work);
		percpu_ref_put(&last_ctx->refs);
	}
}

static void io_fallback_tw(struct io_uring_task *tctx, bool sync)
{
	struct llist_node *node = llist_del_all(&tctx->task_list);

	__io_fallback_tw(node, sync);
}

struct llist_node *tctx_task_work_run(struct io_uring_task *tctx,
				      unsigned int max_entries,
				      unsigned int *count)
{
	struct llist_node *node;

	node = llist_del_all(&tctx->task_list);
	if (node) {
		node = llist_reverse_order(node);
		node = io_handle_tw_list(node, count, max_entries);
	}

	/* relaxed read is enough as only the task itself sets ->in_cancel */
	if (unlikely(atomic_read(&tctx->in_cancel)))
		io_uring_drop_tctx_refs(current);

	trace_io_uring_task_work_run(tctx, *count);
	return node;
}

void tctx_task_work(struct callback_head *cb)
{
	struct io_uring_task *tctx;
	struct llist_node *ret;
	unsigned int count = 0;

	tctx = container_of(cb, struct io_uring_task, task_work);
	ret = tctx_task_work_run(tctx, UINT_MAX, &count);
	/* can't happen */
	WARN_ON_ONCE(ret);
}

void io_req_local_work_add(struct io_kiocb *req, unsigned flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned nr_wait, nr_tw, nr_tw_prev;
	struct llist_node *head;

	/* See comment above IO_CQ_WAKE_INIT */
	BUILD_BUG_ON(IO_CQ_WAKE_FORCE <= IORING_MAX_CQ_ENTRIES);

	/*
	 * We don't know how many requests there are in the link and whether
	 * they can even be queued lazily, fall back to non-lazy.
	 */
	if (req->flags & IO_REQ_LINK_FLAGS)
		flags &= ~IOU_F_TWQ_LAZY_WAKE;

	guard(rcu)();

	head = READ_ONCE(ctx->work_llist.first);
	do {
		nr_tw_prev = 0;
		if (head) {
			struct io_kiocb *first_req = container_of(head,
							struct io_kiocb,
							io_task_work.node);
			/*
			 * Might be executed at any moment, rely on
			 * SLAB_TYPESAFE_BY_RCU to keep it alive.
			 */
			nr_tw_prev = READ_ONCE(first_req->nr_tw);
		}

		/*
		 * Theoretically, it can overflow, but that's fine as one of
		 * previous adds should've tried to wake the task.
		 */
		nr_tw = nr_tw_prev + 1;
		if (!(flags & IOU_F_TWQ_LAZY_WAKE))
			nr_tw = IO_CQ_WAKE_FORCE;

		req->nr_tw = nr_tw;
		req->io_task_work.node.next = head;
	} while (!try_cmpxchg(&ctx->work_llist.first, &head,
			      &req->io_task_work.node));

	/*
	 * cmpxchg implies a full barrier, which pairs with the barrier
	 * in set_current_state() on the io_cqring_wait() side. It's used
	 * to ensure that either we see updated ->cq_wait_nr, or waiters
	 * going to sleep will observe the work added to the list, which
	 * is similar to the wait/wawke task state sync.
	 */

	if (!head) {
		if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
			atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
		if (ctx->has_evfd)
			io_eventfd_signal(ctx, false);
	}

	nr_wait = atomic_read(&ctx->cq_wait_nr);
	/* not enough or no one is waiting */
	if (nr_tw < nr_wait)
		return;
	/* the previous add has already woken it up */
	if (nr_tw_prev >= nr_wait)
		return;
	wake_up_state(ctx->submitter_task, TASK_INTERRUPTIBLE);
}

void io_req_normal_work_add(struct io_kiocb *req)
{
	struct io_uring_task *tctx = req->tctx;
	struct io_ring_ctx *ctx = req->ctx;

	/* task_work already pending, we're done */
	if (!llist_add(&req->io_task_work.node, &tctx->task_list))
		return;

	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);

	/* SQPOLL doesn't need the task_work added, it'll run it itself */
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		__set_notify_signal(tctx->task);
		return;
	}

	if (likely(!task_work_add(tctx->task, &tctx->task_work, ctx->notify_method)))
		return;

	io_fallback_tw(tctx, false);
}

void io_req_task_work_add_remote(struct io_kiocb *req, unsigned flags)
{
	if (WARN_ON_ONCE(!(req->ctx->flags & IORING_SETUP_DEFER_TASKRUN)))
		return;
	__io_req_task_work_add(req, flags);
}

void __cold io_move_task_work_from_local(struct io_ring_ctx *ctx)
{
	struct llist_node *node = llist_del_all(&ctx->work_llist);

	__io_fallback_tw(node, false);
	node = llist_del_all(&ctx->retry_llist);
	__io_fallback_tw(node, false);
}

static bool io_run_local_work_continue(struct io_ring_ctx *ctx, int events,
				       int min_events)
{
	if (!io_local_work_pending(ctx))
		return false;
	if (events < min_events)
		return true;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
	return false;
}

static int __io_run_local_work_loop(struct llist_node **node,
				    io_tw_token_t tw,
				    int events)
{
	int ret = 0;

	while (*node) {
		struct llist_node *next = (*node)->next;
		struct io_kiocb *req = container_of(*node, struct io_kiocb,
						    io_task_work.node);
		INDIRECT_CALL_2(req->io_task_work.func,
				io_poll_task_func, io_req_rw_complete,
				(struct io_tw_req){req}, tw);
		*node = next;
		if (++ret >= events)
			break;
	}

	return ret;
}

static int __io_run_local_work(struct io_ring_ctx *ctx, io_tw_token_t tw,
			       int min_events, int max_events)
{
	struct llist_node *node;
	unsigned int loops = 0;
	int ret = 0;

	if (WARN_ON_ONCE(ctx->submitter_task != current))
		return -EEXIST;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_andnot(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
again:
	tw.cancel = io_should_terminate_tw(ctx);
	min_events -= ret;
	ret = __io_run_local_work_loop(&ctx->retry_llist.first, tw, max_events);
	if (ctx->retry_llist.first)
		goto retry_done;

	/*
	 * llists are in reverse order, flip it back the right way before
	 * running the pending items.
	 */
	node = llist_reverse_order(llist_del_all(&ctx->work_llist));
	ret += __io_run_local_work_loop(&node, tw, max_events - ret);
	ctx->retry_llist.first = node;
	loops++;

	if (io_run_local_work_continue(ctx, ret, min_events))
		goto again;
retry_done:
	io_submit_flush_completions(ctx);
	if (io_run_local_work_continue(ctx, ret, min_events))
		goto again;

	trace_io_uring_local_work_run(ctx, ret, loops);
	return ret;
}

int io_run_local_work_locked(struct io_ring_ctx *ctx, int min_events)
{
	struct io_tw_state ts = {};

	if (!io_local_work_pending(ctx))
		return 0;
	return __io_run_local_work(ctx, ts, min_events,
					max(IO_LOCAL_TW_DEFAULT_MAX, min_events));
}

int io_run_local_work(struct io_ring_ctx *ctx, int min_events, int max_events)
{
	struct io_tw_state ts = {};
	int ret;

	mutex_lock(&ctx->uring_lock);
	ret = __io_run_local_work(ctx, ts, min_events, max_events);
	mutex_unlock(&ctx->uring_lock);
	return ret;
}
