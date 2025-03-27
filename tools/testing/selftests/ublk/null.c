/* SPDX-License-Identifier: GPL-2.0 */

#include "kublk.h"

#ifndef IORING_NOP_INJECT_RESULT
#define IORING_NOP_INJECT_RESULT        (1U << 0)
#endif

#ifndef IORING_NOP_FIXED_BUFFER
#define IORING_NOP_FIXED_BUFFER         (1U << 3)
#endif

static int ublk_null_tgt_init(const struct dev_ctx *ctx, struct ublk_dev *dev)
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

	if (info->flags & UBLK_F_SUPPORT_ZERO_COPY)
		dev->tgt.sq_depth = dev->tgt.cq_depth = 2 * info->queue_depth;
	return 0;
}

static int null_queue_zc_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned ublk_op = ublksrv_get_op(iod);
	struct io_uring_sqe *sqe[3];

	ublk_queue_alloc_sqes(q, sqe, 3);

	io_uring_prep_buf_register(sqe[0], 0, tag, q->q_id, tag);
	sqe[0]->user_data = build_user_data(tag,
			ublk_cmd_op_nr(sqe[0]->cmd_op), 0, 1);
	sqe[0]->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_IO_HARDLINK;

	io_uring_prep_nop(sqe[1]);
	sqe[1]->buf_index = tag;
	sqe[1]->flags |= IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK;
	sqe[1]->rw_flags = IORING_NOP_FIXED_BUFFER | IORING_NOP_INJECT_RESULT;
	sqe[1]->len = iod->nr_sectors << 9; 	/* injected result */
	sqe[1]->user_data = build_user_data(tag, ublk_op, 0, 1);

	io_uring_prep_buf_unregister(sqe[2], 0, tag, q->q_id, tag);
	sqe[2]->user_data = build_user_data(tag, ublk_cmd_op_nr(sqe[2]->cmd_op), 0, 1);

	// buf register is marked as IOSQE_CQE_SKIP_SUCCESS
	return 2;
}

static void ublk_null_io_done(struct ublk_queue *q, int tag,
		const struct io_uring_cqe *cqe)
{
	unsigned op = user_data_to_op(cqe->user_data);
	struct ublk_io *io = ublk_get_io(q, tag);

	if (cqe->res < 0 || op != ublk_cmd_op_nr(UBLK_U_IO_UNREGISTER_IO_BUF)) {
		if (!io->result)
			io->result = cqe->res;
		if (cqe->res < 0)
			ublk_err("%s: io failed op %x user_data %lx\n",
					__func__, op, cqe->user_data);
	}

	/* buffer register op is IOSQE_CQE_SKIP_SUCCESS */
	if (op == ublk_cmd_op_nr(UBLK_U_IO_REGISTER_IO_BUF))
		io->tgt_ios += 1;

	if (ublk_completed_tgt_io(q, tag))
		ublk_complete_io(q, tag, io->result);
}

static int ublk_null_queue_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	int zc = ublk_queue_use_zc(q);
	int queued;

	if (!zc) {
		ublk_complete_io(q, tag, iod->nr_sectors << 9);
		return 0;
	}

	queued = null_queue_zc_io(q, tag);
	ublk_queued_tgt_io(q, tag, queued);
	return 0;
}

const struct ublk_tgt_ops null_tgt_ops = {
	.name = "null",
	.init_tgt = ublk_null_tgt_init,
	.queue_io = ublk_null_queue_io,
	.tgt_io_done = ublk_null_io_done,
};
