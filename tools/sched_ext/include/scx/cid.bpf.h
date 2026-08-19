/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF-side helpers for cids and cmasks. See kernel/sched/ext/cid.h for the
 * authoritative layout and semantics. The BPF-side helpers use the cmask_*
 * naming (no scx_ prefix); cmask is the SCX bitmap type so the prefix is
 * redundant in BPF code. Atomics use __sync_val_compare_and_swap and every
 * helper is inline (no .c counterpart).
 *
 * Included by scx/common.bpf.h; don't include directly.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef __SCX_CID_BPF_H
#define __SCX_CID_BPF_H

#include "bpf_arena_common.bpf.h"

#ifndef BIT_U64
#define BIT_U64(nr)		(1ULL << (nr))
#endif
#ifndef GENMASK_U64
#define GENMASK_U64(h, l)	((~0ULL << (l)) & (~0ULL >> (63 - (h))))
#endif

/*
 * Storage cap for bounded loops over bits[]. Sized to cover NR_CPUS=8192 with
 * one extra word for head-misalignment. Increase if deployment targets larger
 * NR_CPUS.
 */
#ifndef CMASK_MAX_WORDS
#define CMASK_MAX_WORDS 129
#endif

/*
 * Mirrors SCX_CMASK_NR_WORDS in kernel/sched/ext/types.h. The u64 cast keeps
 * the +63 from wrapping when @nr_cids is near U32_MAX, so cmask_reframe()
 * bounds-checking the result against alloc_words catches the overflow instead
 * of seeing a small value.
 */
#define CMASK_NR_WORDS(nr_cids)		((u32)(((u64)(nr_cids) + 63) / 64 + 1))

static __always_inline bool __cmask_contains(u32 cid, const struct scx_cmask __arena *m)
{
	return cid >= m->base && cid < m->base + m->nr_cids;
}

static __always_inline u64 __arena *__cmask_word(u32 cid, const struct scx_cmask __arena *m)
{
	return (u64 __arena *)&m->bits[cid / 64 - m->base / 64];
}

/**
 * __cmask_init - Initialize @m with explicit storage capacity
 * @m: cmask to initialize
 * @base: first cid of the active range
 * @nr_cids: number of cids in the active range
 * @alloc_cids: storage capacity in cids, at least @nr_cids
 *
 * Use when storage is sized larger than the initial active range. All of
 * bits[] is zeroed.
 */
static __always_inline void __cmask_init(struct scx_cmask __arena *m, u32 base,
					 u32 nr_cids, u32 alloc_cids)
{
	u32 alloc_words, i;

	if (unlikely(nr_cids > alloc_cids)) {
		scx_bpf_error("__cmask_init: nr_cids=%u exceeds alloc_cids=%u",
			      nr_cids, alloc_cids);
		return;
	}
	alloc_words = CMASK_NR_WORDS(alloc_cids);

	m->base = base;
	m->nr_cids = nr_cids;
	m->alloc_words = alloc_words;

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= alloc_words)
			break;
		m->bits[i] = 0;
	}
}

/**
 * cmask_init - Initialize @m on tight storage
 * @m: cmask to initialize
 * @base: first cid of the active range
 * @nr_cids: number of cids in the active range
 *
 * All of bits[] is zeroed.
 */
static __always_inline void cmask_init(struct scx_cmask __arena *m, u32 base, u32 nr_cids)
{
	__cmask_init(m, base, nr_cids, nr_cids);
}

/**
 * cmask_reframe - Reshape @m's active range without resizing storage
 * @m: cmask to reframe
 * @base: new active range base
 * @nr_cids: new active range length, must fit within @m->alloc_words
 *
 * Body bits within the new range become garbage - only the head and tail
 * words are zeroed to keep the padding invariant.
 */
static __always_inline void cmask_reframe(struct scx_cmask __arena *m, u32 base, u32 nr_cids)
{
	if (CMASK_NR_WORDS(nr_cids) > m->alloc_words) {
		scx_bpf_error("cmask_reframe: nr_cids=%u exceeds alloc_words=%u",
			      nr_cids, m->alloc_words);
		return;
	}
	if (nr_cids) {
		u32 last_word = ((base & 63) + nr_cids - 1) / 64;

		m->bits[0] = 0;
		m->bits[last_word] = 0;
	}
	m->base = base;
	m->nr_cids = nr_cids;
}

