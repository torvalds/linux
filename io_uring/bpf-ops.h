// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_BPF_OPS_H
#define IOU_BPF_OPS_H

#include <linux/io_uring_types.h>

enum {
	IOU_REGION_MEM,
	IOU_REGION_CQ,
	IOU_REGION_SQ,
};

struct io_uring_bpf_ops {
	int (*loop_step)(struct io_ring_ctx *ctx, struct iou_loop_params *lp);

	__u32 ring_fd;
	void *priv;
};

#ifdef CONFIG_IO_URING_BPF_OPS
void io_unregister_bpf_ops(struct io_ring_ctx *ctx);
#else
static inline void io_unregister_bpf_ops(struct io_ring_ctx *ctx)
{
}
#endif

#endif /* IOU_BPF_OPS_H */
