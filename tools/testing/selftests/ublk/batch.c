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
	free(t->commit);
}

static int alloc_batch_commit_buf(struct ublk_thread *t)
{
	unsigned buf_size = ublk_commit_buf_size(t);
	unsigned int total = buf_size * t->nr_commit_buf;
	unsigned int page_sz = getpagesize();
	void *buf = NULL;
	int i, ret, j = 0;

	t->commit = calloc(t->nr_queues, sizeof(*t->commit));
	for (i = 0; i < t->dev->dev_info.nr_hw_queues; i++) {
		if (t->q_map[i])
			t->commit[j++].q_id = i;
	}

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

static unsigned int ublk_thread_nr_queues(const struct ublk_thread *t)
{
	int i;
	int ret = 0;

	for (i = 0; i < t->dev->dev_info.nr_hw_queues; i++)
		ret += !!t->q_map[i];

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

	/* cache nr_queues because we don't support dynamic load-balance yet */
	t->nr_queues = ublk_thread_nr_queues(t);

	t->commit_buf_elem_size = ublk_commit_elem_buf_size(t->dev);
	t->commit_buf_size = ublk_commit_buf_size(t);
	t->commit_buf_start = t->nr_bufs;
	t->nr_commit_buf = 2 * t->nr_queues;
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

static void free_batch_fetch_buf(struct ublk_thread *t)
{
	int i;

	for (i = 0; i < t->nr_fetch_bufs; i++) {
		io_uring_free_buf_ring(&t->ring, t->fetch[i].br, 1, i);
		munlock(t->fetch[i].fetch_buf, t->fetch[i].fetch_buf_size);
		free(t->fetch[i].fetch_buf);
	}
	free(t->fetch);
}

static int alloc_batch_fetch_buf(struct ublk_thread *t)
{
	/* page aligned fetch buffer, and it is mlocked for speedup delivery */
	unsigned pg_sz = getpagesize();
	unsigned buf_size = round_up(t->dev->dev_info.queue_depth * 2, pg_sz);
	int ret;
	int i = 0;

	/* double fetch buffer for each queue */
	t->nr_fetch_bufs = t->nr_queues * 2;
	t->fetch = calloc(t->nr_fetch_bufs, sizeof(*t->fetch));

	/* allocate one buffer for each queue */
	for (i = 0; i < t->nr_fetch_bufs; i++) {
		t->fetch[i].fetch_buf_size = buf_size;

		if (posix_memalign((void **)&t->fetch[i].fetch_buf, pg_sz,
					t->fetch[i].fetch_buf_size))
			return -ENOMEM;

		/* lock fetch buffer page for fast fetching */
		if (mlock(t->fetch[i].fetch_buf, t->fetch[i].fetch_buf_size))
			ublk_err("%s: can't lock fetch buffer %s\n", __func__,
				strerror(errno));
		t->fetch[i].br = io_uring_setup_buf_ring(&t->ring, 1,
			i, IOU_PBUF_RING_INC, &ret);
		if (!t->fetch[i].br) {
			ublk_err("Buffer ring register failed %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int ublk_batch_alloc_buf(struct ublk_thread *t)
{
	int ret;

	ublk_assert(t->nr_commit_buf < 2 * UBLK_MAX_QUEUES);

	ret = alloc_batch_commit_buf(t);
	if (ret)
		return ret;
	return alloc_batch_fetch_buf(t);
}

void ublk_batch_free_buf(struct ublk_thread *t)
{
	free_batch_commit_buf(t);
	free_batch_fetch_buf(t);
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

	user_data = build_user_data(buf_idx, _IOC_NR(op), nr_elem, q_id, 0);
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

static void ublk_batch_queue_fetch(struct ublk_thread *t,
				   struct ublk_queue *q,
				   unsigned short buf_idx)
{
	unsigned short nr_elem = t->fetch[buf_idx].fetch_buf_size / 2;
	struct io_uring_sqe *sqe;

	io_uring_buf_ring_add(t->fetch[buf_idx].br, t->fetch[buf_idx].fetch_buf,
			t->fetch[buf_idx].fetch_buf_size,
			0, 0, 0);
	io_uring_buf_ring_advance(t->fetch[buf_idx].br, 1);

	ublk_io_alloc_sqes(t, &sqe, 1);

	ublk_init_batch_cmd(t, q->q_id, sqe, UBLK_U_IO_FETCH_IO_CMDS, 2, nr_elem,
			buf_idx);

	sqe->rw_flags= IORING_URING_CMD_MULTISHOT;
	sqe->buf_group = buf_idx;
	sqe->flags |= IOSQE_BUFFER_SELECT;

	t->fetch[buf_idx].fetch_buf_off = 0;
}

void ublk_batch_start_fetch(struct ublk_thread *t)
{
	int i;
	int j = 0;

	for (i = 0; i < t->dev->dev_info.nr_hw_queues; i++) {
		if (t->q_map[i]) {
			struct ublk_queue *q = &t->dev->q[i];

			/* submit two fetch commands for each queue */
			ublk_batch_queue_fetch(t, q, j++);
			ublk_batch_queue_fetch(t, q, j++);
		}
	}
}

static unsigned short ublk_compl_batch_fetch(struct ublk_thread *t,
				   struct ublk_queue *q,
				   const struct io_uring_cqe *cqe)
{
	unsigned short buf_idx = user_data_to_tag(cqe->user_data);
	unsigned start = t->fetch[buf_idx].fetch_buf_off;
	unsigned end = start + cqe->res;
	void *buf = t->fetch[buf_idx].fetch_buf;
	int i;

	if (cqe->res < 0)
		return buf_idx;

       if ((end - start) / 2 > q->q_depth) {
               ublk_err("%s: fetch duplicated ios offset %u count %u\n", __func__, start, cqe->res);

               for (i = start; i < end; i += 2) {
                       unsigned short tag = *(unsigned short *)(buf + i);

                       ublk_err("%u ", tag);
               }
               ublk_err("\n");
       }

	for (i = start; i < end; i += 2) {
		unsigned short tag = *(unsigned short *)(buf + i);

		if (tag >= q->q_depth)
			ublk_err("%s: bad tag %u\n", __func__, tag);

		if (q->tgt_ops->queue_io)
			q->tgt_ops->queue_io(t, q, tag);
	}
	t->fetch[buf_idx].fetch_buf_off = end;
	return buf_idx;
}

static int __ublk_batch_queue_prep_io_cmds(struct ublk_thread *t, struct ublk_queue *q)
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

int ublk_batch_queue_prep_io_cmds(struct ublk_thread *t, struct ublk_queue *q)
{
	int ret = 0;

	pthread_spin_lock(&q->lock);
	if (q->flags & UBLKS_Q_PREPARED)
		goto unlock;
	ret = __ublk_batch_queue_prep_io_cmds(t, q);
	if (!ret)
		q->flags |= UBLKS_Q_PREPARED;
unlock:
	pthread_spin_unlock(&q->lock);

	return ret;
}

static void ublk_batch_compl_commit_cmd(struct ublk_thread *t,
					const struct io_uring_cqe *cqe,
					unsigned op)
{
	unsigned short buf_idx = user_data_to_tag(cqe->user_data);

	if (op == _IOC_NR(UBLK_U_IO_PREP_IO_CMDS))
		ublk_assert(cqe->res == 0);
	else if (op == _IOC_NR(UBLK_U_IO_COMMIT_IO_CMDS)) {
		int nr_elem = user_data_to_tgt_data(cqe->user_data);

		ublk_assert(cqe->res == t->commit_buf_elem_size * nr_elem);
	} else
		ublk_assert(0);

	ublk_free_commit_buf(t, buf_idx);
}

void ublk_batch_compl_cmd(struct ublk_thread *t,
			  const struct io_uring_cqe *cqe)
{
	unsigned op = user_data_to_op(cqe->user_data);
	struct ublk_queue *q;
	unsigned buf_idx;
	unsigned q_id;

	if (op == _IOC_NR(UBLK_U_IO_PREP_IO_CMDS) ||
			op == _IOC_NR(UBLK_U_IO_COMMIT_IO_CMDS)) {
		t->cmd_inflight--;
		ublk_batch_compl_commit_cmd(t, cqe, op);
		return;
	}

	/* FETCH command is per queue */
	q_id = user_data_to_q_id(cqe->user_data);
	q = &t->dev->q[q_id];
	buf_idx = ublk_compl_batch_fetch(t, q, cqe);

	if (cqe->res < 0 && cqe->res != -ENOBUFS) {
		t->cmd_inflight--;
		t->state |= UBLKS_T_STOPPING;
	} else if (!(cqe->flags & IORING_CQE_F_MORE) || cqe->res == -ENOBUFS) {
		t->cmd_inflight--;
		ublk_batch_queue_fetch(t, q, buf_idx);
	}
}

static void __ublk_batch_commit_io_cmds(struct ublk_thread *t,
					struct batch_commit_buf *cb)
{
	struct io_uring_sqe *sqe;
	unsigned short buf_idx;
	unsigned short nr_elem = cb->done;

	/* nothing to commit */
	if (!nr_elem) {
		ublk_free_commit_buf(t, cb->buf_idx);
		return;
	}

	ublk_io_alloc_sqes(t, &sqe, 1);
	buf_idx = cb->buf_idx;
	sqe->addr = (__u64)cb->elem;
	sqe->len = nr_elem * t->commit_buf_elem_size;

	/* commit isn't per-queue command */
	ublk_init_batch_cmd(t, cb->q_id, sqe, UBLK_U_IO_COMMIT_IO_CMDS,
			t->commit_buf_elem_size, nr_elem, buf_idx);
	ublk_setup_commit_sqe(t, sqe, buf_idx);
}

void ublk_batch_commit_io_cmds(struct ublk_thread *t)
{
	int i;

	for (i = 0; i < t->nr_queues; i++) {
		struct batch_commit_buf *cb = &t->commit[i];

		if (cb->buf_idx != UBLKS_T_COMMIT_BUF_INV_IDX)
			__ublk_batch_commit_io_cmds(t, cb);
	}

}

static void __ublk_batch_init_commit(struct ublk_thread *t,
				     struct batch_commit_buf *cb,
				     unsigned short buf_idx)
{
	/* so far only support 1:1 queue/thread mapping */
	cb->buf_idx = buf_idx;
	cb->elem = ublk_get_commit_buf(t, buf_idx);
	cb->done = 0;
	cb->count = t->commit_buf_size /
		t->commit_buf_elem_size;
}

/* COMMIT_IO_CMDS is per-queue command, so use its own commit buffer */
static void ublk_batch_init_commit(struct ublk_thread *t,
				   struct batch_commit_buf *cb)
{
	unsigned short buf_idx = ublk_alloc_commit_buf(t);

	ublk_assert(buf_idx != UBLKS_T_COMMIT_BUF_INV_IDX);
	ublk_assert(!ublk_batch_commit_prepared(cb));

	__ublk_batch_init_commit(t, cb, buf_idx);
}

void ublk_batch_prep_commit(struct ublk_thread *t)
{
	int i;

	for (i = 0; i < t->nr_queues; i++)
		t->commit[i].buf_idx = UBLKS_T_COMMIT_BUF_INV_IDX;
}

void ublk_batch_complete_io(struct ublk_thread *t, struct ublk_queue *q,
			    unsigned tag, int res)
{
	unsigned q_t_idx = ublk_queue_idx_in_thread(t, q);
	struct batch_commit_buf *cb = &t->commit[q_t_idx];
	struct ublk_batch_elem *elem;
	struct ublk_io *io = &q->ios[tag];

	if (!ublk_batch_commit_prepared(cb))
		ublk_batch_init_commit(t, cb);

	ublk_assert(q->q_id == cb->q_id);

	elem = (struct ublk_batch_elem *)(cb->elem + cb->done * t->commit_buf_elem_size);
	elem->tag = tag;
	elem->buf_index = ublk_batch_io_buf_idx(t, q, tag);
	elem->result = res;

	if (!ublk_queue_no_buf(q))
		elem->buf_addr	= (__u64) (uintptr_t) io->buf_addr;

	cb->done += 1;
	ublk_assert(cb->done <= cb->count);
}

void ublk_batch_setup_map(unsigned char (*q_thread_map)[UBLK_MAX_QUEUES],
			   int nthreads, int queues)
{
	int i, j;

	/*
	 * Setup round-robin queue-to-thread mapping for arbitrary N:M combinations.
	 *
	 * This algorithm distributes queues across threads (and threads across queues)
	 * in a balanced round-robin fashion to ensure even load distribution.
	 *
	 * Examples:
	 * - 2 threads, 4 queues: T0=[Q0,Q2], T1=[Q1,Q3]
	 * - 4 threads, 2 queues: T0=[Q0], T1=[Q1], T2=[Q0], T3=[Q1]
	 * - 3 threads, 3 queues: T0=[Q0], T1=[Q1], T2=[Q2] (1:1 mapping)
	 *
	 * Phase 1: Mark which queues each thread handles (boolean mapping)
	 */
	for (i = 0, j = 0; i < queues || j < nthreads; i++, j++) {
		q_thread_map[j % nthreads][i % queues] = 1;
	}

	/*
	 * Phase 2: Convert boolean mapping to sequential indices within each thread.
	 *
	 * Transform from: q_thread_map[thread][queue] = 1 (handles queue)
	 * To:             q_thread_map[thread][queue] = N (queue index within thread)
	 *
	 * This allows each thread to know the local index of each queue it handles,
	 * which is essential for buffer allocation and management. For example:
	 * - Thread 0 handling queues [0,2] becomes: q_thread_map[0][0]=1, q_thread_map[0][2]=2
	 * - Thread 1 handling queues [1,3] becomes: q_thread_map[1][1]=1, q_thread_map[1][3]=2
	 */
	for (j = 0; j < nthreads; j++) {
		unsigned char seq = 1;

		for (i = 0; i < queues; i++) {
			if (q_thread_map[j][i])
				q_thread_map[j][i] = seq++;
		}
	}

#if 0
	for (j = 0; j < nthreads; j++) {
		printf("thread %0d: ", j);
		for (i = 0; i < queues; i++) {
			if (q_thread_map[j][i])
				printf("%03u ", i);
		}
		printf("\n");
	}
	printf("\n");
	for (j = 0; j < nthreads; j++) {
		for (i = 0; i < queues; i++) {
			printf("%03u ", q_thread_map[j][i]);
		}
		printf("\n");
	}
#endif
}
