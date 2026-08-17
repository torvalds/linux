/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Topological CPU IDs (cids)
 * --------------------------
 *
 * Raw cpu numbers are clumsy for sharding work and communication across
 * topology units, especially from BPF: the space can be sparse, numerical
 * closeness doesn't imply topological closeness (x86 hyperthreading often puts
 * SMT siblings far apart), and a range of cpu ids doesn't mean anything.
 * Sub-scheds make this acute - cpu allocation, revocation and other state are
 * constantly communicated across sub-scheds, and passing whole cpumasks scales
 * poorly with cpu count. cpumasks are also awkward in BPF: a variable-length
 * kernel type sized for the maximum NR_CPUS (4k), with verbose helper sequences
 * for every op.
 *
 * cids give every cpu a dense, topology-ordered id. CPUs sharing a core, LLC or
 * NUMA node get contiguous cid ranges, so a topology unit becomes a (start,
 * length) slice of cid space. Communication can pass a slice instead of a
 * cpumask, and BPF code can process, for example, a u64 word's worth of cids at
 * a time.
 *
 * The mapping is built once at root scheduler enable time by walking the
 * topology of online cpus only. Going by online cpus is out of necessity:
 * depending on the arch, topology info isn't reliably available for offline
 * cpus. The expected usage model is restarting the scheduler on hotplug events
 * so the mapping is rebuilt against the new online set. A scheduler that wants
 * to handle hotplug without a restart can provide its own cid and shard mapping
 * through the override interface.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef _KERNEL_SCHED_EXT_CID_H
#define _KERNEL_SCHED_EXT_CID_H

#include "internal.h"

struct scx_sched;

/*
 * Cid space (total is always num_possible_cpus()) is laid out with
 * topology-annotated cids first, then no-topo cids at the tail. The
 * topology-annotated block covers the cpus that were online when scx_cid_init()
 * ran and remains valid even after those cpus go offline. The tail block covers
 * possible-but-not-online cpus and carries all-(-1) topo info (see
 * scx_cid_topo); callers detect it via the -1 sentinels.
 *
 * See the comment above the table definitions in cid.c for the
 * memory-ordering and visibility contract.
 */
extern s16 *scx_cid_to_cpu_tbl;
extern s16 *scx_cpu_to_cid_tbl;
extern struct scx_cid_topo *scx_cid_topo;
extern struct btf_id_set8 scx_kfunc_ids_init;

void scx_cmask_clear(struct scx_cmask *m);
void scx_cmask_fill(struct scx_cmask *m);
void scx_cmask_and(struct scx_cmask *dst, const struct scx_cmask *src);
void scx_cmask_or(struct scx_cmask *dst, const struct scx_cmask *src);
void scx_cmask_or_racy(struct scx_cmask *dst, const struct scx_cmask *src);
void scx_cmask_copy(struct scx_cmask *dst, const struct scx_cmask *src);
void scx_cmask_copy_racy(struct scx_cmask *dst, const struct scx_cmask *src);
void scx_cmask_andnot(struct scx_cmask *dst, const struct scx_cmask *src);
bool scx_cmask_subset(const struct scx_cmask *sub, const struct scx_cmask *super);
bool scx_cmask_intersects(const struct scx_cmask *a, const struct scx_cmask *b);
bool scx_cmask_empty(const struct scx_cmask *m);
s32 scx_cid_init(struct scx_sched *sch);
int scx_cid_kfunc_init(void);
void scx_cpumask_to_cmask(const struct cpumask *src, struct scx_cmask *dst);

/**
 * cid_valid - Verify a cid value, to be used on ops input args
 * @sch: scx_sched to abort on error
 * @cid: cid which came from a BPF ops
 *
 * Return true if @cid is in [0, num_possible_cpus()). On failure, trigger
 * scx_error() and return false.
 */
static inline bool cid_valid(struct scx_sched *sch, s32 cid)
{
	if (likely(cid >= 0 && cid < num_possible_cpus()))
		return true;
	scx_error(sch, "invalid cid %d", cid);
	return false;
}

