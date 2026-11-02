/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF-side helpers for cids and cmasks. See kernel/sched/ext/cid.h for the
 * authoritative layout and semantics. The BPF-side helpers use the cmask_*
 * naming (no scx_ prefix); cmask is the SCX bitmap type so the prefix is
 * redundant in BPF code. Every helper is inline except the binary operations,
 * which are weak global functions defined here, so there is no .c counterpart.
 *
 * Included by scx/common.bpf.h; don't include directly.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef __SCX_CID_BPF_H
#define __SCX_CID_BPF_H

#include "bpf_arena_common.bpf.h"

/* libbpf's bpf_helpers.h defines it from 1.4 on */
#ifndef __arg_arena
#define __arg_arena __attribute((btf_decl_tag("arg:arena")))
#endif

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

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
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

/**
 * cmask_nr_words - Words of bits[] that @m's active range spans
 * @m: cmask to measure
 *
 * @m->base need not be word aligned: bits[0] covers the whole word @m->base
 * falls in, so the count is taken from that word, not from @m->base.
 */
static __always_inline u32 cmask_nr_words(const struct scx_cmask __arena *m)
{
	u32 wbase = m->base / 64;

	return m->nr_cids ? (m->base + m->nr_cids - 1) / 64 - wbase + 1 : 0;
}

/**
 * cmask_word - Read word @k of @m
 * @m: cmask to read
 * @k: word index, counted from the word @m->base falls in
 *
 * A word past the active range reads as 0, so a scan that looks one word
 * ahead (at an SMT sibling that falls in the next word, say) needs no bound
 * of its own.
 */
static __always_inline u64 cmask_word(const struct scx_cmask __arena *m, u32 k)
{
	if (k >= cmask_nr_words(m))
		return 0;
	return m->bits[k];
}

/**
 * cmask_range_word - Bits of word @k of @m that fall in [@start, @start + @nr)
 * @m: cmask the word belongs to
 * @k: word index, counted from the word @m->base falls in
 * @start: first cid of the range
 * @nr: number of cids in the range
 *
 * Scoping a scan to a domain that is contiguous in cid space, an LLC or a
 * node, is then one AND per word, rather than a test of the ends of the
 * range at every bit or a mask kept per domain.
 */
static __always_inline u64 cmask_range_word(const struct scx_cmask __arena *m,
					    u32 k, u32 start, u32 nr)
{
	u64 wlo = (u64)(m->base / 64 + k) * 64, whi = wlo + 64;
	u64 lo = start, hi = (u64)start + nr;

	if (lo < wlo)
		lo = wlo;
	if (hi > whi)
		hi = whi;
	if (lo >= hi)
		return 0;

	return GENMASK_U64(hi - wlo - 1, lo - wlo);
}

/**
 * __cmask_test - Test a cid without checking it against the active range
 * @cid: cid to test
 * @m: cmask to test
 *
 * The caller must already know that @cid lies within
 * [@m->base, @m->base + @m->nr_cids), or the read runs off @m->bits.
 *
 * For hot scans that have already bounded @cid, where the per-call range check
 * is repeated work on every candidate.
 */
static __always_inline bool __cmask_test(u32 cid, const struct scx_cmask __arena *m)
{
	return *__cmask_word(cid, m) & BIT_U64(cid & 63);
}

static __always_inline bool cmask_test(u32 cid, const struct scx_cmask __arena *m)
{
	if (!__cmask_contains(cid, m))
		return false;
	return __cmask_test(cid, m);
}

/*
 * The bit helpers use the fetching bitwise builtins, which order like the
 * cmpxchg they replace, and not every JIT accepts those on arena pointers. The
 * loader sets SCX_LIB_FEAT_ARENA_FETCH_BITOPS when a probe with the fetching
 * form loads, and the helpers run a cmpxchg loop otherwise. The fetched value
 * is consumed through barrier_var() because clang 19 lowers a builtin whose
 * result is unused to the non-fetching form, which is relaxed on arm64.
 *
 * CMASK_CAS_TRIES is sized so exhausting it means seconds of real spinning
 * on one word, past any plausible contention. Abort hard.
 */
#define CMASK_CAS_TRIES		(1U << 23)

static __always_inline u64 __cmask_fetch_or(u64 __arena *w, u64 mask)
{
	u64 old = __atomic_fetch_or(w, mask, __ATOMIC_ACQ_REL);

	barrier_var(old);
	return old;
}

