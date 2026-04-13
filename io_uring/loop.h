// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_LOOP_H
#define IOU_LOOP_H

#include <linux/io_uring_types.h>

struct iou_loop_params {
	/*
	 * The CQE index to wait for. Only serves as a hint and can still be
	 * woken up earlier.
	 */
	__u32			cq_wait_idx;
};

enum {
	IOU_LOOP_CONTINUE = 0,
	IOU_LOOP_STOP,
};

static inline bool io_has_loop_ops(struct io_ring_ctx *ctx)
{
	return data_race(ctx->loop_step);
}

int io_run_loop(struct io_ring_ctx *ctx);

#endif
