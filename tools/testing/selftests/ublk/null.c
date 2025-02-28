/* SPDX-License-Identifier: GPL-2.0 */

#include "kublk.h"

static int ublk_null_tgt_init(struct ublk_dev *dev)
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

	return 0;
}

static int ublk_null_queue_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);

	ublk_complete_io(q, tag, iod->nr_sectors << 9);
	return 0;
}

const struct ublk_tgt_ops null_tgt_ops = {
	.name = "null",
	.init_tgt = ublk_null_tgt_init,
	.queue_io = ublk_null_queue_io,
};
