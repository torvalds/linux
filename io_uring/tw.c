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
#include "mpscq.h"

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

void io_tctx_fallback_work(struct work_struct *work)
{
	struct io_uring_task *tctx = container_of(work, struct io_uring_task,
						  fallback_work);
	unsigned int count = 0;

	/*
	 * Run the entries directly. We're in PF_KTHRED context, hence
	 * io_should_terminate_tw() is true and they will be marked as
	 * canceled.
	 */
	tctx_task_work_run(tctx, UINT_MAX, &count);
	put_task_struct(tctx->task);
}

static void io_fallback_tw(struct io_uring_task *tctx)
{
	/*
	 * The task ref both keeps ->task valid and, as __io_uring_free() is
	 * only called when the task itself is freed, ensures the tctx (and
	 * the queued work) stay around until the drain has run.
	 */
	get_task_struct(tctx->task);
	if (!queue_work(system_dfl_wq, &tctx->fallback_work))
		put_task_struct(tctx->task);
}

/*
 * Run queued task_work, processing no more than max_entries, with the number
 * of entries processed added to *count. If more entries than max_entries are
 * available, the remainder simply stay on the queue for the next run.
 */
void tctx_task_work_run(struct io_uring_task *tctx, unsigned int max_entries,
			unsigned int *count)
{
	struct io_ring_ctx *ctx = NULL;
	struct io_tw_state ts = { };

	while (*count < max_entries) {
		struct llist_node *node = mpscq_pop(&tctx->task_list,
						    &tctx->task_head);
		struct io_kiocb *req;

		if (!node) {
			if (mpscq_empty(&tctx->task_list))
				break;
			/*
			 * A producer has published a node but hasn't
			 * linked it into the queue yet (see mpscq_pop()).
			 * Give it a chance to finish rather than spinning,
			 * and don't sit on the ctx lock while doing so.
			 */
			ctx_flush_and_put(ctx, ts);
			ctx = NULL;
			cond_resched();
			continue;
		}
		req = container_of(node, struct io_kiocb, io_task_work.node);
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
		(*count)++;
		/*
		 * Break if most recent pop emptied the queue. This helps
		 * bound task_work run, and also protects the regular
		 * task_work addition.
		 */
		if (mpscq_pop_emptied(&tctx->task_list, tctx->task_head))
			break;
		if (unlikely(need_resched())) {
			ctx_flush_and_put(ctx, ts);
			ctx = NULL;
			cond_resched();
		}
	}
	ctx_flush_and_put(ctx, ts);

	/*
	 * Relaxed read is enough as only the task itself sets ->in_cancel.
	 * The tctx may also be drained by io_tctx_fallback_work(), in which
	 * case current is a kworker that has no tctx refs to drop.
	 */
	if (unlikely(atomic_read(&tctx->in_cancel)) &&
	    current->io_uring == tctx)
		io_uring_drop_tctx_refs(current);

	trace_io_uring_task_work_run(tctx, *count);
}

void tctx_task_work(struct callback_head *cb)
{
	struct io_uring_task *tctx;
	unsigned int count = 0;

	tctx = container_of(cb, struct io_uring_task, task_work);
	tctx_task_work_run(tctx, UINT_MAX, &count);
}

/*
 * Sets IORING_SQ_TASKRUN in the sq_flags shared with userspace, using the
 * RCU protected rings pointer to be safe against concurrent ring resizing.
 */
static void io_ctx_mark_taskrun(struct io_ring_ctx *ctx)
{
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG) {
		struct io_rings *rings;

		guard(rcu)();
		rings = rcu_dereference(ctx->rings_rcu);
		atomic_or(IORING_SQ_TASKRUN, &rings->sq_flags);
	}
}

