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
