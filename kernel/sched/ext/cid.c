/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#include <linux/cacheinfo.h>

#include "internal.h"
#include "cid.h"

/*
 * cid tables.
 *
 * Pointers are published once on first enable and never revoked. The default
 * mapping is populated before ops.init() runs; scx_bpf_cid_override() commits
 * before it returns. As long as the BPF scheduler only uses the tables from
 * those points onward, it sees a consistent view.
 */
s16 *scx_cid_to_cpu_tbl;
s16 *scx_cpu_to_cid_tbl;
struct scx_cid_topo *scx_cid_topo;

#define SCX_CID_TOPO_NEG	(struct scx_cid_topo) {				\
	.core_cid = -1, .core_idx = -1, .llc_cid = -1, .llc_idx = -1,		\
	.node_cid = -1, .node_idx = -1,						\
}

/*
 * Return @cpu's LLC shared_cpu_map. If cacheinfo isn't populated (offline or
 * !present), record @cpu in @fallbacks and return its node mask instead - the
 * worst that can happen is that the cpu's LLC becomes coarser than reality.
 */
static const struct cpumask *cpu_llc_mask(int cpu, struct cpumask *fallbacks)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(cpu);

	if (!ci || !ci->info_list || !ci->num_leaves) {
		cpumask_set_cpu(cpu, fallbacks);
		return cpumask_of_node(cpu_to_node(cpu));
	}
	return &ci->info_list[ci->num_leaves - 1].shared_cpu_map;
}

/* Allocate the cid tables once on first enable; never freed. */
static s32 scx_cid_arrays_alloc(void)
{
	u32 npossible = num_possible_cpus();
	s16 *cid_to_cpu, *cpu_to_cid;
	struct scx_cid_topo *cid_topo;

	if (scx_cid_to_cpu_tbl)
		return 0;

	cid_to_cpu = kzalloc_objs(*scx_cid_to_cpu_tbl, npossible, GFP_KERNEL);
	cpu_to_cid = kzalloc_objs(*scx_cpu_to_cid_tbl, nr_cpu_ids, GFP_KERNEL);
	cid_topo = kmalloc_objs(*scx_cid_topo, npossible, GFP_KERNEL);

	if (!cid_to_cpu || !cpu_to_cid || !cid_topo) {
		kfree(cid_to_cpu);
		kfree(cpu_to_cid);
		kfree(cid_topo);
		return -ENOMEM;
	}

	WRITE_ONCE(scx_cid_to_cpu_tbl, cid_to_cpu);
	WRITE_ONCE(scx_cpu_to_cid_tbl, cpu_to_cid);
	WRITE_ONCE(scx_cid_topo, cid_topo);
	return 0;
}

/**
 * scx_cid_init - build the cid mapping
 * @sch: the scx_sched being initialized; used as the scx_error() target
 *
 * See "Topological CPU IDs" in cid.h for the model. Walk online cpus by
 * intersection at each level (parent_scratch & this_level_mask), which keeps
 * containment correct by construction and naturally splits a physical LLC
 * straddling two NUMA nodes into two LLC units. The caller must hold
 * cpus_read_lock.
 */
