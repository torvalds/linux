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
#include <linux/string.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

static bool bpf_verifier_log_attr_valid(const struct bpf_verifier_log *log)
{
	/* ubuf and len_total should both be specified (or not) together */
	if (!!log->ubuf != !!log->len_total)
		return false;
	/* log buf without log_level is meaningless */
	if (log->ubuf && log->level == 0)
		return false;
	if (log->level & ~BPF_LOG_MASK)
		return false;
	if (log->len_total > UINT_MAX >> 2)
		return false;
	return true;
}

int bpf_vlog_init(struct bpf_verifier_log *log, u32 log_level,
		  char __user *log_buf, u32 log_size)
{
	log->level = log_level;
	log->ubuf = log_buf;
	log->len_total = log_size;

	/* log attributes have to be sane */
	if (!bpf_verifier_log_attr_valid(log))
		return -EINVAL;

	return 0;
}

static void bpf_vlog_update_len_max(struct bpf_verifier_log *log, u32 add_len)
{
	/* add_len includes terminal \0, so no need for +1. */
	u64 len = log->end_pos + add_len;

	/* log->len_max could be larger than our current len due to
	 * bpf_vlog_reset() calls, so we maintain the max of any length at any
	 * previous point
	 */
	if (len > UINT_MAX)
		log->len_max = UINT_MAX;
	else if (len > log->len_max)
		log->len_max = len;
}

