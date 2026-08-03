/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared definitions between scx_qmap.bpf.c and scx_qmap.c.
 *
 * The scheduler keeps all state in a single BPF arena map. struct
 * qmap_arena is the one object that lives at the base of the arena and is
 * mmap'd into userspace so the loader can read counters directly.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef __SCX_QMAP_H
#define __SCX_QMAP_H

#ifdef __BPF__
#include <scx/bpf_arena_common.bpf.h>
#else
#include <linux/types.h>
#include <scx/bpf_arena_common.h>
#endif

#define MAX_SUB_SCHEDS		8
#define MAX_PARTS		(MAX_SUB_SCHEDS + 1)	/* participants: children + self */

/*
 * cpu_ctxs[] is sized to a fixed cap so the layout is shared between BPF and
 * userspace. Keep this in sync with NR_CPUS used by the BPF side.
 */
#define SCX_QMAP_MAX_CPUS	1024

/*
 * An owner id identifies who holds a cid: a child slot in [0, MAX_SUB_SCHEDS),
 * CID_SELF for this node, CID_NONE for a cid not currently held, or CID_SHARED
 * for a cid in the round-robin pool (its live holder is rr_slots[rr_pos]). Used
 * by the partition's cid_owner[].
 */
#define CID_SELF	(-1)
#define CID_NONE	(-2)
#define CID_SHARED	(-3)

/* -C cid-override test modes. Selects cid_override_mode in scx_qmap.bpf.c. */
enum qmap_cid_override {
	QMAP_CID_OVR_OFF	= 0,	/* disabled */
	QMAP_CID_OVR_SHUFFLE	= 1,	/* valid reversed cpu->cid mapping */
	QMAP_CID_OVR_BAD_DUP	= 2,	/* invalid: duplicate cid assignment */
	QMAP_CID_OVR_BAD_RANGE	= 3,	/* invalid: out-of-range cid */
	QMAP_CID_OVR_BAD_MONO	= 4,	/* invalid: non-monotonic shard_start */
};

struct cpu_ctx {
	u64 dsp_idx;		/* dispatch index */
	u64 dsp_cnt;		/* remaining count */
	u32 avg_weight;
	u32 cpuperf_target;
};

struct qmap_fifo {
	struct task_ctx __arena *head;
	struct task_ctx __arena *tail;
	s32 idx;
};

/* -J fault-injection modes. Selects inject_mode in struct qmap_arena. */
enum qmap_inject {
	QMAP_INJ_OFF		= 0,
	QMAP_INJ_WRONG_CID	= 1,	/* dispatch to a cid we don't hold */
	QMAP_INJ_INIT_FAIL	= 2,	/* fail init_task for "qmfail*" comms */
	QMAP_INJ_CGRP_INIT_FAIL	= 3,	/* fail cpuctl_init for "qmfail*" cgroups */
};

/*
 * scx_cmask's are embedded in struct qmap_arena with inline backing storage.
 * The bpf side uses &field.mask with the normal cmask_* helpers. Userspace
 * doesn't have access to the type definition and sees same-sized opaque words.
 * _Static_assert()'s in .bpf.c ensure that they are in sync.
 */
#define QMAP_CMASK_WORDS	(((SCX_QMAP_MAX_CPUS) + 63) / 64 + 1)
struct qmap_cmask {
#ifdef __BPF__
	union {
		struct scx_cmask mask;
		u64 words[QMAP_CMASK_WORDS + 2];
	};
#else
	u64 words[QMAP_CMASK_WORDS + 2];
#endif
};

/* Opaque to userspace; defined in scx_qmap.bpf.c. */
struct task_ctx;

/* per-direct-child state for the sub-scheduler */
struct sub_sched_ctx {
	u64 cgroup_id;
	u32 weight;			/* cpu.weight, seeded at attach, then set_weight */
	u64 nr_dsps;
	struct qmap_cmask granted_cids;	/* cids granted excl to this child */
	struct qmap_cmask prev_granted;	/* last grant, for delta calculation */
};

/*
 * compute_partition() builds the following from this node's held caps, and
 * apply_partition()/rr_advance() execute it. Userspace only reads for the
 * hierarchy display.
 */