s32 scx_cid_init(struct scx_sched *sch)
{
	cpumask_var_t to_walk __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	cpumask_var_t node_scratch __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	cpumask_var_t llc_scratch __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	cpumask_var_t core_scratch __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	cpumask_var_t llc_fallback __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	cpumask_var_t online_no_topo __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	u32 next_cid = 0;
	s32 next_node_idx = 0, next_llc_idx = 0, next_core_idx = 0;
	s32 cpu, ret;

	/* CMASK_MAX_WORDS in cid.bpf.h covers NR_CPUS up to 8192 */
	BUILD_BUG_ON(NR_CPUS > 8192);

	lockdep_assert_cpus_held();

	ret = scx_cid_arrays_alloc();
	if (ret)
		return ret;

	if (!zalloc_cpumask_var(&to_walk, GFP_KERNEL) ||
	    !zalloc_cpumask_var(&node_scratch, GFP_KERNEL) ||
	    !zalloc_cpumask_var(&llc_scratch, GFP_KERNEL) ||
	    !zalloc_cpumask_var(&core_scratch, GFP_KERNEL) ||
	    !zalloc_cpumask_var(&llc_fallback, GFP_KERNEL) ||
	    !zalloc_cpumask_var(&online_no_topo, GFP_KERNEL))
		return -ENOMEM;

	/* -1 sentinels for sparse-possible cpu id holes (0 is a valid cid) */
	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		scx_cpu_to_cid_tbl[cpu] = -1;

	cpumask_copy(to_walk, cpu_online_mask);

	while (!cpumask_empty(to_walk)) {
		s32 next_cpu = cpumask_first(to_walk);
		s32 nid = cpu_to_node(next_cpu);
		s32 node_cid = next_cid;
		s32 node_idx;

		/*
		 * No NUMA info: skip and let the tail loop assign a no-topo
		 * cid. cpumask_of_node(-1) is undefined.
		 */
		if (nid < 0) {
			cpumask_clear_cpu(next_cpu, to_walk);
			continue;
		}

		node_idx = next_node_idx++;

		/* node_scratch = to_walk & this node */
		cpumask_and(node_scratch, to_walk, cpumask_of_node(nid));
		if (WARN_ON_ONCE(!cpumask_test_cpu(next_cpu, node_scratch)))
			return -EINVAL;

		while (!cpumask_empty(node_scratch)) {
			s32 ncpu = cpumask_first(node_scratch);
			const struct cpumask *llc_mask = cpu_llc_mask(ncpu, llc_fallback);
			s32 llc_cid = next_cid;
			s32 llc_idx = next_llc_idx++;

			/* llc_scratch = node_scratch & this llc */
			cpumask_and(llc_scratch, node_scratch, llc_mask);
			if (WARN_ON_ONCE(!cpumask_test_cpu(ncpu, llc_scratch)))
				return -EINVAL;

			while (!cpumask_empty(llc_scratch)) {
				s32 lcpu = cpumask_first(llc_scratch);
				const struct cpumask *sib = topology_sibling_cpumask(lcpu);
				s32 core_cid = next_cid;
				s32 core_idx = next_core_idx++;
				s32 ccpu;

				/* core_scratch = llc_scratch & this core */
				cpumask_and(core_scratch, llc_scratch, sib);
				if (WARN_ON_ONCE(!cpumask_test_cpu(lcpu, core_scratch)))
					return -EINVAL;

				for_each_cpu(ccpu, core_scratch) {
					s32 cid = next_cid++;

					scx_cid_to_cpu_tbl[cid] = ccpu;
					scx_cpu_to_cid_tbl[ccpu] = cid;
					scx_cid_topo[cid] = (struct scx_cid_topo){
						.core_cid = core_cid,
						.core_idx = core_idx,
						.llc_cid = llc_cid,
						.llc_idx = llc_idx,
						.node_cid = node_cid,
						.node_idx = node_idx,
					};

					cpumask_clear_cpu(ccpu, llc_scratch);
					cpumask_clear_cpu(ccpu, node_scratch);
					cpumask_clear_cpu(ccpu, to_walk);
				}
			}
		}
	}

	/*
	 * No-topo section: any possible cpu without a cid - normally just the
	 * not-online ones. Collect any currently-online cpus that land here in
	 * @online_no_topo so we can warn about them at the end.
	 */
	for_each_cpu(cpu, cpu_possible_mask) {
		s32 cid;

		if (__scx_cpu_to_cid(cpu) != -1)
			continue;
		if (cpu_online(cpu))
			cpumask_set_cpu(cpu, online_no_topo);

		cid = next_cid++;
		scx_cid_to_cpu_tbl[cid] = cpu;
		scx_cpu_to_cid_tbl[cpu] = cid;
		scx_cid_topo[cid] = SCX_CID_TOPO_NEG;
	}

	if (!cpumask_empty(llc_fallback))
		pr_warn("scx_cid: cpus without cacheinfo, using node mask as llc: %*pbl\n",
			cpumask_pr_args(llc_fallback));
	if (!cpumask_empty(online_no_topo))
		pr_warn("scx_cid: online cpus with no usable topology: %*pbl\n",
			cpumask_pr_args(online_no_topo));

	return 0;
}

/**
 * scx_cmask_clear - Zero every bit in @m's active range
 * @m: cmask to clear
 *
 * Storage past the active range is left as is.
 */
void scx_cmask_clear(struct scx_cmask *m)
{
	u32 nr_words;

	if (!m->nr_cids)
		return;
	nr_words = (m->base + m->nr_cids - 1) / 64 - m->base / 64 + 1;
	memset(m->bits, 0, nr_words * sizeof(u64));
}