static __always_inline bool cmask_test(u32 cid, const struct scx_cmask __arena *m)
{
	if (!__cmask_contains(cid, m))
		return false;
	return *__cmask_word(cid, m) & BIT_U64(cid & 63);
}

/*
 * x86 BPF JIT rejects BPF_OR | BPF_FETCH and BPF_AND | BPF_FETCH on arena
 * pointers (see bpf_jit_supports_insn() in arch/x86/net/bpf_jit_comp.c). Only
 * BPF_CMPXCHG / BPF_XCHG / BPF_ADD with FETCH are allowed. Implement
 * test_and_{set,clear} and the atomic set/clear via a cmpxchg loop.
 *
 * CMASK_CAS_TRIES is sized so exhausting it means seconds of real spinning
 * on one word - past any plausible contention. Abort hard.
 */
#define CMASK_CAS_TRIES		(1U << 23)

static __always_inline void cmask_set(u32 cid, struct scx_cmask __arena *m)
{
	u64 __arena *w;
	u64 bit, old, new;
	u32 i;

	if (!__cmask_contains(cid, m))
		return;
	w = __cmask_word(cid, m);
	bit = BIT_U64(cid & 63);
	bpf_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if (old & bit)
			return;
		new = old | bit;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return;
	}
	scx_bpf_error("cmask_set CAS exhausted at cid %u", cid);
}

static __always_inline void cmask_clear(u32 cid, struct scx_cmask __arena *m)
{
	u64 __arena *w;
	u64 bit, old, new;
	u32 i;

	if (!__cmask_contains(cid, m))
		return;
	w = __cmask_word(cid, m);
	bit = BIT_U64(cid & 63);
	bpf_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if (!(old & bit))
			return;
		new = old & ~bit;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return;
	}
	scx_bpf_error("cmask_clear CAS exhausted at cid %u", cid);
}

static __always_inline bool cmask_test_and_set(u32 cid, struct scx_cmask __arena *m)
{
	u64 __arena *w;
	u64 bit, old, new;
	u32 i;

	if (!__cmask_contains(cid, m))
		return false;
	w = __cmask_word(cid, m);
	bit = BIT_U64(cid & 63);
	bpf_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if (old & bit)
			return true;
		new = old | bit;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return false;
	}
	scx_bpf_error("cmask_test_and_set CAS exhausted at cid %u", cid);
	return false;
}

static __always_inline bool cmask_test_and_clear(u32 cid, struct scx_cmask __arena *m)
{
	u64 __arena *w;
	u64 bit, old, new;
	u32 i;

	if (!__cmask_contains(cid, m))
		return false;
	w = __cmask_word(cid, m);
	bit = BIT_U64(cid & 63);
	bpf_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if (!(old & bit))
			return false;
		new = old & ~bit;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return true;
	}
	scx_bpf_error("cmask_test_and_clear CAS exhausted at cid %u", cid);
	return false;
}

static __always_inline void __cmask_set(u32 cid, struct scx_cmask __arena *m)
{
	if (!__cmask_contains(cid, m))
		return;
	*__cmask_word(cid, m) |= BIT_U64(cid & 63);
}

static __always_inline void __cmask_clear(u32 cid, struct scx_cmask __arena *m)
{
	if (!__cmask_contains(cid, m))
		return;
	*__cmask_word(cid, m) &= ~BIT_U64(cid & 63);
}

static __always_inline bool __cmask_test_and_set(u32 cid, struct scx_cmask __arena *m)
{
	u64 bit = BIT_U64(cid & 63);
	u64 __arena *w;
	u64 prev;

	if (!__cmask_contains(cid, m))
		return false;
	w = __cmask_word(cid, m);
	prev = *w & bit;
	*w |= bit;
	return prev;
}

static __always_inline bool __cmask_test_and_clear(u32 cid, struct scx_cmask __arena *m)
{
	u64 bit = BIT_U64(cid & 63);
	u64 __arena *w;
	u64 prev;

	if (!__cmask_contains(cid, m))
		return false;
	w = __cmask_word(cid, m);
	prev = *w & bit;
	*w &= ~bit;
	return prev;
}

