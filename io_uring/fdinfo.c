// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "sqpoll.h"
#include "fdinfo.h"
#include "cancel.h"
#include "rsrc.h"

#ifdef CONFIG_PROC_FS
static __cold int io_uring_show_cred(struct seq_file *m, unsigned int id,
		const struct cred *cred)
{
	struct user_namespace *uns = seq_user_ns(m);
	struct group_info *gi;
	kernel_cap_t cap;
	int g;

	seq_printf(m, "%5d\n", id);
	seq_put_decimal_ull(m, "\tUid:\t", from_kuid_munged(uns, cred->uid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->euid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->suid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->fsuid));
	seq_put_decimal_ull(m, "\n\tGid:\t", from_kgid_munged(uns, cred->gid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->egid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->sgid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->fsgid));
	seq_puts(m, "\n\tGroups:\t");
	gi = cred->group_info;
	for (g = 0; g < gi->ngroups; g++) {
		seq_put_decimal_ull(m, g ? " " : "",
					from_kgid_munged(uns, gi->gid[g]));
	}
	seq_puts(m, "\n\tCapEff:\t");
	cap = cred->cap_effective;
	seq_put_hex_ll(m, NULL, cap.val, 16);
	seq_putc(m, '\n');
	return 0;
}

/*
 * Caller holds a reference to the file already, we don't need to do
 * anything else to get an extra reference.
 */