static __always_inline u64 __cmask_fetch_andnot(u64 __arena *w, u64 mask)
{
	u64 old = __atomic_fetch_and(w, ~mask, __ATOMIC_ACQ_REL);

	barrier_var(old);
	return old;
}

static __always_inline void cmask_set(u32 cid, struct scx_cmask __arena *m)
{
	u64 __arena *w;
	u64 bit, old, new;
	u32 i;

	if (!__cmask_contains(cid, m))
		return;
	w = __cmask_word(cid, m);
	bit = BIT_U64(cid & 63);
	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS)) {
		__cmask_fetch_or(w, bit);
		return;
	}
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
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
	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS)) {
		__cmask_fetch_andnot(w, bit);
		return;
	}
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
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
	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS))
		return __cmask_fetch_or(w, bit) & bit;
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
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
	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS))
		return __cmask_fetch_andnot(w, bit) & bit;
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
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

/* atomically or @mask into *@w */
static __always_inline void __cmask_word_or(u64 __arena *w, u64 mask)
{
	u64 old, new;
	u32 i;

	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS)) {
		__cmask_fetch_or(w, mask);
		return;
	}
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if ((old & mask) == mask)
			return;
		new = old | mask;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return;
	}
	scx_bpf_error("__cmask_word_or CAS exhausted");
}

/* atomically clear @mask bits in *@w */
static __always_inline void __cmask_word_andnot(u64 __arena *w, u64 mask)
{
	u64 old, new;
	u32 i;

	if (scx_lib_has(SCX_LIB_FEAT_ARENA_FETCH_BITOPS)) {
		__cmask_fetch_andnot(w, mask);
		return;
	}
	bpf_arena_for(i, 0, CMASK_CAS_TRIES) {
		old = *w;
		if (!(old & mask))
			return;
		new = old & ~mask;
		if (__sync_val_compare_and_swap(w, old, new) == old)
			return;
	}
	scx_bpf_error("__cmask_word_andnot CAS exhausted");
}

/**
 * cmask_full_range - Test whether every cid in [@start, @start + @nr) is set
 * @m: cmask to test
 * @start: first cid of the range
 * @nr: number of cids in the range
 *
 * The range is clamped to @m's active range first. True when no clamped bit is
 * clear, including when the clamped range is empty.
 */
static __always_inline bool cmask_full_range(const struct scx_cmask __arena *m, u32 start,
					     u32 nr)
{
	u64 end = (u64)start + nr;
	u32 wbase = m->base / 64;
	u32 first_wi, last_wi, first_bit, last_bit, last, i;

	if (start < m->base)
		start = m->base;
	if (end > m->base + m->nr_cids)
		end = m->base + m->nr_cids;
	if (start >= end)
		return true;
	last = end - 1;

	first_wi = start / 64 - wbase;
	last_wi = last / 64 - wbase;
	first_bit = start & 63;
	last_bit = last & 63;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		u32 wi = first_wi + i;
		u64 mask = ~0ULL;

		if (wi > last_wi)
			break;
		if (wi == first_wi)
			mask &= GENMASK_U64(63, first_bit);
		if (wi == last_wi)
			mask &= GENMASK_U64(last_bit, 0);
		if ((m->bits[wi] & mask) != mask)
			return false;
	}
	return true;
}

/**
 * cmask_set_range - Set every cid in [@start, @start + @nr)
 * @m: cmask to modify
 * @start: first cid of the range
 * @nr: number of cids in the range
 *
 * The range is clamped to @m's active range first. Words are updated atomically
 * so concurrent updates of other bits sharing a word are never lost. The range
 * as a whole does not transition atomically.
 */
static __always_inline void cmask_set_range(struct scx_cmask __arena *m, u32 start, u32 nr)
{
	u64 end = (u64)start + nr;
	u32 wbase = m->base / 64;
	u32 first_wi, last_wi, first_bit, last_bit, last, i;

	if (start < m->base)
		start = m->base;
	if (end > m->base + m->nr_cids)
		end = m->base + m->nr_cids;
	if (start >= end)
		return;
	last = end - 1;

	first_wi = start / 64 - wbase;
	last_wi = last / 64 - wbase;
	first_bit = start & 63;
	last_bit = last & 63;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		u32 wi = first_wi + i;
		u64 mask = ~0ULL;

		if (wi > last_wi)
			break;
		if (wi == first_wi)
			mask &= GENMASK_U64(63, first_bit);
		if (wi == last_wi)
			mask &= GENMASK_U64(last_bit, 0);
		__cmask_word_or(&m->bits[wi], mask);
	}
}

