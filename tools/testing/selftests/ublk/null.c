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
		.types = UBLK_PARAM_TYPE_BASIC | UBLK_PARAM_TYPE_DMA_ALIGN |
			UBLK_PARAM_TYPE_SEGMENT,
		.basic = {
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift		= 12,
			.io_min_shift		= 9,
			.max_sectors		= info->max_io_buf_bytes >> 9,
			.dev_sectors		= dev_size >> 9,
		},
		.dma = {
			.alignment 		= 4095,
		},
		.seg = {
			.seg_boundary_mask 	= 4095,
			.max_segment_size 	= 32 << 10,
			.max_segments 		= 32,
		},
	};

	if (info->flags & UBLK_F_SUPPORT_ZERO_COPY)
		dev->tgt.sq_depth = dev->tgt.cq_depth = 2 * info->queue_depth;
	return 0;
}

static void __setup_nop_io(int tag, const struct ublksrv_io_desc *iod,
		struct io_uring_sqe *sqe, int q_id)
{
	unsigned ublk_op = ublksrv_get_op(iod);

	io_uring_prep_nop(sqe);
	sqe->buf_index = tag;
	sqe->flags |= IOSQE_FIXED_FILE;
	sqe->rw_flags = IORING_NOP_FIXED_BUFFER | IORING_NOP_INJECT_RESULT;
	sqe->len = iod->nr_sectors << 9; 	/* injected result */
	sqe->user_data = build_user_data(tag, ublk_op, 0, q_id, 1);
}

static int null_queue_zc_io(struct ublk_thread *t, struct ublk_queue *q,
			    int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	struct io_uring_sqe *sqe[3];

	ublk_io_alloc_sqes(t, sqe, 3);

	io_uring_prep_buf_register(sqe[0], q, tag, q->q_id, ublk_get_io(q, tag)->buf_index);
	sqe[0]->user_data = build_user_data(tag,
			ublk_cmd_op_nr(sqe[0]->cmd_op), 0, q->q_id, 1);
	sqe[0]->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_IO_HARDLINK;

	__setup_nop_io(tag, iod, sqe[1], q->q_id);
	sqe[1]->flags |= IOSQE_IO_HARDLINK;

	io_uring_prep_buf_unregister(sqe[2], q, tag, q->q_id, ublk_get_io(q, tag)->buf_index);
	sqe[2]->user_data = build_user_data(tag, ublk_cmd_op_nr(sqe[2]->cmd_op), 0, q->q_id, 1);

	// buf register is marked as IOSQE_CQE_SKIP_SUCCESS
	return 2;
}

static int null_queue_auto_zc_io(struct ublk_thread *t, struct ublk_queue *q,
				 int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	struct io_uring_sqe *sqe[1];

	ublk_io_alloc_sqes(t, sqe, 1);
	__setup_nop_io(tag, iod, sqe[0], q->q_id);
	return 1;
}

static void ublk_null_io_done(struct ublk_thread *t, struct ublk_queue *q,
			      const struct io_uring_cqe *cqe)
{
	unsigned tag = user_data_to_tag(cqe->user_data);
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

	if (ublk_completed_tgt_io(t, q, tag))
		ublk_complete_io(t, q, tag, io->result);
}

static int ublk_null_queue_io(struct ublk_thread *t, struct ublk_queue *q,
			      int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned auto_zc = ublk_queue_use_auto_zc(q);
	unsigned zc = ublk_queue_use_zc(q);
	int queued;

	if (auto_zc && !ublk_io_auto_zc_fallback(iod))
		queued = null_queue_auto_zc_io(t, q, tag);
	else if (zc)
		queued = null_queue_zc_io(t, q, tag);
	else {
		ublk_complete_io(t, q, tag, iod->nr_sectors << 9);
		return 0;
	}
	ublk_queued_tgt_io(t, q, tag, queued);
	return 0;
}

/*
 * return invalid buffer index for triggering auto buffer register failure,
 * then UBLK_IO_RES_NEED_REG_BUF handling is covered
 */
static unsigned short ublk_null_buf_index(const struct ublk_queue *q, int tag)
{
	if (ublk_queue_auto_zc_fallback(q))
		return (unsigned short)-1;
	return q->ios[tag].buf_index;
}

const struct ublk_tgt_ops null_tgt_ops = {
	.name = "null",
	.init_tgt = ublk_null_tgt_init,
	.queue_io = ublk_null_queue_io,
	.tgt_io_done = ublk_null_io_done,
	.buf_index = ublk_null_buf_index,
};
