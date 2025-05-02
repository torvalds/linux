// SPDX-License-Identifier: GPL-2.0

#include "kublk.h"

#define NR_STRIPE  MAX_BACK_FILES

struct stripe_conf {
	unsigned nr_files;
	unsigned shift;
};

struct stripe {
	loff_t 		start;
	unsigned 	nr_sects;
	int 		seq;

	struct iovec 	*vec;
	unsigned 	nr_vec;
	unsigned 	cap;
};

struct stripe_array {
	struct stripe 	s[NR_STRIPE];
	unsigned 	nr;
	struct iovec 	_vec[];
};

static inline const struct stripe_conf *get_chunk_shift(const struct ublk_queue *q)
{
	return (struct stripe_conf *)q->dev->private_data;
}

static inline unsigned calculate_nr_vec(const struct stripe_conf *conf,
		const struct ublksrv_io_desc *iod)
{
	const unsigned shift = conf->shift - 9;
	const unsigned unit_sects = conf->nr_files << shift;
	loff_t start = iod->start_sector;
	loff_t end = start + iod->nr_sectors;

	return (end / unit_sects) - (start / unit_sects) + 1;
}

static struct stripe_array *alloc_stripe_array(const struct stripe_conf *conf,
		const struct ublksrv_io_desc *iod)
{
	unsigned nr_vecs = calculate_nr_vec(conf, iod);
	unsigned total = nr_vecs * conf->nr_files;
	struct stripe_array *s;
	int i;

	s = malloc(sizeof(*s) + total * sizeof(struct iovec));

	s->nr = 0;
	for (i = 0; i < conf->nr_files; i++) {
		struct stripe *t = &s->s[i];

		t->nr_vec = 0;
		t->vec = &s->_vec[i * nr_vecs];
		t->nr_sects = 0;
		t->cap = nr_vecs;
	}

	return s;
}

static void free_stripe_array(struct stripe_array *s)
{
	free(s);
}

static void calculate_stripe_array(const struct stripe_conf *conf,
		const struct ublksrv_io_desc *iod, struct stripe_array *s)
{
	const unsigned shift = conf->shift - 9;
	const unsigned chunk_sects = 1 << shift;
	const unsigned unit_sects = conf->nr_files << shift;
	off64_t start = iod->start_sector;
	off64_t end = start + iod->nr_sectors;
	unsigned long done = 0;
	unsigned idx = 0;

	while (start < end) {
		unsigned nr_sects = chunk_sects - (start & (chunk_sects - 1));
		loff_t unit_off = (start / unit_sects) * unit_sects;
		unsigned seq = (start - unit_off) >> shift;
		struct stripe *this = &s->s[idx];
		loff_t stripe_off = (unit_off / conf->nr_files) +
			(start & (chunk_sects - 1));

		if (nr_sects > end - start)
			nr_sects = end - start;
		if (this->nr_sects == 0) {
			this->nr_sects = nr_sects;
			this->start = stripe_off;
			this->seq = seq;
			s->nr += 1;
		} else {
			assert(seq == this->seq);
			assert(this->start + this->nr_sects == stripe_off);
			this->nr_sects += nr_sects;
		}

		assert(this->nr_vec < this->cap);
		this->vec[this->nr_vec].iov_base = (void *)(iod->addr + done);
		this->vec[this->nr_vec++].iov_len = nr_sects << 9;

		start += nr_sects;
		done += nr_sects << 9;
		idx = (idx + 1) % conf->nr_files;
	}
}

static inline enum io_uring_op stripe_to_uring_op(
		const struct ublksrv_io_desc *iod, int zc)
{
	unsigned ublk_op = ublksrv_get_op(iod);

	if (ublk_op == UBLK_IO_OP_READ)
		return zc ? IORING_OP_READV_FIXED : IORING_OP_READV;
	else if (ublk_op == UBLK_IO_OP_WRITE)
		return zc ? IORING_OP_WRITEV_FIXED : IORING_OP_WRITEV;
	assert(0);
}

