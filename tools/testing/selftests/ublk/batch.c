/* SPDX-License-Identifier: MIT */
/*
 * Description: UBLK_F_BATCH_IO buffer management
 */

#include "kublk.h"

static inline void *ublk_get_commit_buf(struct ublk_thread *t,
					unsigned short buf_idx)
{
	unsigned idx;

	if (buf_idx < t->commit_buf_start ||
			buf_idx >= t->commit_buf_start + t->nr_commit_buf)
		return NULL;
	idx = buf_idx - t->commit_buf_start;
	return t->commit_buf + idx * t->commit_buf_size;
}

/*
 * Allocate one buffer for UBLK_U_IO_PREP_IO_CMDS or UBLK_U_IO_COMMIT_IO_CMDS
 *
 * Buffer index is returned.
 */
static inline unsigned short ublk_alloc_commit_buf(struct ublk_thread *t)
{
	int idx = allocator_get(&t->commit_buf_alloc);

	if (idx >= 0)
		return  idx + t->commit_buf_start;
	return UBLKS_T_COMMIT_BUF_INV_IDX;
}

/*
 * Free one commit buffer which is used by UBLK_U_IO_PREP_IO_CMDS or
 * UBLK_U_IO_COMMIT_IO_CMDS
 */
static inline void ublk_free_commit_buf(struct ublk_thread *t,
					 unsigned short i)
{
	unsigned short idx = i - t->commit_buf_start;

	ublk_assert(idx < t->nr_commit_buf);
	ublk_assert(allocator_get_val(&t->commit_buf_alloc, idx) != 0);

	allocator_put(&t->commit_buf_alloc, idx);
}

static unsigned char ublk_commit_elem_buf_size(struct ublk_dev *dev)
{
	if (dev->dev_info.flags & (UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_USER_COPY |
				UBLK_F_AUTO_BUF_REG))
		return 8;

	/* one extra 8bytes for carrying buffer address */
	return 16;
}

static unsigned ublk_commit_buf_size(struct ublk_thread *t)
{
	struct ublk_dev *dev = t->dev;
	unsigned elem_size = ublk_commit_elem_buf_size(dev);
	unsigned int total = elem_size * dev->dev_info.queue_depth;
	unsigned int page_sz = getpagesize();

	return round_up(total, page_sz);
}

static void free_batch_commit_buf(struct ublk_thread *t)
{
	if (t->commit_buf) {
		unsigned buf_size = ublk_commit_buf_size(t);
		unsigned int total = buf_size * t->nr_commit_buf;

		munlock(t->commit_buf, total);
		free(t->commit_buf);
	}
	allocator_deinit(&t->commit_buf_alloc);
}

static int alloc_batch_commit_buf(struct ublk_thread *t)
{
	unsigned buf_size = ublk_commit_buf_size(t);
	unsigned int total = buf_size * t->nr_commit_buf;
	unsigned int page_sz = getpagesize();
	void *buf = NULL;
	int ret;

	allocator_init(&t->commit_buf_alloc, t->nr_commit_buf);

	t->commit_buf = NULL;
	ret = posix_memalign(&buf, page_sz, total);
	if (ret || !buf)
		goto fail;

	t->commit_buf = buf;

	/* lock commit buffer pages for fast access */
	if (mlock(t->commit_buf, total))
		ublk_err("%s: can't lock commit buffer %s\n", __func__,
			strerror(errno));

	return 0;

fail:
	free_batch_commit_buf(t);
	return ret;
}

void ublk_batch_prepare(struct ublk_thread *t)
{
	/*
	 * We only handle single device in this thread context.
	 *
	 * All queues have same feature flags, so use queue 0's for
	 * calculate uring_cmd flags.
	 *
	 * This way looks not elegant, but it works so far.
	 */
	struct ublk_queue *q = &t->dev->q[0];

	t->commit_buf_elem_size = ublk_commit_elem_buf_size(t->dev);
	t->commit_buf_size = ublk_commit_buf_size(t);
	t->commit_buf_start = t->nr_bufs;
	t->nr_commit_buf = 2;
	t->nr_bufs += t->nr_commit_buf;

	t->cmd_flags = 0;
	if (ublk_queue_use_auto_zc(q)) {
		if (ublk_queue_auto_zc_fallback(q))
			t->cmd_flags |= UBLK_BATCH_F_AUTO_BUF_REG_FALLBACK;
	} else if (!ublk_queue_no_buf(q))
		t->cmd_flags |= UBLK_BATCH_F_HAS_BUF_ADDR;

	t->state |= UBLKS_T_BATCH_IO;

	ublk_log("%s: thread %d commit(nr_bufs %u, buf_size %u, start %u)\n",
			__func__, t->idx,
			t->nr_commit_buf, t->commit_buf_size,
			t->nr_bufs);
}

