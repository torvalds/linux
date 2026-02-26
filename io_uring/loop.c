/* SPDX-License-Identifier: GPL-2.0 */
#include "io_uring.h"
#include "wait.h"
#include "loop.h"

static inline int io_loop_nr_cqes(const struct io_ring_ctx *ctx,
				  const struct iou_loop_params *lp)
{
	return lp->cq_wait_idx - READ_ONCE(ctx->rings->cq.tail);
}

static inline void io_loop_wait_start(struct io_ring_ctx *ctx, unsigned nr_wait)
{
	atomic_set(&ctx->cq_wait_nr, nr_wait);
	set_current_state(TASK_INTERRUPTIBLE);
}

static inline void io_loop_wait_finish(struct io_ring_ctx *ctx)
{
	__set_current_state(TASK_RUNNING);
	atomic_set(&ctx->cq_wait_nr, IO_CQ_WAKE_INIT);
}

static void io_loop_wait(struct io_ring_ctx *ctx, struct iou_loop_params *lp,
			 unsigned nr_wait)
{
	io_loop_wait_start(ctx, nr_wait);

	if (unlikely(io_local_work_pending(ctx) ||
		     io_loop_nr_cqes(ctx, lp) <= 0) ||
		     READ_ONCE(ctx->check_cq)) {
		io_loop_wait_finish(ctx);
		return;
	}

	mutex_unlock(&ctx->uring_lock);
	schedule();
	io_loop_wait_finish(ctx);
	mutex_lock(&ctx->uring_lock);
}

static int __io_run_loop(struct io_ring_ctx *ctx)
{
	struct iou_loop_params lp = {};

	while (true) {
		int nr_wait, step_res;

		if (unlikely(!ctx->loop_step))
			return -EFAULT;

		step_res = ctx->loop_step(ctx, &lp);
		if (step_res == IOU_LOOP_STOP)
			break;
		if (step_res != IOU_LOOP_CONTINUE)
			return -EINVAL;

		nr_wait = io_loop_nr_cqes(ctx, &lp);
		if (nr_wait > 0)
			io_loop_wait(ctx, &lp, nr_wait);
		else
			nr_wait = 0;

		if (task_work_pending(current)) {
			mutex_unlock(&ctx->uring_lock);
			io_run_task_work();
			mutex_lock(&ctx->uring_lock);
		}
		if (unlikely(task_sigpending(current)))
			return -EINTR;
		io_run_local_work_locked(ctx, nr_wait);

		if (READ_ONCE(ctx->check_cq) & BIT(IO_CHECK_CQ_OVERFLOW_BIT))
			io_cqring_overflow_flush_locked(ctx);
	}

	return 0;
}

int io_run_loop(struct io_ring_ctx *ctx)
{
	int ret;

	if (!io_allowed_run_tw(ctx))
		return -EEXIST;

	mutex_lock(&ctx->uring_lock);
	ret = __io_run_loop(ctx);
	mutex_unlock(&ctx->uring_lock);
	return ret;
}