/**
 * cmask_clear_range - Clear every cid in [@start, @start + @nr)
 * @m: cmask to modify
 * @start: first cid of the range
 * @nr: number of cids in the range
 *
 * The range is clamped to @m's active range first. Words are updated atomically
 * so concurrent updates of other bits sharing a word are never lost. The range
 * as a whole does not transition atomically.
 */
static __always_inline void cmask_clear_range(struct scx_cmask __arena *m, u32 start, u32 nr)
{
	u64 end = (u64)start + nr;
	u32 wbase = m->base / 64;
	u32 first_wi, last_wi, first_bit, last_bit, last, i;

	if (start < m->base)
		start = m->base;
	if (end > m->base + m->nr_cids)
		end = m->base + m->nr_cids;
	if (start >= end)
		return;
	last = end - 1;

	first_wi = start / 64 - wbase;
	last_wi = last / 64 - wbase;
	first_bit = start & 63;
	last_bit = last & 63;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		u32 wi = first_wi + i;
		u64 mask = ~0ULL;

		if (wi > last_wi)
			break;
		if (wi == first_wi)
			mask &= GENMASK_U64(63, first_bit);
		if (wi == last_wi)
			mask &= GENMASK_U64(last_bit, 0);
		__cmask_word_andnot(&m->bits[wi], mask);
	}
}

static __always_inline void cmask_zero(struct scx_cmask __arena *m)
{
	u32 nr_words = CMASK_NR_WORDS(m->nr_cids), i;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		m->bits[i] = 0;
	}
}

/**
 * cmask_fill - Set every cid in @m's active range
 * @m: cmask to fill
 *
 * Counterpart to cmask_zero(). Storage past the active range is left as is,
 * matching the kernel-side scx_cmask_fill().
 */
static __always_inline void cmask_fill(struct scx_cmask __arena *m)
{
	u32 nr_words, head_bits, tail_bits, i;

	if (!m->nr_cids)
		return;
	nr_words = (m->base + m->nr_cids - 1) / 64 - m->base / 64 + 1;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		m->bits[i] = ~0LLU;
	}

	/* clear word-0 bits below base */
	head_bits = m->base & 63;
	if (head_bits)
		m->bits[0] &= ~((1LLU << head_bits) - 1);

	/* clear last-word bits at or past base + nr_cids */
	tail_bits = (m->base + m->nr_cids) & 63;
	if (tail_bits)
		m->bits[nr_words - 1] &= (1LLU << tail_bits) - 1;
}

/**
 * cmask_empty - Test whether no cid is set in @m's active range
 * @m: cmask to test
 *
 * Scans the words the active range spans, whose bits outside the range every
 * mutator keeps zero.
 */
static __always_inline bool cmask_empty(const struct scx_cmask __arena *m)
{
	u32 nr_words = cmask_nr_words(m), i;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		if (m->bits[i])
			return false;
	}
	return true;
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

/* range bits of word @k of @m, which spans @nr words: 0 past the span */
static __always_inline u64 __cmask_word_range(const struct scx_cmask __arena *m, u32 k,
					      u32 nr)
{
	u64 bits = ~0ULL;

	if (k >= nr)
		return 0;
	if (k == 0)
		bits &= GENMASK_U64(63, m->base & 63);
	if (k == nr - 1)
		bits &= GENMASK_U64((m->base + m->nr_cids - 1) & 63, 0);
	return bits;
}

/*
 * The binary operations combine cmasks of any active ranges. A cmask holds bits
 * only for its own range and reads as the operation's identity outside it, all
 * ones for AND and all zeros for OR, so an operand alters only the bits it
 * covers: ANDing a shard's mask into a global one edits that shard's window and
 * leaves the rest of the global mask alone. ANDNOT is AND with @src2 inverted
 * over @src2's range, so where only @src2 has range the result is ~@src2.
 *
 * @dst is written on its range intersected with the union of the sources'
 * ranges and untouched elsewhere, and its padding bits outside its own range
 * stay zero, the invariant every cmask mutator keeps. @dst may alias either
 * source, and the two sources may be one mask. Returns whether @dst has a bit
 * set afterwards, over all of @dst's range.
 *
 * The operations are global functions, verified once per program. Inlined,
 * every data-dependent branch in this loop would multiply the states the
 * verifier explores per iteration at every call site.
 */