__cold void io_uring_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct io_ring_ctx *ctx = f->private_data;
	struct io_sq_data *sq = NULL;
	struct io_overflow_cqe *ocqe;
	struct io_rings *r = ctx->rings;
	unsigned int sq_mask = ctx->sq_entries - 1, cq_mask = ctx->cq_entries - 1;
	unsigned int sq_head = READ_ONCE(r->sq.head);
	unsigned int sq_tail = READ_ONCE(r->sq.tail);
	unsigned int cq_head = READ_ONCE(r->cq.head);
	unsigned int cq_tail = READ_ONCE(r->cq.tail);
	unsigned int cq_shift = 0;
	unsigned int sq_shift = 0;
	unsigned int sq_entries, cq_entries;
	bool has_lock;
	unsigned int i;

	if (ctx->flags & IORING_SETUP_CQE32)
		cq_shift = 1;
	if (ctx->flags & IORING_SETUP_SQE128)
		sq_shift = 1;

	/*
	 * we may get imprecise sqe and cqe info if uring is actively running
	 * since we get cached_sq_head and cached_cq_tail without uring_lock
	 * and sq_tail and cq_head are changed by userspace. But it's ok since
	 * we usually use these info when it is stuck.
	 */
	seq_printf(m, "SqMask:\t0x%x\n", sq_mask);
	seq_printf(m, "SqHead:\t%u\n", sq_head);
	seq_printf(m, "SqTail:\t%u\n", sq_tail);
	seq_printf(m, "CachedSqHead:\t%u\n", ctx->cached_sq_head);
	seq_printf(m, "CqMask:\t0x%x\n", cq_mask);
	seq_printf(m, "CqHead:\t%u\n", cq_head);
	seq_printf(m, "CqTail:\t%u\n", cq_tail);
	seq_printf(m, "CachedCqTail:\t%u\n", ctx->cached_cq_tail);
	seq_printf(m, "SQEs:\t%u\n", sq_tail - sq_head);
	sq_entries = min(sq_tail - sq_head, ctx->sq_entries);
	for (i = 0; i < sq_entries; i++) {
		unsigned int entry = i + sq_head;
		struct io_uring_sqe *sqe;
		unsigned int sq_idx;

		if (ctx->flags & IORING_SETUP_NO_SQARRAY)
			break;
		sq_idx = READ_ONCE(ctx->sq_array[entry & sq_mask]);
		if (sq_idx > sq_mask)
			continue;
		sqe = &ctx->sq_sqes[sq_idx << sq_shift];
		seq_printf(m, "%5u: opcode:%s, fd:%d, flags:%x, off:%llu, "
			      "addr:0x%llx, rw_flags:0x%x, buf_index:%d "
			      "user_data:%llu",
			   sq_idx, io_uring_get_opcode(sqe->opcode), sqe->fd,
			   sqe->flags, (unsigned long long) sqe->off,
			   (unsigned long long) sqe->addr, sqe->rw_flags,
			   sqe->buf_index, sqe->user_data);
		if (sq_shift) {
			u64 *sqeb = (void *) (sqe + 1);
			int size = sizeof(struct io_uring_sqe) / sizeof(u64);
			int j;

			for (j = 0; j < size; j++) {
				seq_printf(m, ", e%d:0x%llx", j,
						(unsigned long long) *sqeb);
				sqeb++;
			}
		}
		seq_printf(m, "\n");
	}
	seq_printf(m, "CQEs:\t%u\n", cq_tail - cq_head);
	cq_entries = min(cq_tail - cq_head, ctx->cq_entries);
	for (i = 0; i < cq_entries; i++) {
		unsigned int entry = i + cq_head;
		struct io_uring_cqe *cqe = &r->cqes[(entry & cq_mask) << cq_shift];

		seq_printf(m, "%5u: user_data:%llu, res:%d, flag:%x",
			   entry & cq_mask, cqe->user_data, cqe->res,
			   cqe->flags);
		if (cq_shift)
			seq_printf(m, ", extra1:%llu, extra2:%llu\n",
					cqe->big_cqe[0], cqe->big_cqe[1]);
		seq_printf(m, "\n");
	}

	/*
	 * Avoid ABBA deadlock between the seq lock and the io_uring mutex,
	 * since fdinfo case grabs it in the opposite direction of normal use
	 * cases. If we fail to get the lock, we just don't iterate any
	 * structures that could be going away outside the io_uring mutex.
	 */
	has_lock = mutex_trylock(&ctx->uring_lock);

	if (has_lock && (ctx->flags & IORING_SETUP_SQPOLL)) {
		sq = ctx->sq_data;
		if (!sq->thread)
			sq = NULL;
	}

	seq_printf(m, "SqThread:\t%d\n", sq ? task_pid_nr(sq->thread) : -1);
	seq_printf(m, "SqThreadCpu:\t%d\n", sq ? task_cpu(sq->thread) : -1);
	seq_printf(m, "UserFiles:\t%u\n", ctx->nr_user_files);
	for (i = 0; has_lock && i < ctx->nr_user_files; i++) {
		struct file *f = io_file_from_index(&ctx->file_table, i);

		if (f)
			seq_printf(m, "%5u: %s\n", i, file_dentry(f)->d_iname);
		else
			seq_printf(m, "%5u: <none>\n", i);
	}
	seq_printf(m, "UserBufs:\t%u\n", ctx->nr_user_bufs);
	for (i = 0; has_lock && i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *buf = ctx->user_bufs[i];
		unsigned int len = buf->ubuf_end - buf->ubuf;

		seq_printf(m, "%5u: 0x%llx/%u\n", i, buf->ubuf, len);
	}
	if (has_lock && !xa_empty(&ctx->personalities)) {
		unsigned long index;
		const struct cred *cred;

		seq_printf(m, "Personalities:\n");
		xa_for_each(&ctx->personalities, index, cred)
			io_uring_show_cred(m, index, cred);
	}

	seq_puts(m, "PollList:\n");
	for (i = 0; i < (1U << ctx->cancel_table.hash_bits); i++) {
		struct io_hash_bucket *hb = &ctx->cancel_table.hbs[i];
		struct io_hash_bucket *hbl = &ctx->cancel_table_locked.hbs[i];
		struct io_kiocb *req;

		spin_lock(&hb->lock);
		hlist_for_each_entry(req, &hb->list, hash_node)
			seq_printf(m, "  op=%d, task_works=%d\n", req->opcode,
					task_work_pending(req->task));
		spin_unlock(&hb->lock);

		if (!has_lock)
			continue;
		hlist_for_each_entry(req, &hbl->list, hash_node)
			seq_printf(m, "  op=%d, task_works=%d\n", req->opcode,
					task_work_pending(req->task));
	}

	if (has_lock)
		mutex_unlock(&ctx->uring_lock);

	seq_puts(m, "CqOverflowList:\n");
	spin_lock(&ctx->completion_lock);
	list_for_each_entry(ocqe, &ctx->cq_overflow_list, list) {
		struct io_uring_cqe *cqe = &ocqe->cqe;

		seq_printf(m, "  user_data=%llu, res=%d, flags=%x\n",
			   cqe->user_data, cqe->res, cqe->flags);

	}

	spin_unlock(&ctx->completion_lock);
}
#endif