static int stripe_queue_tgt_rw_io(struct ublk_queue *q, const struct ublksrv_io_desc *iod, int tag)
{
	const struct stripe_conf *conf = get_chunk_shift(q);
	int zc = !!(ublk_queue_use_zc(q) != 0);
	enum io_uring_op op = stripe_to_uring_op(iod, zc);
	struct io_uring_sqe *sqe[NR_STRIPE];
	struct stripe_array *s = alloc_stripe_array(conf, iod);
	struct ublk_io *io = ublk_get_io(q, tag);
	int i, extra = zc ? 2 : 0;

	io->private_data = s;
	calculate_stripe_array(conf, iod, s);

	ublk_queue_alloc_sqes(q, sqe, s->nr + extra);

	if (zc) {
		io_uring_prep_buf_register(sqe[0], 0, tag, q->q_id, tag);
		sqe[0]->flags |= IOSQE_CQE_SKIP_SUCCESS | IOSQE_IO_HARDLINK;
		sqe[0]->user_data = build_user_data(tag,
			ublk_cmd_op_nr(sqe[0]->cmd_op), 0, 1);
	}

	for (i = zc; i < s->nr + extra - zc; i++) {
		struct stripe *t = &s->s[i - zc];

		io_uring_prep_rw(op, sqe[i],
				t->seq + 1,
				(void *)t->vec,
				t->nr_vec,
				t->start << 9);
		if (zc) {
			sqe[i]->buf_index = tag;
			io_uring_sqe_set_flags(sqe[i],
					IOSQE_FIXED_FILE | IOSQE_IO_HARDLINK);
		} else {
			io_uring_sqe_set_flags(sqe[i], IOSQE_FIXED_FILE);
		}
		/* bit63 marks us as tgt io */
		sqe[i]->user_data = build_user_data(tag, ublksrv_get_op(iod), i - zc, 1);
	}
	if (zc) {
		struct io_uring_sqe *unreg = sqe[s->nr + 1];

		io_uring_prep_buf_unregister(unreg, 0, tag, q->q_id, tag);
		unreg->user_data = build_user_data(tag, ublk_cmd_op_nr(unreg->cmd_op), 0, 1);
	}

	/* register buffer is skip_success */
	return s->nr + zc;
}

static int handle_flush(struct ublk_queue *q, const struct ublksrv_io_desc *iod, int tag)
{
	const struct stripe_conf *conf = get_chunk_shift(q);
	struct io_uring_sqe *sqe[NR_STRIPE];
	int i;

	ublk_queue_alloc_sqes(q, sqe, conf->nr_files);
	for (i = 0; i < conf->nr_files; i++) {
		io_uring_prep_fsync(sqe[i], i + 1, IORING_FSYNC_DATASYNC);
		io_uring_sqe_set_flags(sqe[i], IOSQE_FIXED_FILE);
		sqe[i]->user_data = build_user_data(tag, UBLK_IO_OP_FLUSH, 0, 1);
	}
	return conf->nr_files;
}

static int stripe_queue_tgt_io(struct ublk_queue *q, int tag)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned ublk_op = ublksrv_get_op(iod);
	int ret = 0;

	switch (ublk_op) {
	case UBLK_IO_OP_FLUSH:
		ret = handle_flush(q, iod, tag);
		break;
	case UBLK_IO_OP_WRITE_ZEROES:
	case UBLK_IO_OP_DISCARD:
		ret = -ENOTSUP;
		break;
	case UBLK_IO_OP_READ:
	case UBLK_IO_OP_WRITE:
		ret = stripe_queue_tgt_rw_io(q, iod, tag);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	ublk_dbg(UBLK_DBG_IO, "%s: tag %d ublk io %x %llx %u ret %d\n", __func__, tag,
			iod->op_flags, iod->start_sector, iod->nr_sectors << 9, ret);
	return ret;
}

static int ublk_stripe_queue_io(struct ublk_queue *q, int tag)
{
	int queued = stripe_queue_tgt_io(q, tag);

	ublk_queued_tgt_io(q, tag, queued);
	return 0;
}

static void ublk_stripe_io_done(struct ublk_queue *q, int tag,
		const struct io_uring_cqe *cqe)
{
	const struct ublksrv_io_desc *iod = ublk_get_iod(q, tag);
	unsigned op = user_data_to_op(cqe->user_data);
	struct ublk_io *io = ublk_get_io(q, tag);
	int res = cqe->res;

	if (res < 0 || op != ublk_cmd_op_nr(UBLK_U_IO_UNREGISTER_IO_BUF)) {
		if (!io->result)
			io->result = res;
		if (res < 0)
			ublk_err("%s: io failure %d tag %u\n", __func__, res, tag);
	}

	/* buffer register op is IOSQE_CQE_SKIP_SUCCESS */
	if (op == ublk_cmd_op_nr(UBLK_U_IO_REGISTER_IO_BUF))
		io->tgt_ios += 1;

	/* fail short READ/WRITE simply */
	if (op == UBLK_IO_OP_READ || op == UBLK_IO_OP_WRITE) {
		unsigned seq = user_data_to_tgt_data(cqe->user_data);
		struct stripe_array *s = io->private_data;

		if (res < s->s[seq].nr_sects << 9) {
			io->result = -EIO;
			ublk_err("%s: short rw op %u res %d exp %u tag %u\n",
					__func__, op, res, s->s[seq].vec->iov_len, tag);
		}
	}

	if (ublk_completed_tgt_io(q, tag)) {
		int res = io->result;

		if (!res)
			res = iod->nr_sectors << 9;

		ublk_complete_io(q, tag, res);

		free_stripe_array(io->private_data);
		io->private_data = NULL;
	}
}

