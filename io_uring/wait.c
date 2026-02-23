// SPDX-License-Identifier: GPL-2.0
/*
 * Waiting for completion events
 */
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/io_uring.h>

#include <trace/events/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "napi.h"
#include "wait.h"

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
	if (io_local_work_pending(ctx)) {
		__set_current_state(TASK_RUNNING);
		if (io_run_local_work(ctx, INT_MAX, IO_LOCAL_TW_DEFAULT_MAX) > 0)
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

static enum hrtimer_restart io_cqring_timer_wakeup(struct hrtimer *timer)
{
	struct io_wait_queue *iowq = container_of(timer, struct io_wait_queue, t);

	WRITE_ONCE(iowq->hit_timeout, 1);
	iowq->min_timeout = 0;
	wake_up_process(iowq->wq.private);
	return HRTIMER_NORESTART;
}

/*
 * Doing min_timeout portion. If we saw any timeouts, events, or have work,
 * wake up. If not, and we have a normal timeout, switch to that and keep
 * sleeping.
 */
static enum hrtimer_restart io_cqring_min_timer_wakeup(struct hrtimer *timer)
{
	struct io_wait_queue *iowq = container_of(timer, struct io_wait_queue, t);
	struct io_ring_ctx *ctx = iowq->ctx;

	/* no general timeout, or shorter (or equal), we are done */
	if (iowq->timeout == KTIME_MAX ||
	    ktime_compare(iowq->min_timeout, iowq->timeout) >= 0)
		goto out_wake;
	/* work we may need to run, wake function will see if we need to wake */
	if (io_has_work(ctx))
		goto out_wake;
	/* got events since we started waiting, min timeout is done */
	if (iowq->cq_min_tail != READ_ONCE(ctx->rings->cq.tail))
		goto out_wake;
	/* if we have any events and min timeout expired, we're done */
	if (io_cqring_events(ctx))
		goto out_wake;

	/*
	 * If using deferred task_work running and application is waiting on
	 * more than one request, ensure we reset it now where we are switching
	 * to normal sleeps. Any request completion post min_wait should wake
	 * the task and return.
	 */
	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN) {
		atomic_set(&ctx->cq_wait_nr, 1);
		smp_mb();
		if (!llist_empty(&ctx->work_llist))
			goto out_wake;
	}

	/* any generated CQE posted past this time should wake us up */
	iowq->cq_tail = iowq->cq_min_tail;

	hrtimer_update_function(&iowq->t, io_cqring_timer_wakeup);
	hrtimer_set_expires(timer, iowq->timeout);
	return HRTIMER_RESTART;
out_wake:
	return io_cqring_timer_wakeup(timer);
}

static int io_cqring_schedule_timeout(struct io_wait_queue *iowq,
				      clockid_t clock_id, ktime_t start_time)
{
	ktime_t timeout;

	if (iowq->min_timeout) {
		timeout = ktime_add_ns(iowq->min_timeout, start_time);
		hrtimer_setup_on_stack(&iowq->t, io_cqring_min_timer_wakeup, clock_id,
				       HRTIMER_MODE_ABS);
	} else {
		timeout = iowq->timeout;
		hrtimer_setup_on_stack(&iowq->t, io_cqring_timer_wakeup, clock_id,
				       HRTIMER_MODE_ABS);
	}

	hrtimer_set_expires_range_ns(&iowq->t, timeout, 0);
	hrtimer_start_expires(&iowq->t, HRTIMER_MODE_ABS);

	if (!READ_ONCE(iowq->hit_timeout))
		schedule();

	hrtimer_cancel(&iowq->t);
	destroy_hrtimer_on_stack(&iowq->t);
	__set_current_state(TASK_RUNNING);

	return READ_ONCE(iowq->hit_timeout) ? -ETIME : 0;
}

static int __io_cqring_wait_schedule(struct io_ring_ctx *ctx,
				     struct io_wait_queue *iowq,
				     struct ext_arg *ext_arg,
				     ktime_t start_time)
{
	int ret = 0;

	/*
	 * Mark us as being in io_wait if we have pending requests, so cpufreq
	 * can take into account that the task is waiting for IO - turns out
	 * to be important for low QD IO.
	 */
	if (ext_arg->iowait && current_pending_io())
		current->in_iowait = 1;
	if (iowq->timeout != KTIME_MAX || iowq->min_timeout)
		ret = io_cqring_schedule_timeout(iowq, ctx->clockid, start_time);
	else
		schedule();
	current->in_iowait = 0;
	return ret;
}