static __always_inline void cmask_zero(struct scx_cmask __arena *m)
{
	u32 nr_words = CMASK_NR_WORDS(m->nr_cids), i;

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		m->bits[i] = 0;
	}
}

/*
 * BPF_-prefixed to avoid colliding with the kernel's anonymous CMASK_OP_*
 * enum in ext/cid.c, which is exported via BTF and reachable through
 * vmlinux.h.
 */
enum {
	BPF_CMASK_OP_AND,
	BPF_CMASK_OP_OR,
	BPF_CMASK_OP_COPY,
	BPF_CMASK_OP_ANDNOT,
};

static __always_inline void cmask_op_word(struct scx_cmask __arena *dst,
					  const struct scx_cmask __arena *src,
					  u32 di, u32 si, u64 mask, int op)
{
	u64 dv = dst->bits[di];
	u64 sv = src->bits[si];
	u64 rv;

	if (op == BPF_CMASK_OP_AND)
		rv = dv & sv;
	else if (op == BPF_CMASK_OP_OR)
		rv = dv | sv;
	else if (op == BPF_CMASK_OP_ANDNOT)
		rv = dv & ~sv;
	else
		rv = sv;

	dst->bits[di] = (dv & ~mask) | (rv & mask);
}

static __always_inline void cmask_op(struct scx_cmask __arena *dst,
				     const struct scx_cmask __arena *src, int op)
{
	u32 d_end = dst->base + dst->nr_cids;
	u32 s_end = src->base + src->nr_cids;
	u32 lo = dst->base > src->base ? dst->base : src->base;
	u32 hi = d_end < s_end ? d_end : s_end;
	u32 d_base = dst->base / 64;
	u32 s_base = src->base / 64;
	u32 lo_word, hi_word, i;
	u64 head_mask, tail_mask;

	if (lo >= hi)
		return;

	lo_word = lo / 64;
	hi_word = (hi - 1) / 64;
	head_mask = GENMASK_U64(63, lo & 63);
	tail_mask = GENMASK_U64((hi - 1) & 63, 0);

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		u32 w = lo_word + i;
		u64 m;

		if (w > hi_word)
			break;

		m = GENMASK_U64(63, 0);
		if (w == lo_word)
			m &= head_mask;
		if (w == hi_word)
			m &= tail_mask;

		cmask_op_word(dst, src, w - d_base, w - s_base, m, op);
	}
}

/*
 * cmask_and/or/copy only modify @dst bits that lie in the intersection of
 * [@dst->base, @dst->base + @dst->nr_cids) and [@src->base,
 * @src->base + @src->nr_cids). Bits in @dst outside that window
 * keep their prior values - in particular, cmask_copy() does NOT zero @dst
 * bits that lie outside @src's range.
 */
static __always_inline void cmask_and(struct scx_cmask __arena *dst,
				      const struct scx_cmask __arena *src)
{
	cmask_op(dst, src, BPF_CMASK_OP_AND);
}

static __always_inline void cmask_or(struct scx_cmask __arena *dst,
				     const struct scx_cmask __arena *src)
{
	cmask_op(dst, src, BPF_CMASK_OP_OR);
}

static __always_inline void cmask_copy(struct scx_cmask __arena *dst,
				       const struct scx_cmask __arena *src)
{
	cmask_op(dst, src, BPF_CMASK_OP_COPY);
}

static __always_inline void cmask_andnot(struct scx_cmask __arena *dst,
					 const struct scx_cmask __arena *src)
{
	cmask_op(dst, src, BPF_CMASK_OP_ANDNOT);
}

/*
 * True iff @a and @b have identical bits over their (assumed equal) range.
 * Callers are expected to pass same-shape cmasks; differing shapes always
 * compare unequal.
 */
static __always_inline bool cmask_equal(const struct scx_cmask __arena *a,
					const struct scx_cmask __arena *b)
{
	u32 nr_words, i;

	if (a->base != b->base || a->nr_cids != b->nr_cids)
		return false;
	nr_words = CMASK_NR_WORDS(a->nr_cids);

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		if (a->bits[i] != b->bits[i])
			return false;
	}
	return true;
}