/**
 * scx_cmask_fill - Set every bit in @m's active range
 * @m: cmask to fill
 *
 * Counterpart to scx_cmask_clear(). Storage past the active range is left as is.
 */
void scx_cmask_fill(struct scx_cmask *m)
{
	u32 nr_words, head_bits, tail_bits;

	if (!m->nr_cids)
		return;
	nr_words = (m->base + m->nr_cids - 1) / 64 - m->base / 64 + 1;
	memset(m->bits, 0xff, nr_words * sizeof(u64));

	/* clear word-0 bits below base */
	head_bits = m->base & 63;
	if (head_bits)
		m->bits[0] &= ~((1ULL << head_bits) - 1);

	/* clear last-word bits at or past base + nr_cids */
	tail_bits = (m->base + m->nr_cids) & 63;
	if (tail_bits)
		m->bits[nr_words - 1] &= (1ULL << tail_bits) - 1;
}

/**
 * scx_cpumask_to_cmask - Translate a kernel cpumask into a cmask
 * @src: source cpumask
 * @dst: cmask to write
 *
 * Clear @dst's active range and set the bit for each cid whose cpu is in
 * @src and lies within that range. Out-of-range cids are silently ignored.
 */
void scx_cpumask_to_cmask(const struct cpumask *src, struct scx_cmask *dst)
{
	s32 cpu;

	scx_cmask_clear(dst);
	for_each_cpu(cpu, src) {
		s32 cid = __scx_cpu_to_cid(cpu);

		if (cid >= 0)
			__scx_cmask_set(cid, dst);
	}
}

__bpf_kfunc_start_defs();

/**
 * scx_bpf_cid_override - Install an explicit cpu->cid mapping
 * @cpu_to_cid: array of nr_cpu_ids s32 entries (cid for each cpu)
 * @cpu_to_cid__sz: must be nr_cpu_ids * sizeof(s32) bytes
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * May only be called from ops.init() of the root scheduler. Replace the
 * topology-probed cid mapping with the caller-provided one. Each possible cpu
 * must map to a unique cid in [0, num_possible_cpus()). Topo info is cleared.
 * On invalid input, trigger scx_error() to abort the scheduler.
 */
__bpf_kfunc void scx_bpf_cid_override(const s32 *cpu_to_cid, u32 cpu_to_cid__sz,
				      const struct bpf_prog_aux *aux)
{
	cpumask_var_t seen __free(free_cpumask_var) = CPUMASK_VAR_NULL;
	struct scx_sched *sch;
	bool alloced;
	s32 cpu, cid;

	/* GFP_KERNEL alloc must happen before the rcu read section */
	alloced = zalloc_cpumask_var(&seen, GFP_KERNEL);

	guard(rcu)();

	sch = scx_prog_sched(aux);
	if (unlikely(!sch))
		return;

	if (!alloced) {
		scx_error(sch, "scx_bpf_cid_override: failed to allocate cpumask");
		return;
	}

	if (scx_parent(sch)) {
		scx_error(sch, "scx_bpf_cid_override() only allowed from root sched");
		return;
	}

	if (cpu_to_cid__sz != nr_cpu_ids * sizeof(s32)) {
		scx_error(sch, "scx_bpf_cid_override: expected %zu bytes, got %u",
			  nr_cpu_ids * sizeof(s32), cpu_to_cid__sz);
		return;
	}

	for_each_possible_cpu(cpu) {
		s32 c = cpu_to_cid[cpu];

		if (!cid_valid(sch, c))
			return;
		if (cpumask_test_and_set_cpu(c, seen)) {
			scx_error(sch, "cid %d assigned to multiple cpus", c);
			return;
		}
		scx_cpu_to_cid_tbl[cpu] = c;
		scx_cid_to_cpu_tbl[c] = cpu;
	}

	/* Invalidate stale topo info - the override carries no topology. */
	for (cid = 0; cid < num_possible_cpus(); cid++)
		scx_cid_topo[cid] = SCX_CID_TOPO_NEG;
}

/**
 * scx_bpf_cid_to_cpu - Return the raw CPU id for @cid
 * @cid: cid to look up
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * Return the raw CPU id for @cid. Trigger scx_error() and return -EINVAL if
 * @cid is invalid. The cid<->cpu mapping is static for the lifetime of the
 * loaded scheduler, so the BPF side can cache the result to avoid repeated
 * kfunc invocations.
 */