void bpf_verifier_vlog(struct bpf_verifier_log *log, const char *fmt,
		       va_list args)
{
	u64 cur_pos;
	u32 new_n, n;

	n = vscnprintf(log->kbuf, BPF_VERIFIER_TMP_LOG_SIZE, fmt, args);

	if (log->level == BPF_LOG_KERNEL) {
		bool newline = n > 0 && log->kbuf[n - 1] == '\n';

		pr_err("BPF: %s%s", log->kbuf, newline ? "" : "\n");
		return;
	}

	n += 1; /* include terminating zero */
	bpf_vlog_update_len_max(log, n);

	if (log->level & BPF_LOG_FIXED) {
		/* check if we have at least something to put into user buf */
		new_n = 0;
		if (log->end_pos < log->len_total) {
			new_n = min_t(u32, log->len_total - log->end_pos, n);
			log->kbuf[new_n - 1] = '\0';
		}

		cur_pos = log->end_pos;
		log->end_pos += n - 1; /* don't count terminating '\0' */

		if (log->ubuf && new_n &&
		    copy_to_user(log->ubuf + cur_pos, log->kbuf, new_n))
			goto fail;
	} else {
		u64 new_end, new_start;
		u32 buf_start, buf_end;

		new_end = log->end_pos + n;
		if (new_end - log->start_pos >= log->len_total)
			new_start = new_end - log->len_total;
		else
			new_start = log->start_pos;

		log->start_pos = new_start;
		log->end_pos = new_end - 1; /* don't count terminating '\0' */

		if (!log->ubuf)
			return;

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

	if (!bpf_verifier_log_needed(log) || log->level == BPF_LOG_KERNEL)
		return;

	/* if position to which we reset is beyond current log window,
	 * then we didn't preserve any useful content and should adjust
	 * start_pos to end up with an empty log (start_pos == end_pos)
	 */
	log->end_pos = new_pos;
	if (log->end_pos < log->start_pos)
		log->start_pos = log->end_pos;

	if (!log->ubuf)
		return;

	if (log->level & BPF_LOG_FIXED)
		pos = log->end_pos + 1;
	else
		div_u64_rem(new_pos, log->len_total, &pos);

	if (pos < log->len_total && put_user(zero, log->ubuf + pos))
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

int bpf_vlog_finalize(struct bpf_verifier_log *log, u32 *log_size_actual)
{
	u32 sublen;
	int err;

	*log_size_actual = 0;
	if (!log || log->level == 0 || log->level == BPF_LOG_KERNEL)
		return 0;

	if (!log->ubuf)
		goto skip_log_rotate;
	/* If we never truncated log, there is nothing to move around. */
	if (log->start_pos == 0)
		goto skip_log_rotate;

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

skip_log_rotate:
	*log_size_actual = log->len_max;

	/* properly initialized log has either both ubuf!=NULL and len_total>0
	 * or ubuf==NULL and len_total==0, so if this condition doesn't hold,
	 * we got a fault somewhere along the way, so report it back
	 */
	if (!!log->ubuf != !!log->len_total)
		return -EFAULT;

	/* did truncation actually happen? */
	if (log->ubuf && log->len_max > log->len_total)
		return -ENOSPC;

	return 0;
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

static const struct bpf_line_info *
find_linfo(const struct bpf_verifier_env *env, u32 insn_off)
{
	const struct bpf_line_info *linfo;
	const struct bpf_prog *prog;
	u32 nr_linfo;
	int l, r, m;

	prog = env->prog;
	nr_linfo = prog->aux->nr_linfo;

	if (!nr_linfo || insn_off >= prog->len)
		return NULL;

	linfo = prog->aux->linfo;
	/* Loop invariant: linfo[l].insn_off <= insns_off.
	 * linfo[0].insn_off == 0 which always satisfies above condition.
	 * Binary search is searching for rightmost linfo entry that satisfies
	 * the above invariant, giving us the desired record that covers given
	 * instruction offset.
	 */
	l = 0;
	r = nr_linfo - 1;
	while (l < r) {
		/* (r - l + 1) / 2 means we break a tie to the right, so if:
		 * l=1, r=2, linfo[l].insn_off <= insn_off, linfo[r].insn_off > insn_off,
		 * then m=2, we see that linfo[m].insn_off > insn_off, and so
		 * r becomes 1 and we exit the loop with correct l==1.
		 * If the tie was broken to the left, m=1 would end us up in
		 * an endless loop where l and m stay at 1 and r stays at 2.
		 */
		m = l + (r - l + 1) / 2;
		if (linfo[m].insn_off <= insn_off)
			l = m;
		else
			r = m - 1;
	}

	return &linfo[l];
}

static const char *ltrim(const char *s)
{
	while (isspace(*s))
		s++;

	return s;
}

__printf(3, 4) void verbose_linfo(struct bpf_verifier_env *env,
				  u32 insn_off,
				  const char *prefix_fmt, ...)
{
	const struct bpf_line_info *linfo, *prev_linfo;
	const struct btf *btf;
	const char *s, *fname;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	prev_linfo = env->prev_linfo;
	linfo = find_linfo(env, insn_off);
	if (!linfo || linfo == prev_linfo)
		return;

	/* It often happens that two separate linfo records point to the same
	 * source code line, but have differing column numbers. Given verifier
	 * log doesn't emit column information, from user perspective we just
	 * end up emitting the same source code line twice unnecessarily.
	 * So instead check that previous and current linfo record point to
	 * the same file (file_name_offs match) and the same line number, and
	 * avoid emitting duplicated source code line in such case.
	 */
	if (prev_linfo && linfo->file_name_off == prev_linfo->file_name_off &&
	    BPF_LINE_INFO_LINE_NUM(linfo->line_col) == BPF_LINE_INFO_LINE_NUM(prev_linfo->line_col))
		return;

	if (prefix_fmt) {
		va_list args;

		va_start(args, prefix_fmt);
		bpf_verifier_vlog(&env->log, prefix_fmt, args);
		va_end(args);
	}

	btf = env->prog->aux->btf;
	s = ltrim(btf_name_by_offset(btf, linfo->line_off));
	verbose(env, "%s", s); /* source code line */

	s = btf_name_by_offset(btf, linfo->file_name_off);
	/* leave only file name */
	fname = strrchr(s, '/');
	fname = fname ? fname + 1 : s;
	verbose(env, " @ %s:%u\n", fname, BPF_LINE_INFO_LINE_NUM(linfo->line_col));

	env->prev_linfo = linfo;
}

static const char *btf_type_name(const struct btf *btf, u32 id)
{
	return btf_name_by_offset(btf, btf_type_by_id(btf, id)->name_off);
}

/* string representation of 'enum bpf_reg_type'
 *
 * Note that reg_type_str() can not appear more than once in a single verbose()
 * statement.
 */
const char *reg_type_str(struct bpf_verifier_env *env, enum bpf_reg_type type)
{
	char postfix[16] = {0}, prefix[64] = {0};
	static const char * const str[] = {
		[NOT_INIT]		= "?",
		[SCALAR_VALUE]		= "scalar",
		[PTR_TO_CTX]		= "ctx",
		[CONST_PTR_TO_MAP]	= "map_ptr",
		[PTR_TO_MAP_VALUE]	= "map_value",
		[PTR_TO_STACK]		= "fp",
		[PTR_TO_PACKET]		= "pkt",
		[PTR_TO_PACKET_META]	= "pkt_meta",
		[PTR_TO_PACKET_END]	= "pkt_end",
		[PTR_TO_FLOW_KEYS]	= "flow_keys",
		[PTR_TO_SOCKET]		= "sock",
		[PTR_TO_SOCK_COMMON]	= "sock_common",
		[PTR_TO_TCP_SOCK]	= "tcp_sock",
		[PTR_TO_TP_BUFFER]	= "tp_buffer",
		[PTR_TO_XDP_SOCK]	= "xdp_sock",
		[PTR_TO_BTF_ID]		= "ptr_",
		[PTR_TO_MEM]		= "mem",
		[PTR_TO_ARENA]		= "arena",
		[PTR_TO_BUF]		= "buf",
		[PTR_TO_FUNC]		= "func",
		[PTR_TO_MAP_KEY]	= "map_key",
		[CONST_PTR_TO_DYNPTR]	= "dynptr_ptr",
	};

	if (type & PTR_MAYBE_NULL) {
		if (base_type(type) == PTR_TO_BTF_ID)
			strscpy(postfix, "or_null_");
		else
			strscpy(postfix, "_or_null");
	}

	snprintf(prefix, sizeof(prefix), "%s%s%s%s%s%s%s",
		 type & MEM_RDONLY ? "rdonly_" : "",
		 type & MEM_RINGBUF ? "ringbuf_" : "",
		 type & MEM_USER ? "user_" : "",
		 type & MEM_PERCPU ? "percpu_" : "",
		 type & MEM_RCU ? "rcu_" : "",
		 type & PTR_UNTRUSTED ? "untrusted_" : "",
		 type & PTR_TRUSTED ? "trusted_" : ""
	);

	snprintf(env->tmp_str_buf, TMP_STR_BUF_LEN, "%s%s%s",
		 prefix, str[base_type(type)], postfix);
	return env->tmp_str_buf;
}

const char *dynptr_type_str(enum bpf_dynptr_type type)
{
	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
		return "local";
	case BPF_DYNPTR_TYPE_RINGBUF:
		return "ringbuf";
	case BPF_DYNPTR_TYPE_SKB:
		return "skb";
	case BPF_DYNPTR_TYPE_XDP:
		return "xdp";
	case BPF_DYNPTR_TYPE_INVALID:
		return "<invalid>";
	default:
		WARN_ONCE(1, "unknown dynptr type %d\n", type);
		return "<unknown>";
	}
}

const char *iter_type_str(const struct btf *btf, u32 btf_id)
{
	if (!btf || btf_id == 0)
		return "<invalid>";

	/* we already validated that type is valid and has conforming name */
	return btf_type_name(btf, btf_id) + sizeof(ITER_PREFIX) - 1;
}

const char *iter_state_str(enum bpf_iter_state state)
{
	switch (state) {
	case BPF_ITER_STATE_ACTIVE:
		return "active";
	case BPF_ITER_STATE_DRAINED:
		return "drained";
	case BPF_ITER_STATE_INVALID:
		return "<invalid>";
	default:
		WARN_ONCE(1, "unknown iter state %d\n", state);
		return "<unknown>";
	}
}

static char slot_type_char[] = {
	[STACK_INVALID]	= '?',
	[STACK_SPILL]	= 'r',
	[STACK_MISC]	= 'm',
	[STACK_ZERO]	= '0',
	[STACK_DYNPTR]	= 'd',
	[STACK_ITER]	= 'i',
	[STACK_IRQ_FLAG] = 'f'
};

static void print_liveness(struct bpf_verifier_env *env,
			   enum bpf_reg_liveness live)
{
	if (live & (REG_LIVE_READ | REG_LIVE_WRITTEN | REG_LIVE_DONE))
	    verbose(env, "_");
	if (live & REG_LIVE_READ)
		verbose(env, "r");
	if (live & REG_LIVE_WRITTEN)
		verbose(env, "w");
	if (live & REG_LIVE_DONE)
		verbose(env, "D");
}

#define UNUM_MAX_DECIMAL U16_MAX
#define SNUM_MAX_DECIMAL S16_MAX
#define SNUM_MIN_DECIMAL S16_MIN

static bool is_unum_decimal(u64 num)
{
	return num <= UNUM_MAX_DECIMAL;
}

static bool is_snum_decimal(s64 num)
{
	return num >= SNUM_MIN_DECIMAL && num <= SNUM_MAX_DECIMAL;
}

static void verbose_unum(struct bpf_verifier_env *env, u64 num)
{
	if (is_unum_decimal(num))
		verbose(env, "%llu", num);
	else
		verbose(env, "%#llx", num);
}

static void verbose_snum(struct bpf_verifier_env *env, s64 num)
{
	if (is_snum_decimal(num))
		verbose(env, "%lld", num);
	else
		verbose(env, "%#llx", num);
}

int tnum_strn(char *str, size_t size, struct tnum a)
{
	/* print as a constant, if tnum is fully known */
	if (a.mask == 0) {
		if (is_unum_decimal(a.value))
			return snprintf(str, size, "%llu", a.value);
		else
			return snprintf(str, size, "%#llx", a.value);
	}
	return snprintf(str, size, "(%#llx; %#llx)", a.value, a.mask);
}
EXPORT_SYMBOL_GPL(tnum_strn);

static void print_scalar_ranges(struct bpf_verifier_env *env,
				const struct bpf_reg_state *reg,
				const char **sep)
{
	/* For signed ranges, we want to unify 64-bit and 32-bit values in the
	 * output as much as possible, but there is a bit of a complication.
	 * If we choose to print values as decimals, this is natural to do,
	 * because negative 64-bit and 32-bit values >= -S32_MIN have the same
	 * representation due to sign extension. But if we choose to print
	 * them in hex format (see is_snum_decimal()), then sign extension is
	 * misleading.
	 * E.g., smin=-2 and smin32=-2 are exactly the same in decimal, but in
	 * hex they will be smin=0xfffffffffffffffe and smin32=0xfffffffe, two
	 * very different numbers.
	 * So we avoid sign extension if we choose to print values in hex.
	 */
	struct {
		const char *name;
		u64 val;
		bool omit;
	} minmaxs[] = {
		{"smin",   reg->smin_value,         reg->smin_value == S64_MIN},
		{"smax",   reg->smax_value,         reg->smax_value == S64_MAX},
		{"umin",   reg->umin_value,         reg->umin_value == 0},
		{"umax",   reg->umax_value,         reg->umax_value == U64_MAX},
		{"smin32",
		 is_snum_decimal((s64)reg->s32_min_value)
			 ? (s64)reg->s32_min_value
			 : (u32)reg->s32_min_value, reg->s32_min_value == S32_MIN},
		{"smax32",
		 is_snum_decimal((s64)reg->s32_max_value)
			 ? (s64)reg->s32_max_value
			 : (u32)reg->s32_max_value, reg->s32_max_value == S32_MAX},
		{"umin32", reg->u32_min_value,      reg->u32_min_value == 0},
		{"umax32", reg->u32_max_value,      reg->u32_max_value == U32_MAX},
	}, *m1, *m2, *mend = &minmaxs[ARRAY_SIZE(minmaxs)];
	bool neg1, neg2;

	for (m1 = &minmaxs[0]; m1 < mend; m1++) {
		if (m1->omit)
			continue;

		neg1 = m1->name[0] == 's' && (s64)m1->val < 0;

		verbose(env, "%s%s=", *sep, m1->name);
		*sep = ",";

		for (m2 = m1 + 2; m2 < mend; m2 += 2) {
			if (m2->omit || m2->val != m1->val)
				continue;
			/* don't mix negatives with positives */
			neg2 = m2->name[0] == 's' && (s64)m2->val < 0;
			if (neg2 != neg1)
				continue;
			m2->omit = true;
			verbose(env, "%s=", m2->name);
		}

		if (m1->name[0] == 's')
			verbose_snum(env, m1->val);
		else
			verbose_unum(env, m1->val);
	}
}

static bool type_is_map_ptr(enum bpf_reg_type t) {
	switch (base_type(t)) {
	case CONST_PTR_TO_MAP:
	case PTR_TO_MAP_KEY:
	case PTR_TO_MAP_VALUE:
		return true;
	default:
		return false;
	}
}

/*
 * _a stands for append, was shortened to avoid multiline statements below.
 * This macro is used to output a comma separated list of attributes.
 */
#define verbose_a(fmt, ...) ({ verbose(env, "%s" fmt, sep, ##__VA_ARGS__); sep = ","; })

static void print_reg_state(struct bpf_verifier_env *env,
			    const struct bpf_func_state *state,
			    const struct bpf_reg_state *reg)
{
	enum bpf_reg_type t;
	const char *sep = "";

	t = reg->type;
	if (t == SCALAR_VALUE && reg->precise)
		verbose(env, "P");
	if (t == SCALAR_VALUE && tnum_is_const(reg->var_off)) {
		verbose_snum(env, reg->var_off.value);
		return;
	}

	verbose(env, "%s", reg_type_str(env, t));
	if (t == PTR_TO_ARENA)
		return;
	if (t == PTR_TO_STACK) {
		if (state->frameno != reg->frameno)
			verbose(env, "[%d]", reg->frameno);
		if (tnum_is_const(reg->var_off)) {
			verbose_snum(env, reg->var_off.value + reg->off);
			return;
		}
	}
	if (base_type(t) == PTR_TO_BTF_ID)
		verbose(env, "%s", btf_type_name(reg->btf, reg->btf_id));
	verbose(env, "(");
	if (reg->id)
		verbose_a("id=%d", reg->id & ~BPF_ADD_CONST);
	if (reg->id & BPF_ADD_CONST)
		verbose(env, "%+d", reg->off);
	if (reg->ref_obj_id)
		verbose_a("ref_obj_id=%d", reg->ref_obj_id);
	if (type_is_non_owning_ref(reg->type))
		verbose_a("%s", "non_own_ref");
	if (type_is_map_ptr(t)) {
		if (reg->map_ptr->name[0])
			verbose_a("map=%s", reg->map_ptr->name);
		verbose_a("ks=%d,vs=%d",
			  reg->map_ptr->key_size,
			  reg->map_ptr->value_size);
	}
	if (t != SCALAR_VALUE && reg->off) {
		verbose_a("off=");
		verbose_snum(env, reg->off);
	}
	if (type_is_pkt_pointer(t)) {
		verbose_a("r=");
		verbose_unum(env, reg->range);
	}
	if (base_type(t) == PTR_TO_MEM) {
		verbose_a("sz=");
		verbose_unum(env, reg->mem_size);
	}
	if (t == CONST_PTR_TO_DYNPTR)
		verbose_a("type=%s",  dynptr_type_str(reg->dynptr.type));
	if (tnum_is_const(reg->var_off)) {
		/* a pointer register with fixed offset */
		if (reg->var_off.value) {
			verbose_a("imm=");
			verbose_snum(env, reg->var_off.value);
		}
	} else {
		print_scalar_ranges(env, reg, &sep);
		if (!tnum_is_unknown(reg->var_off)) {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose_a("var_off=%s", tn_buf);
		}
	}
	verbose(env, ")");
}

void print_verifier_state(struct bpf_verifier_env *env, const struct bpf_verifier_state *vstate,
			  u32 frameno, bool print_all)
{
	const struct bpf_func_state *state = vstate->frame[frameno];
	const struct bpf_reg_state *reg;
	int i;

	if (state->frameno)
		verbose(env, " frame%d:", state->frameno);
	for (i = 0; i < MAX_BPF_REG; i++) {
		reg = &state->regs[i];
		if (reg->type == NOT_INIT)
			continue;
		if (!print_all && !reg_scratched(env, i))
			continue;
		verbose(env, " R%d", i);
		print_liveness(env, reg->live);
		verbose(env, "=");
		print_reg_state(env, state, reg);
	}
	for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
		char types_buf[BPF_REG_SIZE + 1];
		const char *sep = "";
		bool valid = false;
		u8 slot_type;
		int j;

		if (!print_all && !stack_slot_scratched(env, i))
			continue;

		for (j = 0; j < BPF_REG_SIZE; j++) {
			slot_type = state->stack[i].slot_type[j];
			if (slot_type != STACK_INVALID)
				valid = true;
			types_buf[j] = slot_type_char[slot_type];
		}
		types_buf[BPF_REG_SIZE] = 0;
		if (!valid)
			continue;

		reg = &state->stack[i].spilled_ptr;
		switch (state->stack[i].slot_type[BPF_REG_SIZE - 1]) {
		case STACK_SPILL:
			/* print MISC/ZERO/INVALID slots above subreg spill */
			for (j = 0; j < BPF_REG_SIZE; j++)
				if (state->stack[i].slot_type[j] == STACK_SPILL)
					break;
			types_buf[j] = '\0';

			verbose(env, " fp%d", (-i - 1) * BPF_REG_SIZE);
			print_liveness(env, reg->live);
			verbose(env, "=%s", types_buf);
			print_reg_state(env, state, reg);
			break;
		case STACK_DYNPTR:
			/* skip to main dynptr slot */
			i += BPF_DYNPTR_NR_SLOTS - 1;
			reg = &state->stack[i].spilled_ptr;

			verbose(env, " fp%d", (-i - 1) * BPF_REG_SIZE);
			print_liveness(env, reg->live);
			verbose(env, "=dynptr_%s(", dynptr_type_str(reg->dynptr.type));
			if (reg->id)
				verbose_a("id=%d", reg->id);
			if (reg->ref_obj_id)
				verbose_a("ref_id=%d", reg->ref_obj_id);
			if (reg->dynptr_id)
				verbose_a("dynptr_id=%d", reg->dynptr_id);
			verbose(env, ")");
			break;
		case STACK_ITER:
			/* only main slot has ref_obj_id set; skip others */
			if (!reg->ref_obj_id)
				continue;

			verbose(env, " fp%d", (-i - 1) * BPF_REG_SIZE);
			print_liveness(env, reg->live);
			verbose(env, "=iter_%s(ref_id=%d,state=%s,depth=%u)",
				iter_type_str(reg->iter.btf, reg->iter.btf_id),
				reg->ref_obj_id, iter_state_str(reg->iter.state),
				reg->iter.depth);
			break;
		case STACK_MISC:
		case STACK_ZERO:
		default:
			verbose(env, " fp%d", (-i - 1) * BPF_REG_SIZE);
			print_liveness(env, reg->live);
			verbose(env, "=%s", types_buf);
			break;
		}
	}
	if (vstate->acquired_refs && vstate->refs[0].id) {
		verbose(env, " refs=%d", vstate->refs[0].id);
		for (i = 1; i < vstate->acquired_refs; i++)
			if (vstate->refs[i].id)
				verbose(env, ",%d", vstate->refs[i].id);
	}
	if (state->in_callback_fn)
		verbose(env, " cb");
	if (state->in_async_callback_fn)
		verbose(env, " async_cb");
	verbose(env, "\n");
	if (!print_all)
		mark_verifier_state_clean(env);
}

static inline u32 vlog_alignment(u32 pos)
{
	return round_up(max(pos + BPF_LOG_MIN_ALIGNMENT / 2, BPF_LOG_ALIGNMENT),
			BPF_LOG_MIN_ALIGNMENT) - pos - 1;
}

void print_insn_state(struct bpf_verifier_env *env, const struct bpf_verifier_state *vstate,
		      u32 frameno)
{
	if (env->prev_log_pos && env->prev_log_pos == env->log.end_pos) {
		/* remove new line character */
		bpf_vlog_reset(&env->log, env->prev_log_pos - 1);
		verbose(env, "%*c;", vlog_alignment(env->prev_insn_print_pos), ' ');
	} else {
		verbose(env, "%d:", env->insn_idx);
	}
	print_verifier_state(env, vstate, frameno, false);
}