/**
 * __scx_cid_to_cpu - Unchecked cid->cpu table lookup
 * @cid: cid to look up. Must be in [0, num_possible_cpus()).
 *
 * Intended for callsites that have already validated @cid and that hold a
 * non-NULL @sch from scx_prog_sched() - a live sched implies the table has
 * been allocated, so no NULL check is needed here.
 */
static inline s32 __scx_cid_to_cpu(s32 cid)
{
	/* READ_ONCE pairs with WRITE_ONCE in scx_cid_arrays_alloc() */
	return READ_ONCE(scx_cid_to_cpu_tbl)[cid];
}

/**
 * __scx_cpu_to_cid - Unchecked cpu->cid table lookup
 * @cpu: cpu to look up. Must be a valid possible cpu id.
 *
 * Same usage constraints as __scx_cid_to_cpu().
 */
static inline s32 __scx_cpu_to_cid(s32 cpu)
{
	return READ_ONCE(scx_cpu_to_cid_tbl)[cpu];
}

/**
 * scx_cid_to_cpu - Translate @cid to its cpu
 * @sch: scx_sched for error reporting
 * @cid: cid to look up
 *
 * Return the cpu for @cid or a negative errno on failure. Invalid cid triggers
 * scx_error() on @sch. The cid arrays are allocated on first scheduler enable
 * and never freed, so the returned cpu is stable for the lifetime of the loaded
 * scheduler.
 */
static inline s32 scx_cid_to_cpu(struct scx_sched *sch, s32 cid)
{
	if (!cid_valid(sch, cid))
		return -EINVAL;
	return __scx_cid_to_cpu(cid);
}

/**
 * scx_cpu_to_cid - Translate @cpu to its cid
 * @sch: scx_sched for error reporting
 * @cpu: cpu to look up
 *
 * Return the cid for @cpu or a negative errno on failure. Invalid cpu triggers
 * scx_error() on @sch. Same lifetime guarantee as scx_cid_to_cpu().
 */
static inline s32 scx_cpu_to_cid(struct scx_sched *sch, s32 cpu)
{
	if (!scx_cpu_valid(sch, cpu, NULL))
		return -EINVAL;
	return __scx_cpu_to_cid(cpu);
}

/**
 * scx_is_cid_type - Test whether the active scheduler hierarchy is cid-form
 */
static inline bool scx_is_cid_type(void)
{
	return static_branch_unlikely(&__scx_is_cid_type);
}

static inline bool __scx_cmask_contains(u32 cid, const struct scx_cmask *m)
{
	return likely(cid >= m->base && cid < m->base + m->nr_cids);
}

/* Word in bits[] covering @cid. @cid must satisfy __scx_cmask_contains(). */
static inline u64 *__scx_cmask_word(u32 cid, const struct scx_cmask *m)
{
	return (u64 *)&m->bits[cid / 64 - m->base / 64];
}

/**
 * __scx_cmask_init - Initialize @m with explicit storage capacity
 * @m: cmask to initialize
 * @base: first cid of the active range
 * @nr_cids: number of cids in the active range
 * @alloc_cids: storage capacity in cids, at least @nr_cids
 *
 * Use when storage is sized larger than the initial active range. All of
 * bits[] is zeroed.
 */
static inline void __scx_cmask_init(struct scx_cmask *m, u32 base, u32 nr_cids,
				    u32 alloc_cids)
{
	if (WARN_ON_ONCE(alloc_cids < nr_cids))
		nr_cids = alloc_cids;

	m->base = base;
	m->nr_cids = nr_cids;
	m->alloc_words = SCX_CMASK_NR_WORDS(alloc_cids);
	memset(m->bits, 0, m->alloc_words * sizeof(u64));
}

/**
 * scx_cmask_init - Initialize @m on tight storage
 * @m: cmask to initialize
 * @base: first cid of the active range
 * @nr_cids: number of cids in the active range
 *
 * All of bits[] is zeroed.
 */
