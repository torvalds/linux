// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
 */
#include <uapi/linux/btf.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/math64.h>

bool bpf_verifier_log_attr_valid(const struct bpf_verifier_log *log)
{
	return log->len_total > 0 && log->len_total <= UINT_MAX >> 2 &&
	       log->level && log->ubuf && !(log->level & ~BPF_LOG_MASK);
}

void bpf_verifier_vlog(struct bpf_verifier_log *log, const char *fmt,
		       va_list args)
{
	unsigned int n;

	n = vscnprintf(log->kbuf, BPF_VERIFIER_TMP_LOG_SIZE, fmt, args);

	WARN_ONCE(n >= BPF_VERIFIER_TMP_LOG_SIZE - 1,
		  "verifier log line truncated - local buffer too short\n");

	if (log->level == BPF_LOG_KERNEL) {
		bool newline = n > 0 && log->kbuf[n - 1] == '\n';

		pr_err("BPF: %s%s", log->kbuf, newline ? "" : "\n");
		return;
	}

	if (log->level & BPF_LOG_FIXED) {
		n = min(log->len_total - bpf_log_used(log) - 1, n);
		log->kbuf[n] = '\0';
		n += 1;

		if (copy_to_user(log->ubuf + log->end_pos, log->kbuf, n))
			goto fail;

		log->end_pos += n - 1; /* don't count terminating '\0' */
	} else {
		u64 new_end, new_start, cur_pos;
		u32 buf_start, buf_end, new_n;

		n += 1;

		new_end = log->end_pos + n;
		if (new_end - log->start_pos >= log->len_total)
			new_start = new_end - log->len_total;
		else
			new_start = log->start_pos;
		new_n = min(n, log->len_total);
		cur_pos = new_end - new_n;

		div_u64_rem(cur_pos, log->len_total, &buf_start);
		div_u64_rem(new_end, log->len_total, &buf_end);
		/* new_end and buf_end are exclusive indices, so if buf_end is
		 * exactly zero, then it actually points right to the end of
		 * ubuf and there is no wrap around
		 */
		if (buf_end == 0)
			buf_end = log->len_total;

		/* if buf_start > buf_end, we wrapped around;
		 * if buf_start == buf_end, then we fill ubuf completely; we
		 * can't have buf_start == buf_end to mean that there is
		 * nothing to write, because we always write at least
		 * something, even if terminal '\0'
		 */
		if (buf_start < buf_end) {
			/* message fits within contiguous chunk of ubuf */
			if (copy_to_user(log->ubuf + buf_start,
					 log->kbuf + n - new_n,
					 buf_end - buf_start))
				goto fail;
		} else {
			/* message wraps around the end of ubuf, copy in two chunks */
			if (copy_to_user(log->ubuf + buf_start,
					 log->kbuf + n - new_n,
					 log->len_total - buf_start))
				goto fail;
			if (copy_to_user(log->ubuf,
					 log->kbuf + n - buf_end,
					 buf_end))
				goto fail;
		}

		log->start_pos = new_start;
		log->end_pos = new_end - 1; /* don't count terminating '\0' */
	}

	return;
fail:
	log->ubuf = NULL;
}

void bpf_vlog_reset(struct bpf_verifier_log *log, u64 new_pos)
{
	char zero = 0;
	u32 pos;

	if (WARN_ON_ONCE(new_pos > log->end_pos))
		return;

	if (!bpf_verifier_log_needed(log))
		return;

	/* if position to which we reset is beyond current log window,
	 * then we didn't preserve any useful content and should adjust
	 * start_pos to end up with an empty log (start_pos == end_pos)
	 */
	log->end_pos = new_pos;
	if (log->end_pos < log->start_pos)
		log->start_pos = log->end_pos;
	div_u64_rem(new_pos, log->len_total, &pos);
	if (put_user(zero, log->ubuf + pos))
		log->ubuf = NULL;
}

static void bpf_vlog_reverse_kbuf(char *buf, int len)
{
	int i, j;

	for (i = 0, j = len - 1; i < j; i++, j--)
		swap(buf[i], buf[j]);
}

