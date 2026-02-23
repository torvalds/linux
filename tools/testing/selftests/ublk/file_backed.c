// SPDX-License-Identifier: GPL-2.0

#include "kublk.h"

static enum io_uring_op ublk_to_uring_op(const struct ublksrv_io_desc *iod, int zc)
{
	unsigned ublk_op = ublksrv_get_op(iod);

	if (ublk_op == UBLK_IO_OP_READ)
		return zc ? IORING_OP_READ_FIXED : IORING_OP_READ;
	else if (ublk_op == UBLK_IO_OP_WRITE)
		return zc ? IORING_OP_WRITE_FIXED : IORING_OP_WRITE;
	ublk_assert(0);
}

static int loop_queue_flush_io(struct ublk_thread *t, struct ublk_queue *q,
			       const struct ublksrv_io_desc *iod, int tag)
{
	unsigned ublk_op = ublksrv_get_op(iod);
	struct io_uring_sqe *sqe[1];

	ublk_io_alloc_sqes(t, sqe, 1);
	io_uring_prep_fsync(sqe[0], ublk_get_registered_fd(q, 1) /*fds[1]*/, IORING_FSYNC_DATASYNC);
	io_uring_sqe_set_flags(sqe[0], IOSQE_FIXED_FILE);
	/* bit63 marks us as tgt io */
	sqe[0]->user_data = build_user_data(tag, ublk_op, 0, q->q_id, 1);
	return 1;
}

static int loop_queue_tgt_rw_io(struct ublk_thread *t, struct ublk_queue *q,
				const struct ublksrv_io_desc *iod, int tag)
{
	unsigned ublk_op = ublksrv_get_op(iod);
	unsigned zc = ublk_queue_use_zc(q);
	unsigned auto_zc = ublk_queue_use_auto_zc(q);
	enum io_uring_op op = ublk_to_uring_op(iod, zc | auto_zc);
	struct ublk_io *io = ublk_get_io(q, tag);
	__u64 offset = iod->start_sector << 9;
	__u32 len = iod->nr_sectors << 9;
	struct io_uring_sqe *sqe[3];
	void *addr = io->buf_addr;
	unsigned short buf_index = ublk_io_buf_idx(t, q, tag);

	if (iod->op_flags & UBLK_IO_F_INTEGRITY) {
		ublk_io_alloc_sqes(t, sqe, 1);
		/* Use second backing file for integrity data */
		io_uring_prep_rw(op, sqe[0], ublk_get_registered_fd(q, 2),
				 io->integrity_buf,
				 ublk_integrity_len(q, len),
				 ublk_integrity_len(q, offset));
		sqe[0]->flags = IOSQE_FIXED_FILE;
		/* tgt_data = 1 indicates integrity I/O */
		sqe[0]->user_data = build_user_data(tag, ublk_op, 1, q->q_id, 1);
	}

	if (!zc || auto_zc) {
		ublk_io_alloc_sqes(t, sqe, 1);
		if (!sqe[0])
			return -ENOMEM;

		io_uring_prep_rw(op, sqe[0], ublk_get_registered_fd(q, 1) /*fds[1]*/,
				addr,
				len,
				offset);
		if (auto_zc)
			sqe[0]->buf_index = buf_index;
		io_uring_sqe_set_flags(sqe[0], IOSQE_FIXED_FILE);
		/* bit63 marks us as tgt io */
		sqe[0]->user_data = build_user_data(tag, ublk_op, 0, q->q_id, 1);
		return !!(iod->op_flags & UBLK_IO_F_INTEGRITY) + 1;
	}

	ublk_io_alloc_sqes(t, sqe, 3);

	io_uring_prep_buf_register(sqe[0], q, tag, q->q_id, buf_index);
	sqe[0]->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_IO_HARDLINK;
	sqe[0]->user_data = build_user_data(tag,
			ublk_cmd_op_nr(sqe[0]->cmd_op), 0, q->q_id, 1);

	io_uring_prep_rw(op, sqe[1], ublk_get_registered_fd(q, 1) /*fds[1]*/, 0,
			len,
			offset);
	sqe[1]->buf_index = buf_index;
	sqe[1]->flags |= IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK;
	sqe[1]->user_data = build_user_data(tag, ublk_op, 0, q->q_id, 1);

	io_uring_prep_buf_unregister(sqe[2], q, tag, q->q_id, buf_index);
	sqe[2]->user_data = build_user_data(tag, ublk_cmd_op_nr(sqe[2]->cmd_op), 0, q->q_id, 1);

	return !!(iod->op_flags & UBLK_IO_F_INTEGRITY) + 2;
}

