// SPDX-License-Identifier: GPL-2.0

#include "kublk.h"

static void backing_file_tgt_deinit(struct ublk_dev *dev)
{
	int i;

	for (i = 1; i < dev->nr_fds; i++) {
		fsync(dev->fds[i]);
		close(dev->fds[i]);
	}
}

static int backing_file_tgt_init(struct ublk_dev *dev)
{
	int fd, i;

	assert(dev->nr_fds == 1);

	for (i = 0; i < dev->tgt.nr_backing_files; i++) {
		char *file = dev->tgt.backing_file[i];
		unsigned long bytes;
		struct stat st;

		ublk_dbg(UBLK_DBG_DEV, "%s: file %d: %s\n", __func__, i, file);

		fd = open(file, O_RDWR | O_DIRECT);
		if (fd < 0) {
			ublk_err("%s: backing file %s can't be opened: %s\n",
					__func__, file, strerror(errno));
			return -EBADF;
		}

		if (fstat(fd, &st) < 0) {
			close(fd);
			return -EBADF;
		}

		if (S_ISREG(st.st_mode))
			bytes = st.st_size;
		else if (S_ISBLK(st.st_mode)) {
			if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
				return -1;
		} else {
			return -EINVAL;
		}

		dev->tgt.backing_file_size[i] = bytes;
		dev->fds[dev->nr_fds] = fd;
		dev->nr_fds += 1;
	}

	return 0;
}

static enum io_uring_op ublk_to_uring_op(const struct ublksrv_io_desc *iod, int zc)
{
	unsigned ublk_op = ublksrv_get_op(iod);

	if (ublk_op == UBLK_IO_OP_READ)
		return zc ? IORING_OP_READ_FIXED : IORING_OP_READ;
	else if (ublk_op == UBLK_IO_OP_WRITE)
		return zc ? IORING_OP_WRITE_FIXED : IORING_OP_WRITE;
	assert(0);
}

static int loop_queue_tgt_rw_io(struct ublk_queue *q, const struct ublksrv_io_desc *iod, int tag)
{
	int zc = ublk_queue_use_zc(q);
	enum io_uring_op op = ublk_to_uring_op(iod, zc);
	struct io_uring_sqe *reg;
	struct io_uring_sqe *rw;
	struct io_uring_sqe *ureg;

	if (!zc) {
		rw = ublk_queue_alloc_sqe(q);
		if (!rw)
			return -ENOMEM;

		io_uring_prep_rw(op, rw, 1 /*fds[1]*/,
				(void *)iod->addr,
				iod->nr_sectors << 9,
				iod->start_sector << 9);
		io_uring_sqe_set_flags(rw, IOSQE_FIXED_FILE);
		q->io_inflight++;
		/* bit63 marks us as tgt io */
		rw->user_data = build_user_data(tag, op, UBLK_IO_TGT_NORMAL, 1);
		return 0;
	}

	ublk_queue_alloc_sqe3(q, &reg, &rw, &ureg);

	io_uring_prep_buf_register(reg, 0, tag, q->q_id, tag);
	reg->user_data = build_user_data(tag, 0xfe, 1, 1);
	reg->flags |= IOSQE_CQE_SKIP_SUCCESS;
	reg->flags |= IOSQE_IO_LINK;

	io_uring_prep_rw(op, rw, 1 /*fds[1]*/, 0,
		iod->nr_sectors << 9,
		iod->start_sector << 9);
	rw->buf_index = tag;
	rw->flags |= IOSQE_FIXED_FILE;
	rw->flags |= IOSQE_IO_LINK;
	rw->user_data = build_user_data(tag, op, UBLK_IO_TGT_ZC_OP, 1);
	q->io_inflight++;

	io_uring_prep_buf_unregister(ureg, 0, tag, q->q_id, tag);
	ureg->user_data = build_user_data(tag, 0xff, UBLK_IO_TGT_ZC_BUF, 1);
	q->io_inflight++;

	return 0;
}

static int loop_queue_tgt_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned ublk_op = ublksrv_get_op(iod);
	struct io_uring_sqe *sqe;

	switch (ublk_op) {
	case UBLK_IO_OP_FLUSH:
		sqe = ublk_queue_alloc_sqe(q);
		if (!sqe)
			return -ENOMEM;
		io_uring_prep_fsync(sqe, 1 /*fds[1]*/, IORING_FSYNC_DATASYNC);
		io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
		q->io_inflight++;
		sqe->user_data = build_user_data(tag, ublk_op, UBLK_IO_TGT_NORMAL, 1);
		break;
	case UBLK_IO_OP_WRITE_ZEROES:
	case UBLK_IO_OP_DISCARD:
		return -ENOTSUP;
	case UBLK_IO_OP_READ:
	case UBLK_IO_OP_WRITE:
		loop_queue_tgt_rw_io(q, iod, tag);
		break;
	default:
		return -EINVAL;
	}

	ublk_dbg(UBLK_DBG_IO, "%s: tag %d ublk io %x %llx %u\n", __func__, tag,
			iod->op_flags, iod->start_sector, iod->nr_sectors << 9);
	return 1;
}

static int ublk_loop_queue_io(struct ublk_queue *q, int tag)
{
	int queued = loop_queue_tgt_io(q, tag);

	if (queued < 0)
		ublk_complete_io(q, tag, queued);

	return 0;
}

static void ublk_loop_io_done(struct ublk_queue *q, int tag,
		const struct io_uring_cqe *cqe)
{
	int cqe_tag = user_data_to_tag(cqe->user_data);
	unsigned tgt_data = user_data_to_tgt_data(cqe->user_data);
	int res = cqe->res;

	if (res < 0 || tgt_data == UBLK_IO_TGT_NORMAL)
		goto complete;

	if (tgt_data == UBLK_IO_TGT_ZC_OP) {
		ublk_set_io_res(q, tag, cqe->res);
		goto exit;
	}
	assert(tgt_data == UBLK_IO_TGT_ZC_BUF);
	res = ublk_get_io_res(q, tag);
complete:
	assert(tag == cqe_tag);
	ublk_complete_io(q, tag, res);
exit:
	q->io_inflight--;
}

static int ublk_loop_tgt_init(struct ublk_dev *dev)
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
