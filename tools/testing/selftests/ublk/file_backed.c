// SPDX-License-Identifier: GPL-2.0

#include "kublk.h"

static enum io_uring_op ublk_to_uring_op(const struct ublksrv_io_desc *iod, int zc)
{
	unsigned ublk_op = ublksrv_get_op(iod);

	if (ublk_op == UBLK_IO_OP_READ)
		return zc ? IORING_OP_READ_FIXED : IORING_OP_READ;
	else if (ublk_op == UBLK_IO_OP_WRITE)
		return zc ? IORING_OP_WRITE_FIXED : IORING_OP_WRITE;
	assert(0);
}

static int loop_queue_flush_io(struct ublk_queue *q, const struct ublksrv_io_desc *iod, int tag)
{
	unsigned ublk_op = ublksrv_get_op(iod);
	struct io_uring_sqe *sqe[1];

	ublk_queue_alloc_sqes(q, sqe, 1);
	io_uring_prep_fsync(sqe[0], 1 /*fds[1]*/, IORING_FSYNC_DATASYNC);
	io_uring_sqe_set_flags(sqe[0], IOSQE_FIXED_FILE);
	/* bit63 marks us as tgt io */
	sqe[0]->user_data = build_user_data(tag, ublk_op, 0, 1);
	return 1;
}

static int loop_queue_tgt_rw_io(struct ublk_queue *q, const struct ublksrv_io_desc *iod, int tag)
{
	unsigned ublk_op = ublksrv_get_op(iod);
	int zc = ublk_queue_use_zc(q);
	enum io_uring_op op = ublk_to_uring_op(iod, zc);
	struct io_uring_sqe *sqe[3];

	if (!zc) {
		ublk_queue_alloc_sqes(q, sqe, 1);
		if (!sqe[0])
			return -ENOMEM;

		io_uring_prep_rw(op, sqe[0], 1 /*fds[1]*/,
				(void *)iod->addr,
				iod->nr_sectors << 9,
				iod->start_sector << 9);
		io_uring_sqe_set_flags(sqe[0], IOSQE_FIXED_FILE);
		/* bit63 marks us as tgt io */
		sqe[0]->user_data = build_user_data(tag, ublk_op, 0, 1);
		return 1;
	}

	ublk_queue_alloc_sqes(q, sqe, 3);

	io_uring_prep_buf_register(sqe[0], 0, tag, q->q_id, tag);
	sqe[0]->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_IO_HARDLINK;
	sqe[0]->user_data = build_user_data(tag,
			ublk_cmd_op_nr(sqe[0]->cmd_op), 0, 1);

	io_uring_prep_rw(op, sqe[1], 1 /*fds[1]*/, 0,
		iod->nr_sectors << 9,
		iod->start_sector << 9);
	sqe[1]->buf_index = tag;
	sqe[1]->flags |= IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK;
	sqe[1]->user_data = build_user_data(tag, ublk_op, 0, 1);

	io_uring_prep_buf_unregister(sqe[2], 0, tag, q->q_id, tag);
	sqe[2]->user_data = build_user_data(tag, ublk_cmd_op_nr(sqe[2]->cmd_op), 0, 1);

	return 2;
}

static int loop_queue_tgt_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned ublk_op = ublksrv_get_op(iod);
	int ret;

	switch (ublk_op) {
	case UBLK_IO_OP_FLUSH:
		ret = loop_queue_flush_io(q, iod, tag);
		break;
	case UBLK_IO_OP_WRITE_ZEROES:
	case UBLK_IO_OP_DISCARD:
		ret = -ENOTSUP;
		break;
	case UBLK_IO_OP_READ:
	case UBLK_IO_OP_WRITE:
		ret = loop_queue_tgt_rw_io(q, iod, tag);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	ublk_dbg(UBLK_DBG_IO, "%s: tag %d ublk io %x %llx %u\n", __func__, tag,
			iod->op_flags, iod->start_sector, iod->nr_sectors << 9);
	return ret;
}

static int ublk_loop_queue_io(struct ublk_queue *q, int tag)
{
	int queued = loop_queue_tgt_io(q, tag);

	ublk_queued_tgt_io(q, tag, queued);
	return 0;
}

static void ublk_loop_io_done(struct ublk_queue *q, int tag,
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

static int ublk_loop_tgt_init(const struct dev_ctx *ctx, struct ublk_dev *dev)
{
	unsigned long long bytes;
	int ret;
	struct ublk_params p = {
		.types = UBLK_PARAM_TYPE_BASIC | UBLK_PARAM_TYPE_DMA_ALIGN,
		.basic = {
			.attrs = UBLK_ATTR_VOLATILE_CACHE,
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift	= 12,
			.io_min_shift	= 9,
			.max_sectors = dev->dev_info.max_io_buf_bytes >> 9,
		},
		.dma = {
			.alignment = 511,
		},
	};

	ret = backing_file_tgt_init(dev);
	if (ret)
		return ret;

	if (dev->tgt.nr_backing_files != 1)
		return -EINVAL;

	bytes = dev->tgt.backing_file_size[0];
	dev->tgt.dev_size = bytes;
	p.basic.dev_sectors = bytes >> 9;
	dev->tgt.params = p;

	return 0;
}

const struct ublk_tgt_ops loop_tgt_ops = {
	.name = "loop",
	.init_tgt = ublk_loop_tgt_init,
	.deinit_tgt = backing_file_tgt_deinit,
	.queue_io = ublk_loop_queue_io,
	.tgt_io_done = ublk_loop_io_done,
};