static inline void scx_cmask_init(struct scx_cmask *m, u32 base, u32 nr_cids)
{
	__scx_cmask_init(m, base, nr_cids, nr_cids);
}

/**
 * scx_cmask_reframe - Reshape @m's active range without resizing storage
 * @m: cmask to reframe
 * @base: new active range base
 * @nr_cids: new active range length, must fit within @m->alloc_words
 *
 * Body bits within the new range become garbage - only the head and tail
 * words are zeroed to keep the padding invariant.
 */
static inline void scx_cmask_reframe(struct scx_cmask *m, u32 base, u32 nr_cids)
{
	if (WARN_ON_ONCE(SCX_CMASK_NR_WORDS(nr_cids) > m->alloc_words))
		return;

	if (nr_cids) {
		u32 last_word = ((base & 63) + nr_cids - 1) / 64;

		m->bits[0] = 0;
		m->bits[last_word] = 0;
	}

	m->base = base;
	m->nr_cids = nr_cids;
}

static inline void __scx_cmask_set(u32 cid, struct scx_cmask *m)
{
	if (!__scx_cmask_contains(cid, m))
		return;
	*__scx_cmask_word(cid, m) |= BIT_U64(cid & 63);
}

/**
 * scx_cmask_test - test whether @cid is set in @m
 * @cid: cid to test
 * @m: cmask to test
 *
 * Return %false if @cid is outside @m's active range. Otherwise return the
 * bit's value. Read via READ_ONCE so callers can race set/clear writers.
 */
static inline bool scx_cmask_test(u32 cid, const struct scx_cmask *m)
{
	if (!__scx_cmask_contains(cid, m))
		return false;
	return READ_ONCE(*__scx_cmask_word(cid, m)) & BIT_U64(cid & 63);
}

/*
 * Words of bits[] the active range spans, 0 if empty. Tighter than the storage
 * SCX_CMASK_NR_WORDS() sizes for the worst-case base alignment.
 */
static inline u32 scx_cmask_nr_used_words(const struct scx_cmask *m)
{
	if (!m->nr_cids)
		return 0;
	return ((m->base & 63) + m->nr_cids - 1) / 64 + 1;
}

/**
 * scx_cmask_for_each_cid - iterate set cids in @m
 * @cid: s32 loop var that receives each set cid in turn
 * @m: cmask to iterate
 *
 * Visits set bits within @m's active range in ascending order. Scans only the
 * words the active range spans, where head and tail padding is kept zero, so
 * no per-cid range check is needed.
 */
#define scx_cmask_for_each_cid(cid, m)						\
	for (u64 __bs = (m)->base & ~63u, __wi = 0,				\
		     __nw = scx_cmask_nr_used_words(m);				\
	     __wi < __nw; __wi++)						\
		for (u64 __w = READ_ONCE((m)->bits[__wi]);			\
		     __w && ((cid) = __bs + __wi * 64 + __ffs64(__w), true);	\
		     __w &= __w - 1)

/*
 * scx_cpu_arg() wraps a cpu arg being handed to an SCX op. For cid-form
 * schedulers it resolves to the matching cid; for cpu-form it passes @cpu
 * through. scx_cpu_ret() is the inverse for a cpu/cid returned from an op
 * (currently only ops.select_cpu); it validates the BPF-supplied cid and
 * triggers scx_error() on @sch if invalid.
 */
static inline s32 scx_cpu_arg(s32 cpu)
{
	if (scx_is_cid_type())
		return __scx_cpu_to_cid(cpu);
	return cpu;
}

static inline s32 scx_cpu_ret(struct scx_sched *sch, s32 cpu_or_cid)
{
	if (cpu_or_cid < 0 || !scx_is_cid_type())
		return cpu_or_cid;
	return scx_cid_to_cpu(sch, cpu_or_cid);
}

#endif /* _KERNEL_SCHED_EXT_CID_H */