void io_req_local_work_add(struct io_kiocb *req, unsigned flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	int nr_wait;

	/*
	 * We don't know how many requests there are in the link and whether
	 * they can even be queued lazily, fall back to non-lazy.
	 */
	if (req->flags & IO_REQ_LINK_FLAGS)
		flags &= ~IOU_F_TWQ_LAZY_WAKE;

	/*
	 * The xchg() in mpscq_push() implies a full barrier, which pairs with
	 * the barrier in set_current_state() on the io_cqring_wait() side. This
	 * ensures that either we see the updated ->cq_wait_nr, or waiters going
	 * to sleep will observe the work added to the list, which is similar to
	 * the wait/wake task state sync.
	 */
	if (mpscq_push(&ctx->work_list, &req->io_task_work.node)) {
		io_ctx_mark_taskrun(ctx);
		if (data_race(ctx->int_flags) & IO_RING_F_HAS_EVFD)
			io_eventfd_signal(ctx, false);
	}

	/*
	 * No one is waiting (IO_CQ_WAKE_INIT), or this cycle's wake up has
	 * already been issued (zero or negative, see below).
	 */
	nr_wait = atomic_read(&ctx->cq_wait_nr);
	if (nr_wait <= 0)
		return;
	if (flags & IOU_F_TWQ_LAZY_WAKE) {
		/*
		 * ->cq_wait_nr counts down the number of lazy adds, once it
		 * hits zero we're good to wake the waiter. A producer that
		 * gets delayed between pushing its entry and getting here
		 * may count down a later wait cycle. That's OK, it'll be an
		 * early wake, not a lost one.
		 */
		if (!atomic_dec_and_test(&ctx->cq_wait_nr))
			return;
	} else if (atomic_xchg(&ctx->cq_wait_nr, IO_CQ_WAKE_INIT) <= 0) {
		/*
		 * Potentially raced with lazy add, claim the wake. A value
		 * <= 0 means a lazy add hit zero or another forced add
		 * claimed IO_CQ_WAKE_INIT. Either way, the wake up for this
		 * wait cycle has already been done.
		 */
		return;
	}
	wake_up_state(ctx->submitter_task, TASK_INTERRUPTIBLE);
}

void io_req_normal_work_add(struct io_kiocb *req)
{
	struct io_uring_task *tctx = req->tctx;
	struct io_ring_ctx *ctx = req->ctx;

	/* tw run already pending, nothing else to do */
	if (!mpscq_push(&tctx->task_list, &req->io_task_work.node))
		return;

	/*
	 * Doesn't need to use ->rings_rcu, as resizing isn't supported for
	 * !DEFER_TASKRUN.
	 */
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_or(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);

	/* SQPOLL doesn't need the task_work added, it'll run it itself */
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		__set_notify_signal(tctx->task);
		return;
	}

	if (likely(!task_work_add(tctx->task, &tctx->task_work, ctx->notify_method)))
		return;

	io_fallback_tw(tctx);
}

void io_req_task_work_add_remote(struct io_kiocb *req, unsigned flags)
{
	if (WARN_ON_ONCE(!(req->ctx->flags & IORING_SETUP_DEFER_TASKRUN)))
		return;
	__io_req_task_work_add(req, flags);
}

void __cold io_cancel_local_task_work(struct io_ring_ctx *ctx)
{
	struct io_tw_state ts = { .cancel = true };
	struct llist_node *node;

	/*
	 * The work list consumer side is serialized by ->uring_lock, see
	 * __io_run_local_work(). Grab it to guard against racing with normal
	 * task_work running, as the task may be exiting. The ring is going
	 * away, run the entries in cancel mode right here - the callers
	 * provide the same process context the per-ctx fallback work that
	 * they were previously punted to ran in.
	 */
	guard(mutex)(&ctx->uring_lock);

	while (!mpscq_empty(&ctx->work_list)) {
		struct io_kiocb *req;

		node = mpscq_pop(&ctx->work_list, &ctx->work_head);
		if (!node) {
			/* a producer is mid-push, wait for it to link */
			cond_resched();
			continue;
		}
		req = container_of(node, struct io_kiocb, io_task_work.node);
		req->io_task_work.func((struct io_tw_req){req}, ts);
	}
	io_submit_flush_completions(ctx);
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

static int __io_run_local_work_loop(struct io_ring_ctx *ctx,
				    io_tw_token_t tw,
				    int events)
{
	int ret = 0;

	while (ret < events) {
		struct llist_node *node = mpscq_pop(&ctx->work_list, &ctx->work_head);
		struct io_kiocb *req;

		if (!node)
			break;
		req = container_of(node, struct io_kiocb, io_task_work.node);
		INDIRECT_CALL_2(req->io_task_work.func,
				io_poll_task_func, io_req_rw_complete,
				(struct io_tw_req){req}, tw);
		ret++;
	}

	return ret;
}

static int __io_run_local_work(struct io_ring_ctx *ctx, io_tw_token_t tw,
			       int min_events, int max_events)
{
	unsigned int loops = 0;
	int ret = 0;

	if (WARN_ON_ONCE(ctx->submitter_task != current))
		return -EEXIST;
	if (ctx->flags & IORING_SETUP_TASKRUN_FLAG)
		atomic_andnot(IORING_SQ_TASKRUN, &ctx->rings->sq_flags);
again:
	/*
	 * If the last loop made no progress while work is still pending,
	 * a producer has published a node but hasn't linked it into the
	 * queue yet (see mpscq_pop()). Give it a chance to finish rather
	 * than spinning on the queue.
	 */
	if (unlikely(loops && !ret))
		cond_resched();
	tw.cancel = io_should_terminate_tw(ctx);
	min_events -= ret;
	ret = __io_run_local_work_loop(ctx, tw, max_events);
	loops++;

	if (io_run_local_work_continue(ctx, ret, min_events))
		goto again;
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