static __always_inline bool __cmask_op(struct scx_cmask __arena *dst,
				       const struct scx_cmask __arena *src1,
				       const struct scx_cmask __arena *src2, int op)
{
	u32 d_first = dst->base / 64, d_nr = cmask_nr_words(dst);
	u32 s1_first = src1->base / 64, s1_nr = cmask_nr_words(src1);
	u32 s2_first = src2->base / 64, s2_nr = cmask_nr_words(src2);
	u64 any = bpf_arena_loop_zero;
	u32 i;

	bpf_arena_for(i, 0, d_nr) {
		u32 k1 = d_first + i - s1_first, k2 = d_first + i - s2_first;
		u64 m1 = __cmask_word_range(src1, k1, s1_nr);
		u64 m2 = op == BPF_CMASK_OP_COPY ? m1 : __cmask_word_range(src2, k2, s2_nr);
		u64 um = __cmask_word_range(dst, i, d_nr) & (m1 | m2);
		u64 dv = dst->bits[i], v1, v2, rv;

		if (um) {
			/* a word in range holds zeros outside the range */
			v1 = m1 ? src1->bits[k1] : 0;
			v2 = m2 ? src2->bits[k2] : 0;
			if (op == BPF_CMASK_OP_AND)
				rv = (v1 | ~m1) & (v2 | ~m2);
			else if (op == BPF_CMASK_OP_OR)
				rv = v1 | v2;
			else if (op == BPF_CMASK_OP_ANDNOT)
				rv = (v1 | ~m1) & ~v2;
			else
				rv = v1;
			dv = (dv & ~um) | (rv & um);
			dst->bits[i] = dv;
		}
		any |= dv;
	}
	return any != 0;
}

/**
 * cmask_and - Store @src1 AND @src2 in @dst
 * @dst: destination, may be @src1 or @src2
 * @src1: first operand
 * @src2: second operand
 *
 * See __cmask_op() for how the ranges combine. Return whether @dst has a bit
 * set afterwards.
 */
__weak bool cmask_and(struct scx_cmask __arena __arg_arena *dst,
		      const struct scx_cmask __arena __arg_arena *src1,
		      const struct scx_cmask __arena __arg_arena *src2)
{
	return __cmask_op(dst, src1, src2, BPF_CMASK_OP_AND);
}

/**
 * cmask_or - Store @src1 OR @src2 in @dst
 * @dst: destination, may be @src1 or @src2
 * @src1: first operand
 * @src2: second operand
 *
 * See __cmask_op() for how the ranges combine. Return whether @dst has a bit
 * set afterwards.
 */
__weak bool cmask_or(struct scx_cmask __arena __arg_arena *dst,
		     const struct scx_cmask __arena __arg_arena *src1,
		     const struct scx_cmask __arena __arg_arena *src2)
{
	return __cmask_op(dst, src1, src2, BPF_CMASK_OP_OR);
}

/**
 * cmask_andnot - Store @src1 AND NOT @src2 in @dst
 * @dst: destination, may be @src1 or @src2
 * @src1: operand to remove bits from
 * @src2: bits to remove, inverted over its own range
 *
 * See __cmask_op() for how the ranges combine. Return whether @dst has a bit
 * set afterwards.
 */
__weak bool cmask_andnot(struct scx_cmask __arena __arg_arena *dst,
			 const struct scx_cmask __arena __arg_arena *src1,
			 const struct scx_cmask __arena __arg_arena *src2)
{
	return __cmask_op(dst, src1, src2, BPF_CMASK_OP_ANDNOT);
}

/**
 * cmask_copy - Copy @src into @dst
 * @dst: destination
 * @src: source
 *
 * The single-source case of __cmask_op(): @dst is written over @src's range and
 * untouched elsewhere.
 */
__weak void cmask_copy(struct scx_cmask __arena __arg_arena *dst,
		       const struct scx_cmask __arena __arg_arena *src)
{
	__cmask_op(dst, src, src, BPF_CMASK_OP_COPY);
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
	if (a->nr_cids == 0)
		return true;
	nr_words = (a->base + a->nr_cids - 1) / 64 - a->base / 64 + 1;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		if (a->bits[i] != b->bits[i])
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

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
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
	     (cid) < (m)->base + (m)->nr_cids && can_loop;			\
	     (cid) = cmask_next_set((m), (cid) + 1))

/*
 * True iff every bit set in @a is also set in @b. Matches the kernel-side
 * scx_cmask_subset(): ranges don't need to nest, and set bits of @a outside
 * @b's range fail the test.
 */
