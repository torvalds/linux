// SPDX-License-Identifier: GPL-2.0

/*
 * Fault injection ublk target. Hack this up however you like for
 * testing specific behaviors of ublk_drv. Currently is a null target
 * with a configurable delay before completing each I/O. This delay can
 * be used to test ublk_drv's handling of I/O outstanding to the ublk
 * server when it dies.
 */

#include "kublk.h"

static int ublk_fault_inject_tgt_init(const struct dev_ctx *ctx,
				      struct ublk_dev *dev)
{
	const struct ublksrv_ctrl_dev_info *info = &dev->dev_info;
	unsigned long dev_size = 250UL << 30;

	dev->tgt.dev_size = dev_size;
	dev->tgt.params = (struct ublk_params) {
		.types = UBLK_PARAM_TYPE_BASIC,
		.basic = {
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift		= 12,
			.io_min_shift		= 9,
			.max_sectors		= info->max_io_buf_bytes >> 9,
			.dev_sectors		= dev_size >> 9,
		},
	};

	dev->private_data = (void *)(unsigned long)(ctx->fault_inject.delay_us * 1000);
	return 0;
}

static int ublk_fault_inject_queue_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	struct io_uring_sqe *sqe;
	struct __kernel_timespec ts = {
		.tv_nsec = (long long)q->dev->private_data,
	};

	ublk_queue_alloc_sqes(q, &sqe, 1);
	io_uring_prep_timeout(sqe, &ts, 1, 0);
	sqe->user_data = build_user_data(tag, ublksrv_get_op(iod), 0, 1);

	ublk_queued_tgt_io(q, tag, 1);

	return 0;
}

static void ublk_fault_inject_tgt_io_done(struct ublk_queue *q, int tag,
					  const struct io_uring_cqe *cqe)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);

	if (cqe->res != -ETIME)
		ublk_err("%s: unexpected cqe res %d\n", __func__, cqe->res);

	if (ublk_completed_tgt_io(q, tag))
		ublk_complete_io(q, tag, iod->nr_sectors << 9);
	else
		ublk_err("%s: io not complete after 1 cqe\n", __func__);
}

static void ublk_fault_inject_cmd_line(struct dev_ctx *ctx, int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "delay_us", 	1,	NULL,  0  },
		{ 0, 0, 0, 0 }
	};
	int option_idx, opt;

	ctx->fault_inject.delay_us = 0;
	while ((opt = getopt_long(argc, argv, "",
				  longopts, &option_idx)) != -1) {
		switch (opt) {
		case 0:
			if (!strcmp(longopts[option_idx].name, "delay_us"))
				ctx->fault_inject.delay_us = strtoll(optarg, NULL, 10);
		}
	}
}

static void ublk_fault_inject_usage(const struct ublk_tgt_ops *ops)
{
	printf("\tfault_inject: [--delay_us us (default 0)]\n");
}

const struct ublk_tgt_ops fault_inject_tgt_ops = {
	.name = "fault_inject",
	.init_tgt = ublk_fault_inject_tgt_init,
	.queue_io = ublk_fault_inject_queue_io,
	.tgt_io_done = ublk_fault_inject_tgt_io_done,
	.parse_cmd_line = ublk_fault_inject_cmd_line,
	.usage = ublk_fault_inject_usage,
};