int ublk_batch_alloc_buf(struct ublk_thread *t)
{
	ublk_assert(t->nr_commit_buf < 16);
	return alloc_batch_commit_buf(t);
}

void ublk_batch_free_buf(struct ublk_thread *t)
{
	free_batch_commit_buf(t);
}

static void ublk_init_batch_cmd(struct ublk_thread *t, __u16 q_id,
				struct io_uring_sqe *sqe, unsigned op,
				unsigned short elem_bytes,
				unsigned short nr_elem,
				unsigned short buf_idx)
{
	struct ublk_batch_io *cmd;
	__u64 user_data;

	cmd = (struct ublk_batch_io *)ublk_get_sqe_cmd(sqe);

	ublk_set_sqe_cmd_op(sqe, op);

	sqe->fd	= 0;	/* dev->fds[0] */
	sqe->opcode	= IORING_OP_URING_CMD;
	sqe->flags	= IOSQE_FIXED_FILE;

	cmd->q_id	= q_id;
	cmd->flags	= 0;
	cmd->reserved 	= 0;
	cmd->elem_bytes = elem_bytes;
	cmd->nr_elem	= nr_elem;

	user_data = build_user_data(buf_idx, _IOC_NR(op), 0, q_id, 0);
	io_uring_sqe_set_data64(sqe, user_data);

	t->cmd_inflight += 1;

	ublk_dbg(UBLK_DBG_IO_CMD, "%s: thread %u qid %d cmd_op %x data %lx "
			"nr_elem %u elem_bytes %u buf_size %u buf_idx %d "
			"cmd_inflight %u\n",
			__func__, t->idx, q_id, op, user_data,
			cmd->nr_elem, cmd->elem_bytes,
			nr_elem * elem_bytes, buf_idx, t->cmd_inflight);
}

static void ublk_setup_commit_sqe(struct ublk_thread *t,
				  struct io_uring_sqe *sqe,
				  unsigned short buf_idx)
{
	struct ublk_batch_io *cmd;

	cmd = (struct ublk_batch_io *)ublk_get_sqe_cmd(sqe);

	/* Use plain user buffer instead of fixed buffer */
	cmd->flags |= t->cmd_flags;
}

int ublk_batch_queue_prep_io_cmds(struct ublk_thread *t, struct ublk_queue *q)
{
	unsigned short nr_elem = q->q_depth;
	unsigned short buf_idx = ublk_alloc_commit_buf(t);
	struct io_uring_sqe *sqe;
	void *buf;
	int i;

	ublk_assert(buf_idx != UBLKS_T_COMMIT_BUF_INV_IDX);

	ublk_io_alloc_sqes(t, &sqe, 1);

	ublk_assert(nr_elem == q->q_depth);
	buf = ublk_get_commit_buf(t, buf_idx);
	for (i = 0; i < nr_elem; i++) {
		struct ublk_batch_elem *elem = (struct ublk_batch_elem *)(
				buf + i * t->commit_buf_elem_size);
		struct ublk_io *io = &q->ios[i];

		elem->tag = i;
		elem->result = 0;

		if (ublk_queue_use_auto_zc(q))
			elem->buf_index = ublk_batch_io_buf_idx(t, q, i);
		else if (!ublk_queue_no_buf(q))
			elem->buf_addr = (__u64)io->buf_addr;
	}

	sqe->addr = (__u64)buf;
	sqe->len = t->commit_buf_elem_size * nr_elem;

	ublk_init_batch_cmd(t, q->q_id, sqe, UBLK_U_IO_PREP_IO_CMDS,
			t->commit_buf_elem_size, nr_elem, buf_idx);
	ublk_setup_commit_sqe(t, sqe, buf_idx);
	return 0;
}

static void ublk_batch_compl_commit_cmd(struct ublk_thread *t,
					const struct io_uring_cqe *cqe,
					unsigned op)
{
	unsigned short buf_idx = user_data_to_tag(cqe->user_data);

	if (op == _IOC_NR(UBLK_U_IO_PREP_IO_CMDS))
		ublk_assert(cqe->res == 0);
	else if (op == _IOC_NR(UBLK_U_IO_COMMIT_IO_CMDS))
		;//assert(cqe->res == t->commit_buf_size);
	else
		ublk_assert(0);

	ublk_free_commit_buf(t, buf_idx);
}

void ublk_batch_compl_cmd(struct ublk_thread *t,
			  const struct io_uring_cqe *cqe)
{
	unsigned op = user_data_to_op(cqe->user_data);

	if (op == _IOC_NR(UBLK_U_IO_PREP_IO_CMDS) ||
			op == _IOC_NR(UBLK_U_IO_COMMIT_IO_CMDS)) {
		t->cmd_inflight--;
		ublk_batch_compl_commit_cmd(t, cqe, op);
		return;
	}
}