static int loop_queue_tgt_io(struct ublk_thread *t, struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned ublk_op = ublksrv_get_op(iod);
	int ret;

	switch (ublk_op) {
	case UBLK_IO_OP_FLUSH:
		ret = loop_queue_flush_io(t, q, iod, tag);
		break;
	case UBLK_IO_OP_WRITE_ZEROES:
	case UBLK_IO_OP_DISCARD:
		ret = -ENOTSUP;
		break;
	case UBLK_IO_OP_READ:
	case UBLK_IO_OP_WRITE:
		ret = loop_queue_tgt_rw_io(t, q, iod, tag);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	ublk_dbg(UBLK_DBG_IO, "%s: tag %d ublk io %x %llx %u\n", __func__, tag,
			iod->op_flags, iod->start_sector, iod->nr_sectors << 9);
	return ret;
}

static int ublk_loop_queue_io(struct ublk_thread *t, struct ublk_queue *q,
			      int tag)
{
	int queued = loop_queue_tgt_io(t, q, tag);

	ublk_queued_tgt_io(t, q, tag, queued);
	return 0;
}

static void ublk_loop_io_done(struct ublk_thread *t, struct ublk_queue *q,
		const struct io_uring_cqe *cqe)
{
	unsigned tag = user_data_to_tag(cqe->user_data);
	unsigned op = user_data_to_op(cqe->user_data);
	struct ublk_io *io = ublk_get_io(q, tag);

	if (cqe->res < 0) {
		io->result = cqe->res;
		ublk_err("%s: io failed op %x user_data %lx\n",
				__func__, op, cqe->user_data);
	} else if (op != ublk_cmd_op_nr(UBLK_U_IO_UNREGISTER_IO_BUF)) {
		__s32 data_len = user_data_to_tgt_data(cqe->user_data)
			? ublk_integrity_data_len(q, cqe->res)
			: cqe->res;

		if (!io->result || data_len < io->result)
			io->result = data_len;
	}

	/* buffer register op is IOSQE_CQE_SKIP_SUCCESS */
	if (op == ublk_cmd_op_nr(UBLK_U_IO_REGISTER_IO_BUF))
		io->tgt_ios += 1;

	if (ublk_completed_tgt_io(t, q, tag))
		ublk_complete_io(t, q, tag, io->result);
}

static int ublk_loop_memset_file(int fd, __u8 byte, size_t len)
{
	off_t offset = 0;
	__u8 buf[4096];

	memset(buf, byte, sizeof(buf));
	while (len) {
		int ret = pwrite(fd, buf, min(len, sizeof(buf)), offset);

		if (ret < 0)
			return -errno;
		if (!ret)
			return -EIO;

		len -= ret;
		offset += ret;
	}
	return 0;
}

static int ublk_loop_tgt_init(const struct dev_ctx *ctx, struct ublk_dev *dev)
{
	unsigned long long bytes;
	unsigned long blocks;
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

	ublk_set_integrity_params(ctx, &p);
	if (ctx->auto_zc_fallback) {
		ublk_err("%s: not support auto_zc_fallback\n", __func__);
		return -EINVAL;
	}

	/* Use O_DIRECT only for data file */
	ret = backing_file_tgt_init(dev, 1);
	if (ret)
		return ret;

	/* Expect a second file for integrity data */
	if (dev->tgt.nr_backing_files != 1 + !!ctx->metadata_size)
		return -EINVAL;

	blocks = dev->tgt.backing_file_size[0] >> p.basic.logical_bs_shift;
	if (ctx->metadata_size) {
		unsigned long metadata_blocks =
			dev->tgt.backing_file_size[1] / ctx->metadata_size;
		unsigned long integrity_len;

		/* Ensure both data and integrity data fit in backing files */
		blocks = min(blocks, metadata_blocks);
		integrity_len = blocks * ctx->metadata_size;
		/*
		 * Initialize PI app tag and ref tag to 0xFF
		 * to disable bio-integrity-auto checks
		 */
		ret = ublk_loop_memset_file(dev->fds[2], 0xFF, integrity_len);
		if (ret)
			return ret;
	}
	bytes = blocks << p.basic.logical_bs_shift;
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