static __always_inline bool cmask_subset(const struct scx_cmask __arena *a,
					 const struct scx_cmask __arena *b)
{
	u32 a_end = a->base + a->nr_cids;
	u32 b_end = b->base + b->nr_cids;
	u32 a_wbase = a->base / 64;
	u32 b_wbase = b->base / 64;
	u32 lo = a->base > b->base ? a->base : b->base;
	u32 hi = a_end < b_end ? a_end : b_end;
	u32 lo_word, hi_word, i;

	/* set bits of @a outside @b's range can't be in @b */
	if (a->base < b->base &&
	    cmask_next_set(a, a->base) < (b->base < a_end ? b->base : a_end))
		return false;
	if (a_end > b_end &&
	    cmask_next_set(a, a->base > b_end ? a->base : b_end) < a_end)
		return false;

	if (lo >= hi)
		return true;

	/*
	 * Walk the words the range intersection spans. Plain word tests
	 * suffice: the scans above guarantee @a has no set bit outside @b's
	 * range and padding bits are kept clear by all cmask helpers.
	 */
	lo_word = lo / 64;
	hi_word = (hi - 1) / 64;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		u32 w = lo_word + i;

		if (w > hi_word)
			break;
		if (a->bits[w - a_wbase] & ~b->bits[w - b_wbase])
			return false;
	}
	return true;
}

/*
 * Population count over [base, base + nr_cids). Padding bits in the head/tail
 * words are guaranteed zero by the mutating helpers, so a flat popcount over
 * the words the range spans is correct.
 */
static __always_inline u32 cmask_weight(const struct scx_cmask __arena *m)
{
	u32 nr_words, i;
	/* callers compare the sum, see bpf_arena_loop_zero */
	u32 count = bpf_arena_loop_zero;

	if (!m->nr_cids)
		return 0;
	nr_words = (m->base + m->nr_cids - 1) / 64 - m->base / 64 + 1;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		if (i >= nr_words)
			break;
		count += __builtin_popcountll(m->bits[i]);
	}
	return count;
}

/* true if @a and @b share any set bit, over the intersection of their ranges */
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

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
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

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
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

/**
 * cmask_end - One past the last cid of @m's active range
 * @m: cmask of interest
 *
 * The scan helpers return this value when no set cid is found.
 */
