// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_WAIT_H
#define IOU_WAIT_H

#include <linux/io_uring_types.h>

/*
 * No waiters. It's larger than any valid value of the tw counter
 * so that tests against ->cq_wait_nr would fail and skip wake_up().
 */
#define IO_CQ_WAKE_INIT		(-1U)
/* Forced wake up if there is a waiter regardless of ->cq_wait_nr */
#define IO_CQ_WAKE_FORCE	(IO_CQ_WAKE_INIT >> 1)

struct ext_arg {
	size_t argsz;
	struct timespec64 ts;
	const sigset_t __user *sig;
	ktime_t min_time;
	bool ts_set;
	bool iowait;
};

int io_cqring_wait(struct io_ring_ctx *ctx, int min_events, u32 flags,
		   struct ext_arg *ext_arg);
int io_run_task_work_sig(struct io_ring_ctx *ctx);
void io_cqring_do_overflow_flush(struct io_ring_ctx *ctx);

static inline unsigned int __io_cqring_events(struct io_ring_ctx *ctx)
{
	return ctx->cached_cq_tail - READ_ONCE(ctx->rings->cq.head);
}

static inline unsigned int __io_cqring_events_user(struct io_ring_ctx *ctx)
{
	return READ_ONCE(ctx->rings->cq.tail) - READ_ONCE(ctx->rings->cq.head);
}

/*
 * Reads the tail/head of the CQ ring while providing an acquire ordering,
 * see comment at top of io_uring.c.
 */
static inline unsigned io_cqring_events(struct io_ring_ctx *ctx)
{
	smp_rmb();
	return __io_cqring_events(ctx);
}

#endif
