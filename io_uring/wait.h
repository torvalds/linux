// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_WAIT_H
#define IOU_WAIT_H

#include <linux/io_uring_types.h>

/*
 * ->cq_wait_nr is armed with the number of lazy task_work adds the waiter
 * still needs, and counted down by the add side, with the add reaching zero
 * issuing the (single) wake up for this wait cycle. Zero and below means no
 * wake up is to be issued: IO_CQ_WAKE_INIT when no task is waiting (also
 * what a forced wake up resets it to when claiming one), zero once the
 * countdown has fired.
 */
#define IO_CQ_WAKE_INIT		(-1)

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
void io_cqring_overflow_flush_locked(struct io_ring_ctx *ctx);

static inline unsigned int __io_cqring_events(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = io_get_rings(ctx);
	return ctx->cached_cq_tail - READ_ONCE(rings->cq.head);
}

static inline unsigned int __io_cqring_events_user(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = io_get_rings(ctx);

	return READ_ONCE(rings->cq.tail) - READ_ONCE(rings->cq.head);
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