/* If this returns > 0, the caller should retry */
static inline int io_cqring_wait_schedule(struct io_ring_ctx *ctx,
					  struct io_wait_queue *iowq,
					  struct ext_arg *ext_arg,
					  ktime_t start_time)
{
	if (unlikely(READ_ONCE(ctx->check_cq)))
		return 1;
	if (unlikely(io_local_work_pending(ctx)))
		return 1;
	if (unlikely(task_work_pending(current)))
		return 1;
	if (unlikely(task_sigpending(current)))
		return -EINTR;
	if (unlikely(io_should_wake(iowq)))
		return 0;

	return __io_cqring_wait_schedule(ctx, iowq, ext_arg, start_time);
}

/*
 * Wait until events become available, if we don't already have some. The
 * application must reap them itself, as they reside on the shared cq ring.
 */
int io_cqring_wait(struct io_ring_ctx *ctx, int min_events, u32 flags,
		   struct ext_arg *ext_arg)
{
	struct io_wait_queue iowq;
	struct io_rings *rings = ctx->rings;
	ktime_t start_time;
	int ret;

	min_events = min_t(int, min_events, ctx->cq_entries);

	if (!io_allowed_run_tw(ctx))
		return -EEXIST;
	if (io_local_work_pending(ctx))
		io_run_local_work(ctx, min_events,
				  max(IO_LOCAL_TW_DEFAULT_MAX, min_events));
	io_run_task_work();

	if (unlikely(test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq)))
		io_cqring_do_overflow_flush(ctx);
	if (__io_cqring_events_user(ctx) >= min_events)
		return 0;

	init_waitqueue_func_entry(&iowq.wq, io_wake_function);
	iowq.wq.private = current;
	INIT_LIST_HEAD(&iowq.wq.entry);
	iowq.ctx = ctx;
	iowq.cq_tail = READ_ONCE(ctx->rings->cq.head) + min_events;
	iowq.cq_min_tail = READ_ONCE(ctx->rings->cq.tail);
	iowq.nr_timeouts = atomic_read(&ctx->cq_timeouts);
	iowq.hit_timeout = 0;
	iowq.min_timeout = ext_arg->min_time;
	iowq.timeout = KTIME_MAX;
	start_time = io_get_time(ctx);

	if (ext_arg->ts_set) {
		iowq.timeout = timespec64_to_ktime(ext_arg->ts);
		if (!(flags & IORING_ENTER_ABS_TIMER))
			iowq.timeout = ktime_add(iowq.timeout, start_time);
	}

	if (ext_arg->sig) {
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			ret = set_compat_user_sigmask((const compat_sigset_t __user *)ext_arg->sig,
						      ext_arg->argsz);
		else
#endif
			ret = set_user_sigmask(ext_arg->sig, ext_arg->argsz);

		if (ret)
			return ret;
	}

	io_napi_busy_loop(ctx, &iowq);

	trace_io_uring_cqring_wait(ctx, min_events);
	do {
		unsigned long check_cq;
		int nr_wait;

		/* if min timeout has been hit, don't reset wait count */
		if (!iowq.hit_timeout)
			nr_wait = (int) iowq.cq_tail -
					READ_ONCE(ctx->rings->cq.tail);
		else
			nr_wait = 1;

		if (ctx->flags & IORING_SETUP_DEFER_TASKRUN) {
			atomic_set(&ctx->cq_wait_nr, nr_wait);
			set_current_state(TASK_INTERRUPTIBLE);
		} else {
			prepare_to_wait_exclusive(&ctx->cq_wait, &iowq.wq,
							TASK_INTERRUPTIBLE);
		}

		ret = io_cqring_wait_schedule(ctx, &iowq, ext_arg, start_time);
		__set_current_state(TASK_RUNNING);
		atomic_set(&ctx->cq_wait_nr, IO_CQ_WAKE_INIT);

		/*
		 * Run task_work after scheduling and before io_should_wake().
		 * If we got woken because of task_work being processed, run it
		 * now rather than let the caller do another wait loop.
		 */
		if (io_local_work_pending(ctx))
			io_run_local_work(ctx, nr_wait, nr_wait);
		io_run_task_work();

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