__bpf_kfunc s32 scx_bpf_cid_to_cpu(s32 cid, const struct bpf_prog_aux *aux)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = scx_prog_sched(aux);
	if (unlikely(!sch))
		return -EINVAL;
	return scx_cid_to_cpu(sch, cid);
}

/**
 * scx_bpf_cpu_to_cid - Return the cid for @cpu
 * @cpu: cpu to look up
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * Return the cid for @cpu. Trigger scx_error() and return -EINVAL if @cpu is
 * invalid. The cid<->cpu mapping is static for the lifetime of the loaded
 * scheduler, so the BPF side can cache the result to avoid repeated kfunc
 * invocations.
 */
__bpf_kfunc s32 scx_bpf_cpu_to_cid(s32 cpu, const struct bpf_prog_aux *aux)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = scx_prog_sched(aux);
	if (unlikely(!sch))
		return -EINVAL;
	return scx_cpu_to_cid(sch, cpu);
}

/*
 * Set ops on cmasks. cmask_walk_op2() shares one walk across mutating
 * (and/or/copy/andnot) and predicate (subset/intersects) two-cmask forms;
 * cmask_walk_op1() does the same shape over a single cmask range. Every public
 * entry passes a compile-time-constant @op; cmask_walk_op{1,2}() and
 * cmask_word_op{1,2}() are __always_inline so the inner switch collapses to the
 * selected op and cmask_op2_is_pred() folds the predicate early-exit out of
 * mutating ops.
 *
 * Two-cmask ops only touch @dst bits inside the intersection of the two ranges;
 * bits outside stay untouched. In particular, scx_cmask_copy() does NOT zero
 * @dst bits that lie outside @src's range.
 *
 * The _RACY variants are otherwise identical to their non-racy counterpart but
 * read @src word-by-word via data_race(). Memory ordering with concurrent
 * writers is the caller's responsibility.
 */
enum cmask_op2 {
	/* mutating */
	CMASK_OP2_AND,
	CMASK_OP2_OR,
	CMASK_OP2_OR_RACY,
	CMASK_OP2_COPY,
	CMASK_OP2_COPY_RACY,
	CMASK_OP2_ANDNOT,
	/* predicates - short-circuit when the per-word result is true */
	CMASK_OP2_SUBSET,
	CMASK_OP2_INTERSECTS,
};

static __always_inline bool cmask_op2_is_pred(const enum cmask_op2 op)
{
	return op == CMASK_OP2_SUBSET || op == CMASK_OP2_INTERSECTS;
}

static __always_inline bool cmask_word_op2(u64 *av, const u64 *bp, u64 mask,
					   const enum cmask_op2 op)
{
	switch (op) {
	case CMASK_OP2_AND:
		*av &= ~mask | *bp;
		return false;
	case CMASK_OP2_OR:
		*av |= *bp & mask;
		return false;
	case CMASK_OP2_OR_RACY:
		*av |= data_race(*bp) & mask;
		return false;
	case CMASK_OP2_COPY:
		*av = (*av & ~mask) | (*bp & mask);
		return false;
	case CMASK_OP2_COPY_RACY:
		*av = (*av & ~mask) | (data_race(*bp) & mask);
		return false;
	case CMASK_OP2_ANDNOT:
		*av &= ~(*bp & mask);
		return false;
	case CMASK_OP2_SUBSET:
		/* stop on the first bit in @sub not set in @super */
		return (*bp & ~*av) & mask;
	case CMASK_OP2_INTERSECTS:
		return (*av & *bp) & mask;
	}
	unreachable();
}

/*
 * Walk the intersection of [@a_base, @a_base + @a_nr_cids) with [@b_base,
 * @b_base + @b_nr_cids) word by word, applying @op. Mutating ops walk all words
 * and return false; predicates return true on the first word whose per-word
 * test is true. Empty intersection returns false (matches "no bits to consider"
 * for both mutate and predicate).
 *
 * Base/nr_cids are taken as parameters so callers with snapshotted bounds can
 * drive the walk with values independent of the cmask's header.
 */