static int ublk_stripe_tgt_init(const struct dev_ctx *ctx, struct ublk_dev *dev)
{
	struct ublk_params p = {
		.types = UBLK_PARAM_TYPE_BASIC,
		.basic = {
			.attrs = UBLK_ATTR_VOLATILE_CACHE,
			.logical_bs_shift	= 9,
			.physical_bs_shift	= 12,
			.io_opt_shift	= 12,
			.io_min_shift	= 9,
			.max_sectors = dev->dev_info.max_io_buf_bytes >> 9,
		},
	};
	unsigned chunk_size = ctx->stripe.chunk_size;
	struct stripe_conf *conf;
	unsigned chunk_shift;
	loff_t bytes = 0;
	int ret, i, mul = 1;

	if ((chunk_size & (chunk_size - 1)) || !chunk_size) {
		ublk_err("invalid chunk size %u\n", chunk_size);
		return -EINVAL;
	}

	if (chunk_size < 4096 || chunk_size > 512 * 1024) {
		ublk_err("invalid chunk size %u\n", chunk_size);
		return -EINVAL;
	}

	chunk_shift = ilog2(chunk_size);

	ret = backing_file_tgt_init(dev);
	if (ret)
		return ret;

	if (!dev->tgt.nr_backing_files || dev->tgt.nr_backing_files > NR_STRIPE)
		return -EINVAL;

	assert(dev->nr_fds == dev->tgt.nr_backing_files + 1);

	for (i = 0; i < dev->tgt.nr_backing_files; i++)
		dev->tgt.backing_file_size[i] &= ~((1 << chunk_shift) - 1);

	for (i = 0; i < dev->tgt.nr_backing_files; i++) {
		unsigned long size = dev->tgt.backing_file_size[i];

		if (size != dev->tgt.backing_file_size[0])
			return -EINVAL;
		bytes += size;
	}

	conf = malloc(sizeof(*conf));
	conf->shift = chunk_shift;
	conf->nr_files = dev->tgt.nr_backing_files;

	dev->private_data = conf;
	dev->tgt.dev_size = bytes;
	p.basic.dev_sectors = bytes >> 9;
	dev->tgt.params = p;

	if (dev->dev_info.flags & UBLK_F_SUPPORT_ZERO_COPY)
		mul = 2;
	dev->tgt.sq_depth = mul * dev->dev_info.queue_depth * conf->nr_files;
	dev->tgt.cq_depth = mul * dev->dev_info.queue_depth * conf->nr_files;

	printf("%s: shift %u files %u\n", __func__, conf->shift, conf->nr_files);

	return 0;
}

static void ublk_stripe_tgt_deinit(struct ublk_dev *dev)
{
	free(dev->private_data);
	backing_file_tgt_deinit(dev);
}

static void ublk_stripe_cmd_line(struct dev_ctx *ctx, int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "chunk_size", 	1,	NULL,  0  },
		{ 0, 0, 0, 0 }
	};
	int option_idx, opt;

	ctx->stripe.chunk_size = 65536;
	while ((opt = getopt_long(argc, argv, "",
				  longopts, &option_idx)) != -1) {
		switch (opt) {
		case 0:
			if (!strcmp(longopts[option_idx].name, "chunk_size"))
				ctx->stripe.chunk_size = strtol(optarg, NULL, 10);
		}
	}
}

static void ublk_stripe_usage(const struct ublk_tgt_ops *ops)
{
	printf("\tstripe: [--chunk_size chunk_size (default 65536)]\n");
}

const struct ublk_tgt_ops stripe_tgt_ops = {
	.name = "stripe",
	.init_tgt = ublk_stripe_tgt_init,
	.deinit_tgt = ublk_stripe_tgt_deinit,
	.queue_io = ublk_stripe_queue_io,
	.tgt_io_done = ublk_stripe_io_done,
	.parse_cmd_line = ublk_stripe_cmd_line,
	.usage = ublk_stripe_usage,
};