/*
 * True iff every bit set in @a is also set in @b over the intersection of
 * their ranges. Bits of @a outside @b's range fail the test.
 */
static __always_inline bool cmask_subset(const struct scx_cmask __arena *a,
					 const struct scx_cmask __arena *b)
{
	u32 a_end = a->base + a->nr_cids;
	u32 b_end = b->base + b->nr_cids;
	u32 a_wbase = a->base / 64;
	u32 b_wbase = b->base / 64;
	u32 nr_words, i;

	/* any bit of @a outside @b's range is a subset violation */
	if (a->base < b->base || a_end > b_end)
		return false;

	nr_words = CMASK_NR_WORDS(a->nr_cids);
	bpf_for(i, 0, CMASK_MAX_WORDS) {
		u32 wi_b;

		if (i >= nr_words)
			break;
		wi_b = a_wbase + i - b_wbase;
		if (a->bits[i] & ~b->bits[wi_b])
			return false;
	}
	return true;
}

/**
 * cmask_next_set - find the first set bit at or after @cid
 * @m: cmask to search
 * @cid: starting cid (clamped to @m->base if below)
 *
 * Returns the smallest set cid in [@cid, @m->base + @m->nr_cids), or
 * @m->base + @m->nr_cids if none (the out-of-range sentinel matches the
 * termination condition used by cmask_for_each()).
 */
static __always_inline u32 cmask_next_set(const struct scx_cmask __arena *m, u32 cid)
{
	u32 end = m->base + m->nr_cids;
	u32 base = m->base / 64;
	u32 last_wi = (end - 1) / 64 - base;
	u32 start_wi, start_bit, i;

	if (cid < m->base)
		cid = m->base;
	if (cid >= end)
		return end;

	start_wi = cid / 64 - base;
	start_bit = cid & 63;

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		u32 wi = start_wi + i;
		u64 word;
		u32 found;

		if (wi > last_wi)
			break;

		word = m->bits[wi];
		if (i == 0)
			word &= GENMASK_U64(63, start_bit);
		if (!word)
			continue;

		found = (base + wi) * 64 + ctzll(word);
		if (found >= end)
			return end;
		return found;
	}
	return end;
}

static __always_inline u32 cmask_first_set(const struct scx_cmask __arena *m)
{
	return cmask_next_set(m, m->base);
}

#define cmask_for_each(cid, m)							\
	for ((cid) = cmask_first_set(m);					\
	     (cid) < (m)->base + (m)->nr_cids;					\
	     (cid) = cmask_next_set((m), (cid) + 1))

/*
 * Population count over [base, base + nr_cids). Padding bits in the head/tail
 * words are guaranteed zero by the mutating helpers, so a flat popcount over
 * all words is correct.
 */
static __always_inline u32 cmask_weight(const struct scx_cmask __arena *m)
{
	u32 nr_words = CMASK_NR_WORDS(m->nr_cids), i;
	u32 count = 0;

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		count += __builtin_popcountll(m->bits[i]);
	}
	return count;
}

/*
 * True if @a and @b share any set bit. Walk only the intersection of their
 * ranges, matching the semantics of cmask_and().
 */
static __always_inline bool cmask_intersects(const struct scx_cmask __arena *a,
					     const struct scx_cmask __arena *b)
{
	u32 a_end = a->base + a->nr_cids;
	u32 b_end = b->base + b->nr_cids;
	u32 lo = a->base > b->base ? a->base : b->base;
	u32 hi = a_end < b_end ? a_end : b_end;
	u32 a_base = a->base / 64;
	u32 b_base = b->base / 64;
	u32 lo_word, hi_word, i;
	u64 head_mask, tail_mask;

	if (lo >= hi)
		return false;

	lo_word = lo / 64;
	hi_word = (hi - 1) / 64;
	head_mask = GENMASK_U64(63, lo & 63);
	tail_mask = GENMASK_U64((hi - 1) & 63, 0);

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		u32 w = lo_word + i;
		u64 mask, av, bv;

		if (w > hi_word)
			break;

		mask = GENMASK_U64(63, 0);
		if (w == lo_word)
			mask &= head_mask;
		if (w == hi_word)
			mask &= tail_mask;

		av = a->bits[w - a_base] & mask;
		bv = b->bits[w - b_base] & mask;
		if (av & bv)
			return true;
	}
	return false;
}