static __always_inline bool cmask_walk_op2(u64 *a_bits, u32 a_base, u32 a_nr_cids,
					   const u64 *b_bits, u32 b_base, u32 b_nr_cids,
					   const enum cmask_op2 op)
{
	u32 lo = max(a_base, b_base);
	u32 hi = min(a_base + a_nr_cids, b_base + b_nr_cids);
	u32 a_word_off = a_base / 64;
	u32 b_word_off = b_base / 64;
	u32 lo_word = lo / 64;
	u32 hi_word = (hi - 1) / 64;
	u64 head_mask = GENMASK_U64(63, lo & 63);
	u64 tail_mask = GENMASK_U64((hi - 1) & 63, 0);
	u32 w;

	if (lo >= hi)
		return false;

	if (lo_word == hi_word)
		return cmask_word_op2(&a_bits[lo_word - a_word_off],
				      &b_bits[lo_word - b_word_off],
				      head_mask & tail_mask, op);

	if (cmask_word_op2(&a_bits[lo_word - a_word_off],
			   &b_bits[lo_word - b_word_off], head_mask, op) &&
	    cmask_op2_is_pred(op))
		return true;

	for (w = lo_word + 1; w < hi_word; w++)
		if (cmask_word_op2(&a_bits[w - a_word_off],
				   &b_bits[w - b_word_off], ~0ULL, op) &&
		    cmask_op2_is_pred(op))
			return true;

	return cmask_word_op2(&a_bits[hi_word - a_word_off],
			      &b_bits[hi_word - b_word_off], tail_mask, op);
}

enum cmask_op1 {
	CMASK_OP1_ANY_SET,
};

static __always_inline bool cmask_word_op1(const u64 *ap, u64 mask,
					   const enum cmask_op1 op)
{
	switch (op) {
	case CMASK_OP1_ANY_SET:
		return *ap & mask;
	}
	unreachable();
}

/*
 * Walk [@a_base, @a_base + @a_nr_cids) of @a_bits word by word, applying @op.
 * Returns true on the first word whose per-word test is true; returns false if
 * no word matches or the range is empty. All current op1s short-circuit on
 * per-word true; if a non-predicate op1 lands here, add a cmask_op1_is_pred()
 * guard analogous to cmask_op2_is_pred().
 */
static __always_inline bool cmask_walk_op1(const u64 *a_bits, u32 a_base,
					   u32 a_nr_cids,
					   const enum cmask_op1 op)
{
	u32 lo = a_base;
	u32 hi = a_base + a_nr_cids;
	u32 a_word_off = a_base / 64;
	u32 lo_word = lo / 64;
	u32 hi_word = (hi - 1) / 64;
	u64 head_mask = GENMASK_U64(63, lo & 63);
	u64 tail_mask = GENMASK_U64((hi - 1) & 63, 0);
	u32 w;

	if (lo >= hi)
		return false;

	if (lo_word == hi_word)
		return cmask_word_op1(&a_bits[lo_word - a_word_off],
				      head_mask & tail_mask, op);

	if (cmask_word_op1(&a_bits[lo_word - a_word_off], head_mask, op))
		return true;
	for (w = lo_word + 1; w < hi_word; w++)
		if (cmask_word_op1(&a_bits[w - a_word_off], ~0ULL, op))
			return true;
	return cmask_word_op1(&a_bits[hi_word - a_word_off], tail_mask, op);
}

void scx_cmask_and(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_AND);
}

void scx_cmask_or(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_OR);
}

/**
 * scx_cmask_or_racy - OR @src into @dst, reading @src without locking
 *
 * @src is read word-by-word through data_race(). Same per-bit independence
 * rationale as scx_cmask_copy_racy(). Memory ordering with writers is the
 * caller's responsibility.
 */
void scx_cmask_or_racy(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_OR_RACY);
}

void scx_cmask_copy(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_COPY);
}

/**
 * scx_cmask_copy_racy - Snapshot @src into @dst without locking
 *
 * @src is read word-by-word through data_race(). Head/tail masking matches
 * scx_cmask_copy(). Each bit in a cmask is independent, so partial updates
 * just leave some bits fresher than others. Memory ordering with writers is
 * the caller's responsibility.
 */
void scx_cmask_copy_racy(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_COPY_RACY);
}

void scx_cmask_andnot(struct scx_cmask *dst, const struct scx_cmask *src)
{
	cmask_walk_op2(dst->bits, dst->base, dst->nr_cids,
		       src->bits, src->base, src->nr_cids, CMASK_OP2_ANDNOT);
}

/*
 * Return true if @cm has any bit set in [@lo, @hi). Caller must ensure
 * [@lo, @hi) is contained in @cm's range.
 */
