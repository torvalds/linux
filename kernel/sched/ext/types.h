/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Early sched_ext type definitions.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef _KERNEL_SCHED_EXT_TYPES_H
#define _KERNEL_SCHED_EXT_TYPES_H

#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/overflow.h>
#include <linux/time64.h>
#include <linux/sched/topology.h>

enum scx_consts {
	SCX_DSP_DFL_MAX_BATCH		= 32,
	SCX_DSP_MAX_LOOPS		= 32,
	SCX_WATCHDOG_MAX_TIMEOUT	= 30 * HZ,

	/* per-CPU chunk size for p->scx.tid allocation, see scx_alloc_tid() */
	SCX_TID_CHUNK			= 1024,

	SCX_EXIT_BT_LEN			= 64,
	SCX_EXIT_MSG_LEN		= 1024,
	SCX_EXIT_DUMP_DFL_LEN		= 32768,

	SCX_CPUPERF_ONE			= SCHED_CAPACITY_SCALE,

	/*
	 * Iterating all tasks may take a while. Periodically drop
	 * scx_tasks_lock to avoid causing e.g. CSD and RCU stalls.
	 */
	SCX_TASK_ITER_BATCH		= 32,

	SCX_BYPASS_HOST_NTH		= 2,

	SCX_BYPASS_LB_DFL_INTV_US	= 500 * USEC_PER_MSEC,
	SCX_BYPASS_LB_DONOR_PCT		= 125,
	SCX_BYPASS_LB_MIN_DELTA_DIV	= 4,
	SCX_BYPASS_LB_BATCH		= 256,

	SCX_REENQ_MAX_REPEAT		= 256,

	SCX_SUB_MAX_DEPTH		= 4,
};

/*
 * Per-cid topology info. For each topology level (core, LLC, node) and shard,
 * records the first cid in the unit and its global index. Global indices are
 * consecutive integers assigned in cid-walk order, so e.g. core_idx ranges over
 * [0, nr_cores_at_init) with no gaps. No-topo cids have core/LLC/node fields
 * set to -1 but always have valid shard assignments.
 *
 * Shards are contiguous CID ranges used as scalable locking/work domains for
 * sub-scheduler operations. By default each LLC becomes one shard, split into
 * smaller shards if the LLC exceeds the target size. No-topo cids are packed
 * into their own max-sized shards.
 *
 * @core_cid: first cid of this cid's core (smt-sibling group)
 * @core_idx: global index of that core, in [0, nr_cores_at_init)
 * @llc_cid: first cid of this cid's LLC
 * @llc_idx: global index of that LLC, in [0, nr_llcs_at_init)
 * @node_cid: first cid of this cid's NUMA node
 * @node_idx: global index of that node, in [0, nr_nodes_at_init)
 * @shard_cid: first cid of this cid's shard
 * @shard_idx: global index of that shard, in [0, scx_nr_cid_shards)
 */
struct scx_cid_topo {
	s32 core_cid;
	s32 core_idx;
	s32 llc_cid;
	s32 llc_idx;
	s32 node_cid;
	s32 node_idx;
	s32 shard_cid;
	s32 shard_idx;
};

enum scx_cid_consts {
	SCX_CID_SHARD_SIZE_DFL		= 24,
	SCX_CID_SHARD_MAX_CPUS		= 512,
};

/*
 * Per-shard metadata for O(1) shard->cid-range lookup.
 *
 * @base_cid: first cid of the shard
 * @nr_cids: number of cids in the shard
 */
struct scx_cid_shard {
	s32			base_cid;
	s32			nr_cids;
};

/*
 * cmask: variable-length, base-windowed bitmap over cid space
 * -----------------------------------------------------------
 *
 * A cmask covers the cid range [base, base + nr_cids). bits[] is aligned to the
 * global 64-cid grid: bits[0] spans [base & ~63, (base & ~63) + 64), so the
 * first (base & 63) bits of bits[0] are head padding and the trailing bits of
 * the last active word past base + nr_cids are tail padding. Both stay zero;
 * all mutating helpers preserve that. Words past the last active word are not
 * read by any helper and have no constraint.
 *
 * Grid alignment means two cmasks always address bits[] against the same global
 * 64-cid windows, so cross-cmask word ops (AND, OR, ...) reduce to
 *
 *	dst->bits[i] OP= src->bits[i - delta]
 *
 * with no bit-shifting, regardless of how the two bases relate mod 64.
 */