/*
 * Find the next cid set in both @a and @b at or after @start, bounded by the
 * intersection of the two ranges. Return a->base + a->nr_cids if none found.
 *
 * Building block for cmask_next_and_set_wrap(). Callers that want a bounded
 * scan without wrap call this directly.
 */
static __always_inline u32 cmask_next_and_set(const struct scx_cmask __arena *a,
					      const struct scx_cmask __arena *b,
					      u32 start)
{
	u32 a_end = a->base + a->nr_cids;
	u32 b_end = b->base + b->nr_cids;
	u32 a_wbase = a->base / 64;
	u32 b_wbase = b->base / 64;
	u32 lo = a->base > b->base ? a->base : b->base;
	u32 hi = a_end < b_end ? a_end : b_end;
	u32 last_wi, start_wi, start_bit, i;

	if (lo >= hi)
		return a_end;
	if (start < lo)
		start = lo;
	if (start >= hi)
		return a_end;

	last_wi = (hi - 1) / 64;
	start_wi = start / 64;
	start_bit = start & 63;

	bpf_for(i, 0, CMASK_MAX_WORDS) {
		u32 abs_wi = start_wi + i;
		u64 word;
		u32 found;

		if (abs_wi > last_wi)
			break;

		word = a->bits[abs_wi - a_wbase] & b->bits[abs_wi - b_wbase];
		if (i == 0)
			word &= GENMASK_U64(63, start_bit);
		if (!word)
			continue;

		found = abs_wi * 64 + ctzll(word);
		if (found >= hi)
			return a_end;
		return found;
	}
	return a_end;
}

/*
 * Find the next set cid in @m at or after @start, wrapping to @m->base if no
 * set bit is found in [start, m->base + m->nr_cids). Return m->base +
 * m->nr_cids if @m is empty.
 *
 * Callers do round-robin distribution by passing (last_cid + 1) as @start.
 */
static __always_inline u32 cmask_next_set_wrap(const struct scx_cmask __arena *m,
					       u32 start)
{
	u32 end = m->base + m->nr_cids;
	u32 found;

	found = cmask_next_set(m, start);
	if (found < end || start <= m->base)
		return found;

	found = cmask_next_set(m, m->base);
	return found < start ? found : end;
}

/*
 * Find the next cid set in both @a and @b at or after @start, wrapping to
 * @a->base if none found in the forward half. Return a->base + a->nr_cids
 * if the intersection is empty.
 *
 * Callers do round-robin distribution by passing (last_cid + 1) as @start.
 */
static __always_inline u32 cmask_next_and_set_wrap(const struct scx_cmask __arena *a,
						   const struct scx_cmask __arena *b,
						   u32 start)
{
	u32 a_end = a->base + a->nr_cids;
	u32 found;

	found = cmask_next_and_set(a, b, start);
	if (found < a_end || start <= a->base)
		return found;

	found = cmask_next_and_set(a, b, a->base);
	return found < start ? found : a_end;
}

/**
 * cmask_from_cpumask - translate a kernel cpumask to a cid-space cmask
 * @m: cmask to fill. Zeroed first; only bits within [@m->base, @m->base +
 *     @m->nr_cids) are updated - cpus mapping to cids outside that range
 *     are ignored.
 * @cpumask: kernel cpumask to translate
 *
 * For each cpu in @cpumask, set the cpu's cid in @m. Caller must ensure
 * @cpumask stays stable across the call (e.g. RCU read lock for
 * task->cpus_ptr).
 */
static __always_inline void cmask_from_cpumask(struct scx_cmask __arena *m,
					       const struct cpumask *cpumask)
{
	u32 nr_cpu_ids = scx_bpf_nr_cpu_ids();
	s32 cpu;

	cmask_zero(m);
	bpf_for(cpu, 0, nr_cpu_ids) {
		s32 cid;

		if (!bpf_cpumask_test_cpu(cpu, cpumask))
			continue;
		cid = scx_bpf_cpu_to_cid(cpu);
		if (cid >= 0)
			__cmask_set(cid, m);
	}
}

#endif /* __SCX_CID_BPF_H */
