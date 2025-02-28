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

static int loop_queue_tgt_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	struct io_uring_sqe *sqe = ublk_queue_alloc_sqe(q);
	unsigned ublk_op = ublksrv_get_op(iod);

	if (!sqe)
		return -ENOMEM;

	switch (ublk_op) {
	case UBLK_IO_OP_FLUSH:
		io_uring_prep_sync_file_range(sqe, 1 /*fds[1]*/,
				iod->nr_sectors << 9,
				iod->start_sector << 9,
				IORING_FSYNC_DATASYNC);
		io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
		break;
	case UBLK_IO_OP_WRITE_ZEROES:
	case UBLK_IO_OP_DISCARD:
		return -ENOTSUP;
	case UBLK_IO_OP_READ:
		io_uring_prep_read(sqe, 1 /*fds[1]*/,
				(void *)iod->addr,
				iod->nr_sectors << 9,
				iod->start_sector << 9);
		io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
		break;
	case UBLK_IO_OP_WRITE:
		io_uring_prep_write(sqe, 1 /*fds[1]*/,
				(void *)iod->addr,
				iod->nr_sectors << 9,
				iod->start_sector << 9);
		io_uring_sqe_set_flags(sqe, IOSQE_FIXED_FILE);
		break;
	default:
		return -EINVAL;
	}

	q->io_inflight++;
	/* bit63 marks us as tgt io */
	sqe->user_data = build_user_data(tag, ublk_op, 0, 1);

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

	assert(tag == cqe_tag);
	ublk_complete_io(q, tag, cqe->res);
	q->io_inflight--;
}

static int ublk_loop_tgt_init(struct ublk_dev *dev)
{
	unsigned long long bytes;
	int ret;
	struct ublk_params p = {
		.types = UBLK_PARAM_TYPE_BASIC,
		.basic = {
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift	= 12,
			.io_min_shift	= 9,
			.max_sectors = dev->dev_info.max_io_buf_bytes >> 9,
		},
	};

	assert(dev->tgt.nr_backing_files == 1);
	ret = backing_file_tgt_init(dev);
	if (ret)
		return ret;

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