struct scx_cmask {
	u32 base;
	u32 nr_cids;
	u32 alloc_words;
	u64 bits[];
};

/*
 * Number of u64 words of bits[] storage that covers @nr_cids regardless of base
 * alignment. The +1 absorbs up to 63 bits of head padding when base is not
 * 64-aligned - always allocating one extra word beats branching on base or
 * splitting the compute. The u64 cast keeps the +63 from wrapping when @nr_cids
 * is near U32_MAX, so callers bounds-checking the result against @alloc_words
 * catch the overflow instead of seeing a small value.
 */
#define SCX_CMASK_NR_WORDS(nr_cids)	((u32)(((u64)(nr_cids) + 63) / 64 + 1))

/**
 * __SCX_CMASK_DEFINE - Define an on-stack cmask with explicit storage capacity
 * @NAME: variable name to define
 * @BASE: first cid of the active range
 * @NR_CIDS: active range length
 * @ALLOC_CIDS: storage capacity in cids, at least @NR_CIDS
 *
 * @NAME aliases zero-initialized storage with the active range set to
 * [BASE, BASE + NR_CIDS). Use scx_cmask_reframe() to reshape later, up to
 * @ALLOC_CIDS.
 */
#define __SCX_CMASK_DEFINE(NAME, BASE, NR_CIDS, ALLOC_CIDS)			\
	_DEFINE_FLEX(struct scx_cmask, NAME, bits, SCX_CMASK_NR_WORDS(ALLOC_CIDS), \
		     = { .base = (BASE),					\
			 .nr_cids = (NR_CIDS),					\
			 .alloc_words = SCX_CMASK_NR_WORDS(ALLOC_CIDS) })

/**
 * SCX_CMASK_DEFINE - Define an on-stack cmask on tight storage
 * @NAME: variable name to define
 * @BASE: first cid of the active range
 * @NR_CIDS: active range length, also storage capacity
 *
 * @NAME aliases zero-initialized storage with the active range and storage
 * both [BASE, BASE + NR_CIDS).
 */
#define SCX_CMASK_DEFINE(NAME, BASE, NR_CIDS)					\
	__SCX_CMASK_DEFINE(NAME, BASE, NR_CIDS, NR_CIDS)

/**
 * SCX_CMASK_DEFINE_SHARD - Define an on-stack cmask sized to one shard
 * @NAME: variable name to define
 * @BASE: first cid of the active range
 * @NR_CIDS: active range length, must be <= SCX_CID_SHARD_MAX_CPUS
 *
 * Storage is fixed at SCX_CID_SHARD_MAX_CPUS, active range framed by
 * (BASE, NR_CIDS). Passing NR_CIDS > SCX_CID_SHARD_MAX_CPUS leaves the
 * cmask claiming more bits than storage holds and subsequent cmask
 * operations will overrun.
 */
#define SCX_CMASK_DEFINE_SHARD(NAME, BASE, NR_CIDS)				\
	__SCX_CMASK_DEFINE(NAME, BASE, NR_CIDS, SCX_CID_SHARD_MAX_CPUS)

/*
 * scx_cmask_ref: validated reference to a BPF-arena cmask.
 *
 * scx_cmask_ref_init() normalizes the pointer into the arena and snapshots
 * @base/@nr_cids. The snapshot is what downstream code uses for sizing - the
 * live header can be mutated concurrently by BPF.
 *
 * scx_cmask_ref_shard() reads one shard into a cmask. scx_cmask_ref_or() and
 * scx_cmask_ref_copy() write back into the referenced arena cmask, bounded by
 * the snapshot.
 *
 * Typical input use:
 *
 *	struct scx_cmask_ref ref;
 *	SCX_CMASK_DEFINE(shard, 0, SCX_CID_SHARD_MAX_CPUS);
 *	s32 idx, ret;
 *
 *	ret = scx_cmask_ref_init(sch, src, &ref);
 *	if (ret < 0)
 *		return ret;
 *
 *	for (idx = ref.shard_first; idx < ref.shard_end; idx++) {
 *		scx_cmask_ref_shard(&ref, idx, shard);
 *		if (!shard->nr_cids)
 *			continue;
 *		... use idx and shard ...
 *	}
 */
struct scx_cmask_ref {
	struct scx_sched	*sch;
	struct scx_cmask	*src;
	u32			base;
	u32			nr_cids;
	s32			shard_first;
	s32			shard_end;
};

#endif /* _KERNEL_SCHED_EXT_TYPES_H */