static int bpf_vlog_reverse_ubuf(struct bpf_verifier_log *log, int start, int end)
{
	/* we split log->kbuf into two equal parts for both ends of array */
	int n = sizeof(log->kbuf) / 2, nn;
	char *lbuf = log->kbuf, *rbuf = log->kbuf + n;

	/* Read ubuf's section [start, end) two chunks at a time, from left
	 * and right side; within each chunk, swap all the bytes; after that
	 * reverse the order of lbuf and rbuf and write result back to ubuf.
	 * This way we'll end up with swapped contents of specified
	 * [start, end) ubuf segment.
	 */
	while (end - start > 1) {
		nn = min(n, (end - start ) / 2);

		if (copy_from_user(lbuf, log->ubuf + start, nn))
			return -EFAULT;
		if (copy_from_user(rbuf, log->ubuf + end - nn, nn))
			return -EFAULT;

		bpf_vlog_reverse_kbuf(lbuf, nn);
		bpf_vlog_reverse_kbuf(rbuf, nn);

		/* we write lbuf to the right end of ubuf, while rbuf to the
		 * left one to end up with properly reversed overall ubuf
		 */
		if (copy_to_user(log->ubuf + start, rbuf, nn))
			return -EFAULT;
		if (copy_to_user(log->ubuf + end - nn, lbuf, nn))
			return -EFAULT;

		start += nn;
		end -= nn;
	}

	return 0;
}

bool bpf_vlog_truncated(const struct bpf_verifier_log *log)
{
	if (log->level & BPF_LOG_FIXED)
		return bpf_log_used(log) >= log->len_total - 1;
	else
		return log->start_pos > 0;
}

void bpf_vlog_finalize(struct bpf_verifier_log *log)
{
	u32 sublen;
	int err;

	if (!log || !log->level || !log->ubuf)
		return;
	if ((log->level & BPF_LOG_FIXED) || log->level == BPF_LOG_KERNEL)
		return;

	/* If we never truncated log, there is nothing to move around. */
	if (log->start_pos == 0)
		return;

	/* Otherwise we need to rotate log contents to make it start from the
	 * buffer beginning and be a continuous zero-terminated string. Note
	 * that if log->start_pos != 0 then we definitely filled up entire log
	 * buffer with no gaps, and we just need to shift buffer contents to
	 * the left by (log->start_pos % log->len_total) bytes.
	 *
	 * Unfortunately, user buffer could be huge and we don't want to
	 * allocate temporary kernel memory of the same size just to shift
	 * contents in a straightforward fashion. Instead, we'll be clever and
	 * do in-place array rotation. This is a leetcode-style problem, which
	 * could be solved by three rotations.
	 *
	 * Let's say we have log buffer that has to be shifted left by 7 bytes
	 * (spaces and vertical bar is just for demonstrative purposes):
	 *   E F G H I J K | A B C D
	 *
	 * First, we reverse entire array:
	 *   D C B A | K J I H G F E
	 *
	 * Then we rotate first 4 bytes (DCBA) and separately last 7 bytes
	 * (KJIHGFE), resulting in a properly rotated array:
	 *   A B C D | E F G H I J K
	 *
	 * We'll utilize log->kbuf to read user memory chunk by chunk, swap
	 * bytes, and write them back. Doing it byte-by-byte would be
	 * unnecessarily inefficient. Altogether we are going to read and
	 * write each byte twice, for total 4 memory copies between kernel and
	 * user space.
	 */

	/* length of the chopped off part that will be the beginning;
	 * len(ABCD) in the example above
	 */
	div_u64_rem(log->start_pos, log->len_total, &sublen);
	sublen = log->len_total - sublen;

	err = bpf_vlog_reverse_ubuf(log, 0, log->len_total);
	err = err ?: bpf_vlog_reverse_ubuf(log, 0, sublen);
	err = err ?: bpf_vlog_reverse_ubuf(log, sublen, log->len_total);
	if (err)
		log->ubuf = NULL;
}

/* log_level controls verbosity level of eBPF verifier.
 * bpf_verifier_log_write() is used to dump the verification trace to the log,
 * so the user can figure out what's wrong with the program
 */
__printf(2, 3) void bpf_verifier_log_write(struct bpf_verifier_env *env,
					   const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(&env->log, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(bpf_verifier_log_write);

__printf(2, 3) void bpf_log(struct bpf_verifier_log *log,
			    const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(bpf_log);