struct qmap_partition {
	u32 nr_excl;			/* number of excl-held (delegatable) cids */
	s32 cid_owner[SCX_QMAP_MAX_CPUS]; /* per cid: owner id, or CID_NONE */
	s32 shared_cids[MAX_PARTS];	/* the round-robin cid pool */
	u32 nr_shared;			/* number of shared_cids entries */
	u64 rr_slots[MAX_PARTS];	/* rotation order: holder cgroup_id, 0 = self */
	u32 nr_rr;			/* number of rr_slots entries */
	u32 rr_pos;			/* current rotation index */
};

struct qmap_arena {
	/* userspace-visible stats */
	u64 nr_enqueued, nr_dispatched, nr_reenqueued, nr_reenqueued_cid0;
	u64 nr_dequeued, nr_ddsp_from_enq;
	u64 nr_core_sched_execed;
	u64 nr_expedited_local, nr_expedited_remote;
	u64 nr_expedited_lost, nr_expedited_from_timer;
	u64 nr_highpri_queued;
	u32 test_error_cnt;
	u32 cpuperf_min, cpuperf_avg, cpuperf_max;
	u32 cpuperf_target_min, cpuperf_target_avg, cpuperf_target_max;

	/* kernel-side runtime state */
	u64 core_sched_head_seqs[5];
	u64 core_sched_tail_seqs[5];

	struct cpu_ctx cpu_ctxs[SCX_QMAP_MAX_CPUS];

	/* task_ctx slab; allocated and threaded by qmap_init() */
	struct task_ctx __arena *task_ctxs;
	struct task_ctx __arena *task_free_head;

	/* five priority FIFOs, each a doubly-linked list through task_ctx */
	struct qmap_fifo fifos[5];

	/*
	 * Hierarchical sub-scheduling state. See the design comment at the top
	 * of scx_qmap.bpf.c.
	 */
	u32 nr_cids;			/* cid count, cached at init */

	/* bpf-owned partition: read by userspace for display */
	struct qmap_partition part;

	struct sub_sched_ctx sub_sched_ctxs[MAX_SUB_SCHEDS]; /* per-child context */
	u64 nr_sub_scheds;		/* number of attached children */

	/* bpf-internal per-cid state */
	u8 cid_shared[SCX_QMAP_MAX_CPUS]; /* per cid: 1 if held shared (ENQ_IMMED-only) */

	/* allocated cid-time, charged per owner by account_alloc() */
	u64 alloc_ns[MAX_SUB_SCHEDS];	/* per child slot */
	u64 self_alloc_ns;
	u64 alloc_ts;			/* last accounting timestamp */
	u64 alloc_window_ns;		/* total accounted time, the alloc denominator */

	/* bpf-internal cmasks (embedded, see struct qmap_cmask) */
	struct qmap_cmask self_cids;	/* cids this node runs its own tasks on */
	struct qmap_cmask idle_cids;	/* idle state of all cids regardless of delegation */
	struct qmap_cmask rr_cids;	/* the shared pool, as a mask for grant/revoke */

	/* scratch cmasks */
	struct qmap_cmask to_revoke_cids; /* delta cids to revoke */
	struct qmap_cmask to_grant_cids; /* delta cids to grant */
	struct qmap_cmask prev_rr_cids; /* previous shared pool, to clear stale grants */
	struct qmap_cmask held_excl;	/* cids held excl (ENQ): delegatable */
	struct qmap_cmask held_shared;	/* cids held shared (ENQ_IMMED only): self-local */

	/* bpf -> userspace: stats */
	u64 nr_reenq_cap;		/* SCX_TASK_REENQ_CAP bounces */
	u64 nr_reenq_immed;		/* SCX_TASK_REENQ_IMMED bounces */
	u64 nr_inject_attempts;		/* fault-injection: dispatches to an unheld cid */
	u64 nr_rescue_dsp;		/* SCX_ENQ_RESCUE dispatch attempts */
	u32 inject_mode;		/* fault-injection mode (QMAP_INJ_*) */
};

#endif /* __SCX_QMAP_H */