static __always_inline u32 cmask_end(const struct scx_cmask __arena *m)
{
	return m->base + m->nr_cids;
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

/*
 * The distribute helpers rotate through one slot per cid, a cacheline each so
 * picks on one CPU do not bounce the line of another. A missing allocation is a
 * setup bug and aborts the scheduler. On a CPU outside the scheduler's cid
 * space the helpers fall back to the first matching cid.
 *
 * TODO: The slots are one allocation without node placement. Move them to a
 * per-cid arena allocator once the library has one, for node-local slots
 * without the cacheline padding.
 */
struct cmask_rotor_slot {
	u32 cid;
} __attribute__((aligned(SCX_CACHELINE_SIZE)));

struct cmask_rotor_slot __arena *cmask_distribute_rotors __weak;

/**
 * cmask_distribute_init - Allocate the distribute rotor from @map
 * @map: the scheduler's arena map
 *
 * The shared arena init calls this once. A scheduler with its own arena calls
 * it before the first distribute pick. On a kernel without the cid kfuncs
 * nothing can pick, so the call allocates nothing and succeeds. Return 0 on
 * success, -ENOMEM if the allocation fails.
 */
static __always_inline int cmask_distribute_init(void *map)
{
	u64 size;
	u32 pages;

	/* no cid kfuncs means no picks and nothing to allocate */
	if (!bpf_ksym_exists(scx_bpf_nr_cids))
		return 0;

	size = scx_bpf_nr_cids() * sizeof(*cmask_distribute_rotors);
	pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	cmask_distribute_rotors = bpf_arena_alloc_pages(map, NULL, pages, NUMA_NO_NODE, 0);
	return cmask_distribute_rotors ? 0 : -ENOMEM;
}

static __always_inline u32 __arena *__cmask_distribute_rotor(void)
{
	s32 cid = scx_bpf_this_cid();

	if (unlikely(!cmask_distribute_rotors)) {
		scx_bpf_error("cmask distribute rotor not allocated");
		return NULL;
	}
	if (cid < 0)
		return NULL;
	return &cmask_distribute_rotors[cid].cid;
}

/**
 * cmask_any_distribute - Pick a set cid, spreading successive picks
 * @m: cmask to pick from
 *
 * Counterpart of bpf_cpumask_any_distribute(): a per-cid rotor makes successive
 * picks rotate through the set cids instead of repeating the first one. Returns
 * cmask_end(@m) if @m is empty.
 */
static __always_inline u32 cmask_any_distribute(const struct scx_cmask __arena *m)
{
	u32 __arena *rotor = __cmask_distribute_rotor();
	u32 pick;

	if (unlikely(!rotor))
		return cmask_first_set(m);

	pick = cmask_next_set_wrap(m, *rotor + 1);
	if (pick < cmask_end(m))
		*rotor = pick;
	return pick;
}

/**
 * cmask_any_and_distribute - Pick a cid set in both masks, spreading picks
 * @a: first cmask, the scan is bounded by its range
 * @b: second cmask
 *
 * Counterpart of bpf_cpumask_any_and_distribute(). Shares the rotor with
 * cmask_any_distribute() the same way the kernel counterparts share theirs.
 * Returns cmask_end(@a) if the intersection is empty.
 */
static __always_inline u32 cmask_any_and_distribute(const struct scx_cmask __arena *a,
						    const struct scx_cmask __arena *b)
{
	u32 __arena *rotor = __cmask_distribute_rotor();
	u32 pick;

	if (unlikely(!rotor))
		return cmask_next_and_set(a, b, a->base);

	pick = cmask_next_and_set_wrap(a, b, *rotor + 1);
	if (pick < cmask_end(a))
		*rotor = pick;
	return pick;
}

/**
 * cmask_distribute_rotor_pos - Last cid picked by the distribute helpers
 *
 * For callers that anchor scans of their own on the shared rotor. Returns 0
 * when nothing has been picked on this cid yet or the CPU is outside the cid
 * space.
 */
static __always_inline u32 cmask_distribute_rotor_pos(void)
{
	u32 __arena *rotor = __cmask_distribute_rotor();

	if (unlikely(!rotor))
		return 0;
	return *rotor;
}

/*
 * Like cmask_next_and_set() but over the intersection of THREE masks. Return
 * a->base + a->nr_cids if no cid is set in all three at or after @start.
 */
static __always_inline u32 cmask_next_and2_set(const struct scx_cmask __arena *a,
					       const struct scx_cmask __arena *b,
					       const struct scx_cmask __arena *c,
					       u32 start)
{
	u32 a_end = a->base + a->nr_cids;
	u32 b_end = b->base + b->nr_cids;
	u32 c_end = c->base + c->nr_cids;
	u32 a_wbase = a->base / 64;
	u32 b_wbase = b->base / 64;
	u32 c_wbase = c->base / 64;
	u32 lo = a->base > b->base ? a->base : b->base;
	u32 hi = a_end < b_end ? a_end : b_end;
	u32 last_wi, start_wi, start_bit, i;

	lo = lo > c->base ? lo : c->base;
	hi = hi < c_end ? hi : c_end;

	if (lo >= hi)
		return a_end;
	if (start < lo)
		start = lo;
	if (start >= hi)
		return a_end;

	last_wi = (hi - 1) / 64;
	start_wi = start / 64;
	start_bit = start & 63;

	bpf_arena_for(i, 0, CMASK_MAX_WORDS) {
		u32 abs_wi = start_wi + i;
		u64 word;
		u32 found;

		if (abs_wi > last_wi)
			break;

		word = a->bits[abs_wi - a_wbase] & b->bits[abs_wi - b_wbase] &
		       c->bits[abs_wi - c_wbase];
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
 * Round-robin variant of cmask_next_and2_set(): wrap to @a->base if the
 * three-way intersection has no cid in the forward half. Return a->base +
 * a->nr_cids if empty.
 */
static __always_inline u32 cmask_next_and2_set_wrap(const struct scx_cmask __arena *a,
						    const struct scx_cmask __arena *b,
						    const struct scx_cmask __arena *c,
						    u32 start)
{
	u32 a_end = a->base + a->nr_cids;
	u32 found;

	found = cmask_next_and2_set(a, b, c, start);
	if (found < a_end || start <= a->base)
		return found;

	found = cmask_next_and2_set(a, b, c, a->base);
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
	bpf_arena_for(cpu, 0, nr_cpu_ids) {
		s32 cid;

		if (!bpf_cpumask_test_cpu(cpu, cpumask))
			continue;
		cid = scx_bpf_cpu_to_cid(cpu);
		if (cid >= 0)
			__cmask_set(cid, m);
	}
}

#endif /* __SCX_CID_BPF_H */