static bool cmask_any_set_in_range(const struct scx_cmask *cm, u32 lo, u32 hi)
{
	if (lo >= hi)
		return false;
	return cmask_walk_op1(&cm->bits[lo / 64 - cm->base / 64], lo, hi - lo,
			      CMASK_OP1_ANY_SET);
}

/**
 * scx_cmask_subset - test whether @sub is a subset of @super
 * @sub: cmask to test
 * @super: cmask to test against
 *
 * Return true iff every set bit of @sub is also set in @super.
 */
bool scx_cmask_subset(const struct scx_cmask *sub, const struct scx_cmask *super)
{
	u32 super_end = super->base + super->nr_cids;
	u32 sub_end = sub->base + sub->nr_cids;

	/*
	 * Set bits in @sub outside @super's range can't be in @super, so any
	 * such bit means not a subset. The walk below only visits words
	 * common to both ranges, so these need a separate scan.
	 */
	if (sub->base < super->base &&
	    cmask_any_set_in_range(sub, sub->base, min(super->base, sub_end)))
		return false;
	if (sub_end > super_end &&
	    cmask_any_set_in_range(sub, max(sub->base, super_end), sub_end))
		return false;

	return !cmask_walk_op2((u64 *)super->bits, super->base, super->nr_cids,
			       sub->bits, sub->base, sub->nr_cids, CMASK_OP2_SUBSET);
}

bool scx_cmask_intersects(const struct scx_cmask *a, const struct scx_cmask *b)
{
	return cmask_walk_op2((u64 *)a->bits, a->base, a->nr_cids,
			      b->bits, b->base, b->nr_cids, CMASK_OP2_INTERSECTS);
}

/**
 * scx_cmask_empty - Test whether @m has no bits set
 * @m: cmask to test
 *
 * Return true iff @m's active range has no bits set.
 */
bool scx_cmask_empty(const struct scx_cmask *m)
{
	return !cmask_any_set_in_range(m, m->base, m->base + m->nr_cids);
}

/**
 * scx_bpf_cid_topo - Copy out per-cid topology info
 * @cid: cid to look up
 * @out__uninit: where to copy the topology info; fully written by this call
 * @aux: implicit BPF argument to access bpf_prog_aux hidden from BPF progs
 *
 * Fill @out__uninit with the topology info for @cid. Trigger scx_error() if
 * @cid is out of range. If @cid is valid but in the no-topo section, all fields
 * are set to -1.
 */
__bpf_kfunc void scx_bpf_cid_topo(s32 cid, struct scx_cid_topo *out__uninit,
				  const struct bpf_prog_aux *aux)
{
	struct scx_sched *sch;

	guard(rcu)();

	sch = scx_prog_sched(aux);
	if (unlikely(!sch) || !cid_valid(sch, cid)) {
		*out__uninit = SCX_CID_TOPO_NEG;
		return;
	}

	*out__uninit = READ_ONCE(scx_cid_topo)[cid];
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(scx_kfunc_ids_init)
BTF_ID_FLAGS(func, scx_bpf_cid_override, KF_IMPLICIT_ARGS | KF_SLEEPABLE)
BTF_KFUNCS_END(scx_kfunc_ids_init)

static const struct btf_kfunc_id_set scx_kfunc_set_init = {
	.owner	= THIS_MODULE,
	.set	= &scx_kfunc_ids_init,
	.filter	= scx_kfunc_context_filter,
};

BTF_KFUNCS_START(scx_kfunc_ids_cid)
BTF_ID_FLAGS(func, scx_bpf_cid_to_cpu, KF_IMPLICIT_ARGS)
BTF_ID_FLAGS(func, scx_bpf_cpu_to_cid, KF_IMPLICIT_ARGS)
BTF_ID_FLAGS(func, scx_bpf_cid_topo, KF_IMPLICIT_ARGS)
BTF_KFUNCS_END(scx_kfunc_ids_cid)

static const struct btf_kfunc_id_set scx_kfunc_set_cid = {
	.owner	= THIS_MODULE,
	.set	= &scx_kfunc_ids_cid,
};

int scx_cid_kfunc_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &scx_kfunc_set_init) ?:
		register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &scx_kfunc_set_cid) ?:
		register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &scx_kfunc_set_cid) ?:
		register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &scx_kfunc_set_cid);
}
