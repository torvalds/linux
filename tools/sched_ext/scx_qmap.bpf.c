/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scx_qmap: a demonstration and testing scheduler for sched_ext features.
 *
 * A simple scheduler that exercises a broad set of sched_ext features. Unlikely
 * to be useful for real workloads. It demonstrates:
 *
 * - BPF-side queueing using TIDs.
 * - BPF arena for scheduler state.
 * - Core-sched support.
 * - Hierarchical sub-scheduling: delegating cpus to child cgroup schedulers.
 *
 * Base design: Five FIFOs (arena-backed doubly-linked lists through per-task
 * context). A task is assigned to a FIFO by its compound weight. Each cpu
 * round-robins the FIFOs, dispatching more from higher ones.
 *
 * Sub-scheduling: Any qmap sched can delegate cpus to its own child cgroup
 * schedulers and keep the rest for its tasks. Terminology:
 *
 *   excl   - A cpu the delegatee owns wholly (ENQ_IMMED|ENQ|PREEMPT).
 *   shared - A cpu delegated as ENQ_IMMED only. Time-shared.
 *   held_excl / held_shared - What this node was handed by its parent.
 *            held-excl cpus are re-delegatable. A held-shared cpu is a
 *            time-share that stays self-local.
 *   self   - The excl cpus the node kept for itself, plus all of held_shared.
 *   owner  - Who holds a cid - a child slot, CID_SELF, or CID_NONE.
 *
 * The scheduler splits its held-excl cpus among self and the children in
 * proportion to each node's cpu.weight, handing each the floor of its share as
 * excl cpus. The leftover from rounding forms a shared pool the round-robin
 * timer hands around. With no excl cpu to delegate, the node evicts its
 * children.
 *
 * This policy is a demonstration only, not a practical one. The split
 * considers only direct children and is not work-conserving. It only exists to
 * drive sub-sched primitives with as simple logic as possible.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>

#include "scx_qmap.h"

enum consts {
	ONE_SEC_IN_NS		= 1000000000,
	ONE_MSEC_IN_NS		= 1000000,
	LOWPRI_INTV_NS		= 10 * ONE_MSEC_IN_NS,
	SHARED_DSQ		= 0,
	HIGHPRI_DSQ		= 1,
	LOWPRI_DSQ		= 2,
	HIGHPRI_WEIGHT		= 8668,		/* this is what -20 maps to */
};

char _license[] SEC("license") = "GPL";

const volatile u64 slice_ns;
const volatile u32 stall_user_nth;
const volatile u32 stall_kernel_nth;
const volatile u32 dsp_inf_loop_after;
const volatile u32 dsp_batch;
const volatile bool highpri_boosting;
const volatile bool print_dsqs_and_events;
const volatile bool print_msgs;
const volatile u64 sub_cgroup_id;
const volatile s32 disallow_tgid;
const volatile bool suppress_dump;
const volatile u32 immed_stress_nth;
const volatile u32 max_tasks;

/* sub-sched: period for handing the round-robin cid pool to the next child */
const volatile u64 round_robin_ns;

/*
 * Optional cid-override test harness. When cid_override_mode is non-zero,
 * qmap_init_cids() calls scx_bpf_cid_override() with the caller-supplied arrays
 * to exercise the kfunc's acceptance and error paths. See enum
 * qmap_cid_override for the modes.
 */
const volatile u32 cid_override_mode;
const volatile u32 cid_override_nr_shards;

UEI_DEFINE(uei);

/*
 * All scheduler state - per-cpu context, stats counters, core-sched sequence
 * numbers, sub-sched cgroup ids - lives in this single BPF arena map. Userspace
 * reaches it via skel->arena->qa.
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1 << 16);		/* upper bound in pages */
#if defined(__TARGET_ARCH_arm64) || defined(__aarch64__)
	__ulong(map_extra, 0x1ull << 32);	/* user/BPF mmap base */
#else
	__ulong(map_extra, 0x1ull << 44);
#endif
} arena SEC(".maps");

struct qmap_arena __arena_global qa;

/* ensure that BPF and userspace are seeing the same size for qmap_cmask */
_Static_assert(QMAP_CMASK_WORDS == CMASK_NR_WORDS(SCX_QMAP_MAX_CPUS),
	       "QMAP_CMASK_WORDS must equal CMASK_NR_WORDS(SCX_QMAP_MAX_CPUS)");
_Static_assert(sizeof(struct qmap_cmask) ==
	       struct_size_t(struct scx_cmask, bits, QMAP_CMASK_WORDS),
	       "qmap_cmask must be exactly sized to back a full scx_cmask");

/* Per-queue locks. Each in its own .data section as bpf_res_spin_lock requires. */
__hidden struct bpf_res_spin_lock qa_q_lock0 SEC(".data.qa_q_lock0");
__hidden struct bpf_res_spin_lock qa_q_lock1 SEC(".data.qa_q_lock1");
__hidden struct bpf_res_spin_lock qa_q_lock2 SEC(".data.qa_q_lock2");
__hidden struct bpf_res_spin_lock qa_q_lock3 SEC(".data.qa_q_lock3");
__hidden struct bpf_res_spin_lock qa_q_lock4 SEC(".data.qa_q_lock4");

static struct bpf_res_spin_lock *qa_q_lock(s32 qid)
{
	switch (qid) {
	case 0:	return &qa_q_lock0;
	case 1:	return &qa_q_lock1;
	case 2:	return &qa_q_lock2;
	case 3:	return &qa_q_lock3;
	case 4:	return &qa_q_lock4;
	default: return NULL;
	}
}

/*
 * If enabled, CPU performance target is set according to the queue index
 * according to the following table.
 */
static const u32 qidx_to_cpuperf_target[] = {
	[0] = SCX_CPUPERF_ONE * 0 / 4,
	[1] = SCX_CPUPERF_ONE * 1 / 4,
	[2] = SCX_CPUPERF_ONE * 2 / 4,
	[3] = SCX_CPUPERF_ONE * 3 / 4,
	[4] = SCX_CPUPERF_ONE * 4 / 4,
};

/*
 * Per-queue sequence numbers to implement core-sched ordering.
 *
 * Tail seq is assigned to each queued task and incremented. Head seq tracks the
 * sequence number of the latest dispatched task. The distance between the a
 * task's seq and the associated queue's head seq is called the queue distance
 * and used when comparing two tasks for ordering. See qmap_core_sched_before().
 */

/*
 * Per-task scheduling context. Allocated from the qa.task_ctxs[] slab in
 * arena. While the task is alive the entry is referenced from task_ctx_stor;
 * while it's free the entry sits on the free list singly-linked through
 * @next_free.
 *
 * When the task is queued on one of the five priority FIFOs, @q_idx is the
 * queue index and @q_next/@q_prev link it in the queue's doubly-linked list.
 * @q_idx is -1 when the task isn't on any queue.
 */
struct task_ctx {
	struct task_ctx __arena	*next_free;	/* only valid on free list */
	struct task_ctx __arena	*q_next;	/* queue link, NULL if tail */
	struct task_ctx __arena	*q_prev;	/* queue link, NULL if head */
	struct qmap_fifo __arena *fifo;		/* queue we're on, NULL if not queued */
	u64			tid;
	s32			pid;	/* for dump only */
	bool			force_local;	/* Dispatch directly to local_dsq */
	bool			highpri;
	u64			core_sched_seq;
	struct scx_cmask	cpus_allowed;	/* per-task affinity in cid space */
};

/*
 * Slab stride for task_ctx. cpus_allowed's flex array bits[] overlaps the
 * tail bytes appended per entry; struct_size() gives the actual per-entry
 * footprint.
 */
#define TASK_CTX_STRIDE							\
	struct_size_t(struct task_ctx, cpus_allowed.bits,		\
		      CMASK_NR_WORDS(SCX_QMAP_MAX_CPUS))

/* All task_ctx pointers are arena pointers. */
typedef struct task_ctx __arena task_ctx_t;

/* Holds an arena pointer to the task's slab entry. */
struct task_ctx_stor_val {
	task_ctx_t		*taskc;
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx_stor_val);
} task_ctx_stor SEC(".maps");

/* Protects the task_ctx slab free list. */
__hidden struct bpf_res_spin_lock qa_task_lock SEC(".data.qa_task_lock");

static int qmap_spin_lock(struct bpf_res_spin_lock *lock)
{
	if (bpf_res_spin_lock(lock)) {
		scx_bpf_error("res_spin_lock failed");
		return -EBUSY;
	}
	return 0;
}

/*
 * Try prev_cid, then scan cpus_allowed AND idle_cids AND self_cids round-robin
 * from prev_cid + 1. Atomic claim retries on race; bounded by
 * IDLE_PICK_RETRIES to keep the verifier's insn budget in check.
 */
#define IDLE_PICK_RETRIES	16

static s32 pick_direct_dispatch_cid(struct task_struct *p, s32 prev_cid,
				    task_ctx_t *taskc)
{
	u32 nr_cids = scx_bpf_nr_cids();
	s32 cid;
	u32 i;

	if (cmask_test(prev_cid, &qa.self_cids.mask) &&
	    cmask_test_and_clear(prev_cid, &qa.idle_cids.mask))
		return prev_cid;

	cid = prev_cid;
	bpf_for(i, 0, IDLE_PICK_RETRIES) {
		cid = cmask_next_and2_set_wrap(&taskc->cpus_allowed,
					       &qa.idle_cids.mask,
					       &qa.self_cids.mask, cid + 1);
		barrier_var(cid);
		if (cid >= nr_cids)
			return -1;
		if (cmask_test_and_clear(cid, &qa.idle_cids.mask))
			return cid;
	}
	return -1;
}

/*
 * Force a reference to the arena map. The verifier associates an arena with
 * a program by finding an LD_IMM64 instruction that loads the arena's BPF
 * map; programs that only use arena pointers returned from task-local
 * storage (like qmap_select_cpu) never reference @arena directly. Without
 * this, the verifier rejects addr_space_cast with "addr_space_cast insn
 * can only be used in a program that has an associated arena".
 */
#define QMAP_TOUCH_ARENA() do { asm volatile("" :: "r"(&arena)); } while (0)

static task_ctx_t *lookup_task_ctx(struct task_struct *p)
{
	struct task_ctx_stor_val *v;

	QMAP_TOUCH_ARENA();

	v = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!v || !v->taskc)
		return NULL;
	return v->taskc;
}

/* Append @taskc to the tail of @fifo. Must not already be queued. */
static void qmap_fifo_enqueue(struct qmap_fifo __arena *fifo, task_ctx_t *taskc)
{
	struct bpf_res_spin_lock *lock = qa_q_lock(fifo->idx);

	if (!lock || qmap_spin_lock(lock))
		return;
	taskc->fifo = fifo;
	taskc->q_next = NULL;
	taskc->q_prev = fifo->tail;
	if (fifo->tail)
		fifo->tail->q_next = taskc;
	else
		fifo->head = taskc;
	fifo->tail = taskc;
	bpf_res_spin_unlock(lock);
}

/* Pop the head of @fifo. Returns NULL if empty. */
static task_ctx_t *qmap_fifo_pop(struct qmap_fifo __arena *fifo)
{
	struct bpf_res_spin_lock *lock = qa_q_lock(fifo->idx);
	task_ctx_t *taskc;

	if (!lock || qmap_spin_lock(lock))
		return NULL;
	taskc = fifo->head;
	if (taskc) {
		fifo->head = taskc->q_next;
		if (taskc->q_next)
			taskc->q_next->q_prev = NULL;
		else
			fifo->tail = NULL;
		taskc->q_next = NULL;
		taskc->q_prev = NULL;
		taskc->fifo = NULL;
	}
	bpf_res_spin_unlock(lock);
	return taskc;
}

/* Remove @taskc from its fifo. No-op if not queued. */
static void qmap_fifo_remove(task_ctx_t *taskc)
{
	struct qmap_fifo __arena *fifo = taskc->fifo;
	struct bpf_res_spin_lock *lock;

	if (!fifo)
		return;

	lock = qa_q_lock(fifo->idx);
	if (!lock || qmap_spin_lock(lock))
		return;

	/* Re-check under lock — a concurrent pop may have cleared fifo. */
	if (taskc->fifo != fifo) {
		bpf_res_spin_unlock(lock);
		return;
	}

	if (taskc->q_next)
		taskc->q_next->q_prev = taskc->q_prev;
	else
		fifo->tail = taskc->q_prev;
	if (taskc->q_prev)
		taskc->q_prev->q_next = taskc->q_next;
	else
		fifo->head = taskc->q_next;
	taskc->q_next = NULL;
	taskc->q_prev = NULL;
	taskc->fifo = NULL;
	bpf_res_spin_unlock(lock);
}

s32 BPF_STRUCT_OPS(qmap_select_cid, struct task_struct *p,
		   s32 prev_cid, u64 wake_flags)
{
	task_ctx_t *taskc;
	s32 cid;

	if (!(taskc = lookup_task_ctx(p)))
		return prev_cid;

	if (p->scx.weight < 2 && !(p->flags & PF_KTHREAD))
		return prev_cid;

	cid = pick_direct_dispatch_cid(p, prev_cid, taskc);

	if (cid >= 0) {
		taskc->force_local = true;
		return cid;
	} else {
		return prev_cid;
	}
}

/*
 * A received time-shared cid is held ENQ_IMMED-only, so inserts must set
 * SCX_ENQ_IMMED.
 */
static u64 needs_immed(s32 cid)
{
	return qa.cid_shared[cid] ? SCX_ENQ_IMMED : 0;
}

/* first cid this node does NOT hold for fault injection, -1 if none */
static s32 first_unavail_cid(void)
{
	s32 nr_cids = qa.nr_cids, c;

	if (nr_cids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("-ERANGE");
		return -1;
	}

	bpf_for(c, 0, nr_cids) {
		if (!cmask_test(c, &qa.held_excl.mask) &&
		    !cmask_test(c, &qa.held_shared.mask))
			return c;
	}
	return -1;
}

static int weight_to_idx(u32 weight)
{
	/* Coarsely map the compound weight to a FIFO. */
	if (weight <= 25)
		return 0;
	else if (weight <= 50)
		return 1;
	else if (weight < 200)
		return 2;
	else if (weight < 400)
		return 3;
	else
		return 4;
}

void BPF_STRUCT_OPS(qmap_enqueue, struct task_struct *p, u64 enq_flags)
{
	static u32 user_cnt, kernel_cnt;
	task_ctx_t *taskc;
	int idx = weight_to_idx(p->scx.weight);
	s32 cid;

	if (enq_flags & SCX_ENQ_REENQ) {
		u64 reason = p->scx.flags & SCX_TASK_REENQ_REASON_MASK;

		__sync_fetch_and_add(&qa.nr_reenqueued, 1);
		if (scx_bpf_task_cid(p) == 0)
			__sync_fetch_and_add(&qa.nr_reenqueued_cid0, 1);
		/* cap-loss and IMMED-handback bounces, relocated below */
		if (reason == SCX_TASK_REENQ_CAP)
			__sync_fetch_and_add(&qa.nr_reenq_cap, 1);
		else if (reason == SCX_TASK_REENQ_IMMED)
			__sync_fetch_and_add(&qa.nr_reenq_immed, 1);
	}

	if (p->flags & PF_KTHREAD) {
		if (stall_kernel_nth && !(++kernel_cnt % stall_kernel_nth))
			return;
	} else {
		if (stall_user_nth && !(++user_cnt % stall_user_nth))
			return;
	}

	if (qa.test_error_cnt && !--qa.test_error_cnt)
		scx_bpf_error("test triggering error");

	if (!(taskc = lookup_task_ctx(p)))
		return;

	/*
	 * All enqueued tasks must have their core_sched_seq updated for correct
	 * core-sched ordering. Also, take a look at the end of qmap_dispatch().
	 */
	taskc->core_sched_seq = qa.core_sched_tail_seqs[idx]++;

	/*
	 * A task of ours that can run on none of our self cids - the parent
	 * didn't grant them or we delegated them to children - would starve in
	 * SHARED/FIFO since we only pull from those on self cids.
	 *
	 * Force it onto its first allowed cid's local DSQ. If we hold that cid
	 * it runs. Otherwise the insert carries SCX_ENQ_RESCUE and the kernel
	 * diverts the task to its rescue path.
	 */
	if (!cmask_intersects(&taskc->cpus_allowed, &qa.self_cids.mask)) {
		s32 c = cmask_next_set_wrap(&taskc->cpus_allowed, 0);

		if (c >= 0 && c < scx_bpf_nr_cids()) {
			taskc->force_local = false;
			__sync_fetch_and_add(&qa.nr_rescue_dsp, 1);
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | c, slice_ns,
					   enq_flags | needs_immed(c) | SCX_ENQ_RESCUE);
			return;
		}
	}

	/*
	 * Fault injection: deliberately dispatch one of our own tasks to a cid
	 * we don't hold. The inserts carry SCX_ENQ_RESCUE and divert to the
	 * kernel rescue path, a deterministic rescue-traffic generator. Under
	 * -B 0 the kernel cap check rejects and re-enqueues them instead, so
	 * nr_inject_attempts tracks nr_reenq_cap 1:1 and proves delivery-time
	 * enforcement. Throttled.
	 */
	if (qa.inject_mode == QMAP_INJ_WRONG_CID && p->nr_cpus_allowed > 1 &&
	    !(enq_flags & SCX_ENQ_REENQ)) {
		static u32 inj_cnt;

		if (!(++inj_cnt % 64)) {
			s32 bad = first_unavail_cid();

			if (bad >= 0 && cmask_test(bad, &taskc->cpus_allowed)) {
				__sync_fetch_and_add(&qa.nr_inject_attempts, 1);
				__sync_fetch_and_add(&qa.nr_rescue_dsp, 1);
				scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | bad, slice_ns,
						   enq_flags | SCX_ENQ_RESCUE);
				return;
			}
		}
	}

	/*
	 * IMMED stress testing: Every immed_stress_nth'th enqueue, dispatch
	 * directly to prev_cpu's local DSQ even when busy to force dsq->nr > 1
	 * and exercise the kernel IMMED reenqueue trigger paths.
	 */
	if (immed_stress_nth && !(enq_flags & SCX_ENQ_REENQ)) {
		static u32 immed_stress_cnt;

		if (!(++immed_stress_cnt % immed_stress_nth)) {
			taskc->force_local = false;
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | scx_bpf_task_cid(p),
					   slice_ns, enq_flags);
			return;
		}
	}

	/*
	 * If qmap_select_cid() is telling us to or this is the last runnable
	 * task on the CPU, enqueue locally.
	 */
	if (taskc->force_local) {
		taskc->force_local = false;
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice_ns,
				   enq_flags | needs_immed(scx_bpf_task_cid(p)));
		return;
	}

	/* see lowpri_timerfn() */
	if (__COMPAT_has_generic_reenq() &&
	    p->scx.weight < 2 && !(p->flags & PF_KTHREAD) && !(enq_flags & SCX_ENQ_REENQ)) {
		scx_bpf_dsq_insert(p, LOWPRI_DSQ, slice_ns, enq_flags);
		return;
	}

	/* if select_cid() wasn't called, try direct dispatch */
	if (!__COMPAT_is_enq_cpu_selected(enq_flags) &&
	    (cid = pick_direct_dispatch_cid(p, scx_bpf_task_cid(p), taskc)) >= 0) {
		__sync_fetch_and_add(&qa.nr_ddsp_from_enq, 1);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cid, slice_ns,
				   enq_flags | needs_immed(cid));
		return;
	}

	/*
	 * If the task was re-enqueued due to the CPU being preempted by a
	 * higher priority scheduling class, just re-enqueue the task directly
	 * on the global DSQ. As we want another CPU to pick it up, find and
	 * kick an idle cid.
	 */
	if (enq_flags & SCX_ENQ_REENQ) {
		s32 cid;

		scx_bpf_dsq_insert(p, SHARED_DSQ, 0, enq_flags);
		cid = cmask_next_and2_set_wrap(&taskc->cpus_allowed,
					       &qa.idle_cids.mask,
					       &qa.self_cids.mask, 0);
		if (cid < scx_bpf_nr_cids())
			scx_bpf_kick_cid(cid, SCX_KICK_IDLE);
		return;
	}

	/* Queue on the selected FIFO. */
	qmap_fifo_enqueue(&qa.fifos[idx], taskc);

	if (highpri_boosting && p->scx.weight >= HIGHPRI_WEIGHT) {
		taskc->highpri = true;
		__sync_fetch_and_add(&qa.nr_highpri_queued, 1);
	}
	__sync_fetch_and_add(&qa.nr_enqueued, 1);
}

void BPF_STRUCT_OPS(qmap_dequeue, struct task_struct *p, u64 deq_flags)
{
	task_ctx_t *taskc;

	__sync_fetch_and_add(&qa.nr_dequeued, 1);
	if (deq_flags & SCX_DEQ_CORE_SCHED_EXEC)
		__sync_fetch_and_add(&qa.nr_core_sched_execed, 1);

	taskc = lookup_task_ctx(p);
	if (taskc && taskc->fifo) {
		if (taskc->highpri)
			__sync_fetch_and_sub(&qa.nr_highpri_queued, 1);
		qmap_fifo_remove(taskc);
	}
}

static void update_core_sched_head_seq(struct task_struct *p)
{
	int idx = weight_to_idx(p->scx.weight);
	task_ctx_t *taskc;

	if ((taskc = lookup_task_ctx(p)))
		qa.core_sched_head_seqs[idx] = taskc->core_sched_seq;
}

/*
 * One pass over SHARED_DSQ: rescue stranded tasks and boost highpri ones. A
 * task whose cids were lost while it was queued in the fifos would strand on
 * SHARED_DSQ, which is consumed only on self cids it can't run on - move it to
 * the kernel rescue path. One whose cids were lost after the highpri cull is
 * likewise rescued out of HIGHPRI_DSQ below.
 *
 * To demonstrate the use of scx_bpf_dsq_move(), implement silly selective
 * priority boosting mechanism by moving highpri tasks to HIGHPRI_DSQ and then
 * consuming them first. This makes minor difference only when dsp_batch is
 * larger than 1.
 *
 * scx_bpf_dsq_move[_vtime]() are allowed both from ops.dispatch() and
 * non-rq-lock holding BPF programs. As demonstration, this function is called
 * from qmap_dispatch() and monitor_timerfn().
 */
static bool scan_shared_dsq(bool from_timer)
{
	struct task_struct *p;
	s32 this_cid = scx_bpf_this_cid();
	u32 nr_cids = scx_bpf_nr_cids();

	/* rescue strands and move highpri tasks to HIGHPRI_DSQ */
	bpf_for_each(scx_dsq, p, SHARED_DSQ, 0) {
		static u64 highpri_seq;
		task_ctx_t *taskc;
		s32 c;

		if (!(taskc = lookup_task_ctx(p)))
			return false;

		/* stranded? rescue - it can't be dispatched here either way */
		if (!cmask_intersects(&taskc->cpus_allowed, &qa.self_cids.mask)) {
			c = cmask_next_set_wrap(&taskc->cpus_allowed, 0);
			if (c >= 0 && c < scx_bpf_nr_cids()) {
				__sync_fetch_and_add(&qa.nr_rescue_dsp, 1);
				scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_LOCAL_ON | c,
						 needs_immed(c) | SCX_ENQ_RESCUE);
			}
			continue;
		}

		if (taskc->highpri) {
			/* exercise the set_*() and vtime interface too */
			scx_bpf_dsq_move_set_slice(BPF_FOR_EACH_ITER, slice_ns * 2);
			scx_bpf_dsq_move_set_vtime(BPF_FOR_EACH_ITER, highpri_seq++);
			scx_bpf_dsq_move_vtime(BPF_FOR_EACH_ITER, p, HIGHPRI_DSQ, 0);
		}
	}

	/*
	 * Scan HIGHPRI_DSQ and dispatch until a task that can run here is
	 * found. Prefer this_cid if the task allows it; otherwise RR-scan the
	 * task's cpus_allowed starting after this_cid.
	 */
	bpf_for_each(scx_dsq, p, HIGHPRI_DSQ, 0) {
		task_ctx_t *taskc;
		bool dispatched = false;
		s32 cid;

		if (!(taskc = lookup_task_ctx(p)))
			return false;

		/* only run highpri tasks on cids this node holds, not delegated ones */
		if (cmask_test(this_cid, &taskc->cpus_allowed) &&
		    cmask_test(this_cid, &qa.self_cids.mask))
			cid = this_cid;
		else
			cid = cmask_next_and_set_wrap(&taskc->cpus_allowed,
						      &qa.self_cids.mask,
						      this_cid + 1);
		if (cid >= nr_cids) {
			/* stranded after the cull - rescue it from here */
			s32 c = cmask_next_set_wrap(&taskc->cpus_allowed, 0);

			if (c >= 0 && c < nr_cids) {
				__sync_fetch_and_add(&qa.nr_rescue_dsp, 1);
				scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_LOCAL_ON | c,
						 needs_immed(c) | SCX_ENQ_RESCUE);
			}
			continue;
		}

		if (scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_LOCAL_ON | cid,
				     SCX_ENQ_PREEMPT | needs_immed(cid))) {
			if (cid == this_cid) {
				dispatched = true;
				__sync_fetch_and_add(&qa.nr_expedited_local, 1);
			} else {
				__sync_fetch_and_add(&qa.nr_expedited_remote, 1);
			}
			if (from_timer)
				__sync_fetch_and_add(&qa.nr_expedited_from_timer, 1);
		} else {
			__sync_fetch_and_add(&qa.nr_expedited_lost, 1);
		}

		if (dispatched)
			return true;
	}

	return false;
}

void BPF_STRUCT_OPS(qmap_dispatch, s32 cid, struct task_struct *prev)
{
	struct task_struct *p;
	struct cpu_ctx __arena *cpuc;
	task_ctx_t *taskc;
	u32 batch = dsp_batch ?: 1;
	s32 owner, i;

	if (scan_shared_dsq(false))
		return;

	/*
	 * Sub-sched routing: a child-owned cid goes to its owner. Never run
	 * this node's own tasks on a delegated cid. Read without the guard.
	 */
	owner = qa.part.cid_owner[cid];
	if (owner == CID_SHARED) {
		/* route to the live rr holder (0 = self, runs below) */
		s32 pos = qa.part.rr_pos;
		u64 holder_cgid = (pos >= 0 && pos < MAX_PARTS) ?
				  qa.part.rr_slots[pos] : 0;

		if (holder_cgid) {
			scx_bpf_sub_dispatch(holder_cgid);
			return;
		}
	} else if (owner >= 0 && owner < MAX_SUB_SCHEDS) {
		u64 cgid = qa.sub_sched_ctxs[owner].cgroup_id;

		if (cgid) {
			if (scx_bpf_sub_dispatch(cgid))
				__sync_fetch_and_add(&qa.sub_sched_ctxs[owner].nr_dsps, 1);
			return;
		}
	}

	if (!qa.nr_highpri_queued && scx_bpf_dsq_move_to_local(SHARED_DSQ, needs_immed(cid)))
		return;

	if (dsp_inf_loop_after && qa.nr_dispatched > dsp_inf_loop_after) {
		/*
		 * PID 2 should be kthreadd which should mostly be idle and off
		 * the scheduler. Let's keep dispatching it to force the kernel
		 * to call this function over and over again.
		 */
		p = bpf_task_from_pid(2);
		if (p) {
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice_ns, 0);
			bpf_task_release(p);
			return;
		}
	}

	cpuc = &qa.cpu_ctxs[scx_bpf_this_cid()];

	for (i = 0; i < 5; i++) {
		/* Advance the dispatch cursor and pick the fifo. */
		if (!cpuc->dsp_cnt) {
			cpuc->dsp_idx = (cpuc->dsp_idx + 1) % 5;
			cpuc->dsp_cnt = 1 << cpuc->dsp_idx;
		}

		/* Dispatch or advance. */
		bpf_repeat(BPF_MAX_LOOPS) {
			task_ctx_t *taskc;

			taskc = qmap_fifo_pop(&qa.fifos[cpuc->dsp_idx]);
			if (!taskc)
				break;

			p = scx_bpf_tid_to_task(taskc->tid);
			if (!p)
				continue;

			if (taskc->highpri)
				__sync_fetch_and_sub(&qa.nr_highpri_queued, 1);

			update_core_sched_head_seq(p);
			__sync_fetch_and_add(&qa.nr_dispatched, 1);

			scx_bpf_dsq_insert(p, SHARED_DSQ, slice_ns, 0);

			/*
			 * scx_qmap uses a global BPF queue that any CPU's
			 * dispatch can pop from. If this CPU popped a task that
			 * can't run here, it gets stranded on SHARED_DSQ after
			 * consume_dispatch_q() skips it. Kick the task's home
			 * CPU so it drains SHARED_DSQ.
			 *
			 * There's a race between the pop and the flush of the
			 * buffered dsq_insert:
			 *
			 *  CPU 0 (dispatching)      CPU 1 (home, idle)
			 *  ~~~~~~~~~~~~~~~~~~~      ~~~~~~~~~~~~~~~~~~~
			 *  pop from BPF queue
			 *  dsq_insert(buffered)
			 *                           balance:
			 *                             SHARED_DSQ empty
			 *                             BPF queue empty
			 *                             -> goes idle
			 *  flush -> on SHARED
			 *  kick CPU 1
			 *                           wakes, drains task
			 *
			 * The kick prevents indefinite stalls but a per-CPU
			 * kthread like ksoftirqd can be briefly stranded when
			 * its home CPU enters idle with softirq pending,
			 * triggering:
			 *
			 *  "NOHZ tick-stop error: local softirq work is pending, handler #N!!!"
			 *
			 * from report_idle_softirq(). The kick lands shortly
			 * after and the home CPU drains the task. This could be
			 * avoided by e.g. dispatching pinned tasks to local or
			 * global DSQs, but the current code is left as-is to
			 * document this class of issue -- other schedulers
			 * seeing similar warnings can use this as a reference.
			 */
			if (!cmask_test(cid, &taskc->cpus_allowed))
				scx_bpf_kick_cid(scx_bpf_task_cid(p), 0);
			batch--;
			cpuc->dsp_cnt--;
			if (!batch || !scx_bpf_dispatch_nr_slots()) {
				if (scan_shared_dsq(false))
					return;
				scx_bpf_dsq_move_to_local(SHARED_DSQ, needs_immed(cid));
				return;
			}
			if (!cpuc->dsp_cnt)
				break;
		}

		cpuc->dsp_cnt = 0;
	}

	if (scan_shared_dsq(false))
		return;

	/*
	 * No other tasks. @prev will keep running. Update its core_sched_seq as
	 * if the task were enqueued and dispatched immediately.
	 */
	if (prev) {
		taskc = lookup_task_ctx(prev);
		if (!taskc)
			return;

		taskc->core_sched_seq =
			qa.core_sched_tail_seqs[weight_to_idx(prev->scx.weight)]++;
	}
}

void BPF_STRUCT_OPS(qmap_tick, struct task_struct *p)
{
	struct cpu_ctx __arena *cpuc = &qa.cpu_ctxs[scx_bpf_this_cid()];
	int idx;

	/*
	 * Use the running avg of weights to select the target cpuperf level.
	 * This is a demonstration of the cpuperf feature rather than a
	 * practical strategy to regulate CPU frequency.
	 */
	cpuc->avg_weight = cpuc->avg_weight * 3 / 4 + p->scx.weight / 4;
	idx = weight_to_idx(cpuc->avg_weight);
	cpuc->cpuperf_target = qidx_to_cpuperf_target[idx];

	scx_bpf_cidperf_set(scx_bpf_task_cid(p), cpuc->cpuperf_target);
}

/*
 * The distance from the head of the queue scaled by the weight of the queue.
 * The lower the number, the older the task and the higher the priority.
 */
static s64 task_qdist(struct task_struct *p, task_ctx_t *taskc)
{
	int idx = weight_to_idx(p->scx.weight);
	s64 qdist;

	qdist = taskc->core_sched_seq - qa.core_sched_head_seqs[idx];

	/*
	 * As queue index increments, the priority doubles. The queue w/ index 3
	 * is dispatched twice more frequently than 2. Reflect the difference by
	 * scaling qdists accordingly. Note that the shift amount needs to be
	 * flipped depending on the sign to avoid flipping priority direction.
	 */
	if (qdist >= 0)
		return qdist << (4 - idx);
	else
		return qdist << idx;
}

/*
 * This is called to determine the task ordering when core-sched is picking
 * tasks to execute on SMT siblings and should encode about the same ordering as
 * the regular scheduling path. Use the priority-scaled distances from the head
 * of the queues to compare the two tasks which should be consistent with the
 * dispatch path behavior.
 */
bool BPF_STRUCT_OPS(qmap_core_sched_before,
		    struct task_struct *a, struct task_struct *b)
{
	task_ctx_t *taskc_a = lookup_task_ctx(a);
	task_ctx_t *taskc_b = lookup_task_ctx(b);

	/*
	 * A task delegated to a sub-scheduler has no task_ctx here. Order such
	 * pairs by the kernel's default ordering - a running task after every
	 * waiting task, then by runnable_at.
	 */
	if (!taskc_a || !taskc_b) {
		if (a->on_cpu != b->on_cpu)
			return b->on_cpu;
		return time_before(a->scx.runnable_at, b->scx.runnable_at);
	}

	return task_qdist(a, taskc_a) < task_qdist(b, taskc_b);
}

/*
 * sched_switch tracepoint and cpu_release handlers are no longer needed.
 * With SCX_OPS_ALWAYS_ENQ_IMMED, wakeup_preempt_scx() reenqueues IMMED
 * tasks when a higher-priority scheduling class takes the CPU.
 */

s32 BPF_STRUCT_OPS_SLEEPABLE(qmap_init_task, struct task_struct *p,
			     struct scx_init_task_args *args)
{
	struct task_ctx_stor_val *v;
	task_ctx_t *taskc;

	if (qa.inject_mode == QMAP_INJ_INIT_FAIL &&
	    !bpf_strncmp(p->comm, 6, "qmfail"))
		return -ENOMEM;

	if (p->tgid == disallow_tgid)
		p->scx.disallow = true;

	/* pop a slab entry off the free list */
	if (qmap_spin_lock(&qa_task_lock))
		return -EBUSY;
	taskc = qa.task_free_head;
	if (taskc)
		qa.task_free_head = taskc->next_free;
	bpf_res_spin_unlock(&qa_task_lock);
	if (!taskc) {
		scx_bpf_error("task_ctx slab exhausted (max_tasks=%u)", max_tasks);
		return -ENOMEM;
	}

	taskc->next_free = NULL;
	taskc->q_next = NULL;
	taskc->q_prev = NULL;
	taskc->fifo = NULL;
	taskc->tid = p->scx.tid;
	taskc->pid = p->pid;
	taskc->force_local = false;
	taskc->highpri = false;
	taskc->core_sched_seq = 0;
	cmask_init(&taskc->cpus_allowed, 0, scx_bpf_nr_cids());
	bpf_rcu_read_lock();
	cmask_from_cpumask(&taskc->cpus_allowed, p->cpus_ptr);
	bpf_rcu_read_unlock();

	v = bpf_task_storage_get(&task_ctx_stor, p, NULL,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v) {
		/* push back to the free list */
		if (!qmap_spin_lock(&qa_task_lock)) {
			taskc->next_free = qa.task_free_head;
			qa.task_free_head = taskc;
			bpf_res_spin_unlock(&qa_task_lock);
		}
		return -ENOMEM;
	}
	v->taskc = taskc;
	return 0;
}

void BPF_STRUCT_OPS(qmap_exit_task, struct task_struct *p,
		    struct scx_exit_task_args *args)
{
	struct task_ctx_stor_val *v;
	task_ctx_t *taskc;

	v = bpf_task_storage_get(&task_ctx_stor, p, NULL, 0);
	if (!v || !v->taskc)
		return;
	taskc = v->taskc;
	v->taskc = NULL;

	if (qmap_spin_lock(&qa_task_lock))
		return;
	taskc->next_free = qa.task_free_head;
	qa.task_free_head = taskc;
	bpf_res_spin_unlock(&qa_task_lock);
}

void BPF_STRUCT_OPS(qmap_dump, struct scx_dump_ctx *dctx)
{
	task_ctx_t *taskc;
	s32 i;

	QMAP_TOUCH_ARENA();

	if (suppress_dump)
		return;

	/*
	 * Walk the queue lists without locking - kfunc calls (scx_bpf_dump)
	 * aren't in the verifier's kfunc_spin_allowed() list so we can't hold
	 * a lock and dump. Best-effort; racing may print stale tids but the
	 * walk is bounded by bpf_repeat() so it always terminates.
	 */
	bpf_for(i, 0, 5) {
		scx_bpf_dump("QMAP FIFO[%d]:", i);
		taskc = qa.fifos[i].head;
		bpf_repeat(4096) {
			if (!taskc)
				break;
			scx_bpf_dump(" %d:%llu", taskc->pid, taskc->tid);
			taskc = taskc->q_next;
		}
		scx_bpf_dump("\n");
	}
}

void BPF_STRUCT_OPS(qmap_dump_cid, struct scx_dump_ctx *dctx, s32 cid, bool idle)
{
	struct cpu_ctx __arena *cpuc = &qa.cpu_ctxs[cid];

	if (suppress_dump || idle)
		return;

	scx_bpf_dump("QMAP: dsp_idx=%llu dsp_cnt=%llu avg_weight=%u cpuperf_target=%u",
		     cpuc->dsp_idx, cpuc->dsp_cnt, cpuc->avg_weight,
		     cpuc->cpuperf_target);
}

void BPF_STRUCT_OPS(qmap_dump_task, struct scx_dump_ctx *dctx, struct task_struct *p)
{
	struct task_ctx_stor_val *v;
	task_ctx_t *taskc;

	QMAP_TOUCH_ARENA();

	if (suppress_dump)
		return;
	v = bpf_task_storage_get(&task_ctx_stor, p, NULL, 0);
	if (!v || !v->taskc)
		return;
	taskc = v->taskc;

	scx_bpf_dump("QMAP: force_local=%d core_sched_seq=%llu",
		     taskc->force_local, taskc->core_sched_seq);
}

s32 BPF_STRUCT_OPS(qmap_cpuctl_init, struct cgroup *cgrp, struct scx_cgroup_init_args *args)
{
	QMAP_TOUCH_ARENA();

	if (print_msgs)
		bpf_printk("CGRP INIT %llu weight=%u period=%lu quota=%ld burst=%lu",
			   cgrp->kn->id, args->weight, args->bw_period_us,
			   args->bw_quota_us, args->bw_burst_us);

	if (qa.inject_mode == QMAP_INJ_CGRP_INIT_FAIL) {
		char name[7] = {};

		bpf_probe_read_kernel_str(name, sizeof(name), cgrp->kn->name);
		if (!bpf_strncmp(name, 6, "qmfail"))
			return -ENOMEM;
	}

	return 0;
}

static void redistribute(void);

void BPF_STRUCT_OPS(qmap_cpuctl_set_weight, struct cgroup *cgrp, u32 weight)
{
	u64 cgid = cgrp->kn->id;
	s32 i;

	QMAP_TOUCH_ARENA();

	if (print_msgs)
		bpf_printk("CGRP SET %llu weight=%u", cgid, weight);

	/*
	 * Knobs belong to the parent, so this op carries the child subs'
	 * attach point weights. Adjust the matching sub's share of the cid
	 * partition. Other cgroups don't participate in the split.
	 */
	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (qa.sub_sched_ctxs[i].cgroup_id != cgid)
			continue;
		if (qa.sub_sched_ctxs[i].weight != weight) {
			qa.sub_sched_ctxs[i].weight = weight;
			redistribute();
		}
		break;
	}
}

void BPF_STRUCT_OPS(qmap_cpuctl_set_bandwidth, struct cgroup *cgrp, u64 period_us,
		    u64 quota_us, u64 burst_us)
{
	if (print_msgs)
		bpf_printk("CGRP SET %llu period=%lu quota=%ld burst=%lu",
			   cgrp->kn->id, period_us, quota_us, burst_us);
}

void BPF_STRUCT_OPS(qmap_cpuctl_move, struct task_struct *p, struct cgroup *from,
		    struct cgroup *to)
{
	if (print_msgs)
		bpf_printk("CGRP MOVE %d %llu -> %llu",
			   p->pid, from->kn->id, to->kn->id);
}

void BPF_STRUCT_OPS(qmap_update_idle, s32 cid, bool idle)
{
	QMAP_TOUCH_ARENA();

	/*
	 * The kernel delivers update_idle() for every cid this node holds
	 * SCX_CAP_BASE on. Track every cid's idle state regardless of
	 * delegation: the direct-dispatch pick masks idle_cids with self_cids
	 * at selection, so a cid already idle when it returns to self needs no
	 * reseed here.
	 */
	if (idle)
		cmask_set(cid, &qa.idle_cids.mask);
	else
		cmask_clear(cid, &qa.idle_cids.mask);
}

void BPF_STRUCT_OPS(qmap_set_cmask, struct task_struct *p,
		    const struct scx_cmask *cmask_in)
{
	struct scx_cmask __arena *cmask = (struct scx_cmask __arena *)(long)cmask_in;
	task_ctx_t *taskc;

	taskc = lookup_task_ctx(p);
	if (!taskc)
		return;
	cmask_copy(&taskc->cpus_allowed, cmask);
}

struct monitor_timer {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct monitor_timer);
} monitor_timer SEC(".maps");

/*
 * Aggregate cidperf across the first nr_online_cids cids. Post-hotplug
 * the first-N-are-online invariant drifts, so some cap/cur values may
 * be stale. For this demo monitor that's fine; the scheduler exits on
 * the enable-time hotplug_seq mismatch and userspace restarts, which
 * rebuilds the layout.
 */
static void monitor_cpuperf(void)
{
	u32 nr_online = scx_bpf_nr_online_cids();
	u64 cap_sum = 0, cur_sum = 0, cur_min = SCX_CPUPERF_ONE, cur_max = 0;
	u64 target_sum = 0, target_min = SCX_CPUPERF_ONE, target_max = 0;
	s32 cid;

	QMAP_TOUCH_ARENA();

	bpf_for(cid, 0, nr_online) {
		struct cpu_ctx __arena *cpuc = &qa.cpu_ctxs[cid];
		u32 cap = scx_bpf_cidperf_cap(cid);
		u32 cur = scx_bpf_cidperf_cur(cid);
		u32 target;

		cur_min = cur < cur_min ? cur : cur_min;
		cur_max = cur > cur_max ? cur : cur_max;

		cur_sum += (u64)cur * cap / SCX_CPUPERF_ONE;
		cap_sum += cap;

		target = cpuc->cpuperf_target;
		target_sum += target;
		target_min = target < target_min ? target : target_min;
		target_max = target > target_max ? target : target_max;
	}

	if (!nr_online || !cap_sum)
		return;

	qa.cpuperf_min = cur_min;
	qa.cpuperf_avg = cur_sum * SCX_CPUPERF_ONE / cap_sum;
	qa.cpuperf_max = cur_max;

	qa.cpuperf_target_min = target_min;
	qa.cpuperf_target_avg = target_sum / nr_online;
	qa.cpuperf_target_max = target_max;
}

/*
 * Dump the currently queued tasks in the shared DSQ to demonstrate the usage of
 * scx_bpf_dsq_nr_queued() and DSQ iterator. Raise the dispatch batch count to
 * see meaningful dumps in the trace pipe.
 */
static void dump_shared_dsq(void)
{
	struct task_struct *p;
	s32 nr;

	if (!(nr = scx_bpf_dsq_nr_queued(SHARED_DSQ)))
		return;

	bpf_printk("Dumping %d tasks in SHARED_DSQ in reverse order", nr);

	bpf_rcu_read_lock();
	bpf_for_each(scx_dsq, p, SHARED_DSQ, SCX_DSQ_ITER_REV)
		bpf_printk("%s[%d]", p->comm, p->pid);
	bpf_rcu_read_unlock();
}

static int monitor_timerfn(void *map, int *key, struct bpf_timer *timer)
{
	bpf_rcu_read_lock();
	scan_shared_dsq(true);
	bpf_rcu_read_unlock();

	monitor_cpuperf();

	if (print_dsqs_and_events) {
		struct scx_event_stats events;

		dump_shared_dsq();

		__COMPAT_scx_bpf_events(&events, sizeof(events));

		bpf_printk("%35s: %lld", "SCX_EV_SELECT_CPU_FALLBACK",
			   scx_read_event(&events, SCX_EV_SELECT_CPU_FALLBACK));
		bpf_printk("%35s: %lld", "SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE",
			   scx_read_event(&events, SCX_EV_DISPATCH_LOCAL_DSQ_OFFLINE));
		bpf_printk("%35s: %lld", "SCX_EV_DISPATCH_KEEP_LAST",
			   scx_read_event(&events, SCX_EV_DISPATCH_KEEP_LAST));
		bpf_printk("%35s: %lld", "SCX_EV_ENQ_SKIP_EXITING",
			   scx_read_event(&events, SCX_EV_ENQ_SKIP_EXITING));
		bpf_printk("%35s: %lld", "SCX_EV_REFILL_SLICE_DFL",
			   scx_read_event(&events, SCX_EV_REFILL_SLICE_DFL));
		bpf_printk("%35s: %lld", "SCX_EV_BYPASS_DURATION",
			   scx_read_event(&events, SCX_EV_BYPASS_DURATION));
		bpf_printk("%35s: %lld", "SCX_EV_BYPASS_DISPATCH",
			   scx_read_event(&events, SCX_EV_BYPASS_DISPATCH));
		bpf_printk("%35s: %lld", "SCX_EV_BYPASS_ACTIVATE",
			   scx_read_event(&events, SCX_EV_BYPASS_ACTIVATE));
	}

	bpf_timer_start(timer, ONE_SEC_IN_NS, 0);
	return 0;
}

struct lowpri_timer {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct lowpri_timer);
} lowpri_timer SEC(".maps");

/*
 * Nice 19 tasks are put into the lowpri DSQ. Every 10ms, reenq is triggered and
 * the tasks are transferred to SHARED_DSQ.
 */
static int lowpri_timerfn(void *map, int *key, struct bpf_timer *timer)
{
	scx_bpf_dsq_reenq(LOWPRI_DSQ, 0);
	bpf_timer_start(timer, LOWPRI_INTV_NS, 0);
	return 0;
}

struct round_robin_timer {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, struct round_robin_timer);
} round_robin_timer SEC(".maps");

/*
 * Partition update synchronization. qa.part can be written from concurrent
 * contexts. This single-runner guard admits one writer at a time without
 * holding a lock across the grant/revoke kfuncs. part_pending coalesces
 * repartition requests that arrive while it is held.
 *
 * They live in .bss, not the arena: rr_advance() runs from a bpf_timer
 * callback, where the verifier rejects atomic ops on arena memory.
 */
static u64 part_busy;
static u64 part_pending;

static bool part_try_start(void)
{
	/* set busy, report whether it was previously clear (we acquired it) */
	return !__sync_fetch_and_or(&part_busy, 1);
}

static void part_end(void)
{
	__sync_fetch_and_and(&part_busy, 0);
}

/*
 * compute_partition() scratch.
 *
 * The excl-held cids are handed out in cid order: position 0..nr_excl-1 over
 * the held cids is split into contiguous ranges, one per participant that gets
 * at least one excl cid. Range k is owned by cp_range_owner[k] and ends at the
 * cumulative position cp_range_end[k].
 */
static s32 cp_range_owner[MAX_PARTS];	/* exclusive range k: its owner id ... */
static s32 cp_range_end[MAX_PARTS];	/* ... and the cumulative position it ends at */

/* a participant in the partition: self or an attached child */
struct participant {
	s32 slot;	/* child slot, or CID_SELF */
	u32 weight;	/* cpu.weight */
};

/**
 * place_one - assign one excl-held cid to its owner
 * @cid: the excl-held cid to place
 * @n: its position among the excl-held cids, in [0, nr_excl)
 * @total_excl:	how many positions are owned exclusively (the rest are shared)
 *
 * Position @n below @total_excl is owned exclusively. It falls in the range
 * whose cumulative end it is under, owned by cp_range_owner[]. A position at or
 * above @total_excl is the rounding leftover which joins the shared pool.
 *
 * A separate __noinline function to help verification.
 */
__noinline int place_one(s32 cid, s32 n, s32 total_excl)
{
	s32 owner = CID_SELF, i, s;

	if (cid < 0 || cid >= SCX_QMAP_MAX_CPUS || n < 0 || n >= SCX_QMAP_MAX_CPUS ||
	    total_excl < 0) {
		scx_bpf_error("-ERANGE");
		return 0;
	}

	if (n < total_excl) {
		for (i = 0; i < MAX_PARTS; i++) {
			if (n < cp_range_end[i]) {
				owner = cp_range_owner[i];
				break;
			}
		}
		qa.part.cid_owner[cid] = owner;
	} else {
		s = n - total_excl;
		if (s < 0 || s >= MAX_PARTS) {
			scx_bpf_error("-ERANGE");
			return 0;
		}
		qa.part.shared_cids[s] = cid;
		/* time-shared: dispatch resolves the live holder via rr_pos */
		qa.part.cid_owner[cid] = CID_SHARED;
	}
	return 0;
}

/**
 * compute_partition - build the cid partition from this node's held caps
 *
 * Decide each cid's owner, the shared pool and the rr rotation. __noinline to
 * help verification. See the comment at the top of the file.
 */
__noinline void compute_partition(void)
{
	s32 nr_cids = qa.nr_cids;
	s32 nr_excl, total_excl = 0, nr_rr = 0;
	s32 sum_w, i, cid, n = 0, share, self_w;
	u64 cgid_snap[MAX_SUB_SCHEDS];
	s32 w_snap[MAX_SUB_SCHEDS];

	if (nr_cids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("-ERANGE");
		return;
	}

	/* find out the cids we hold */
	scx_bpf_sub_caps(0, SCX_CAP_ENQ, &qa.held_excl.mask);
	scx_bpf_sub_caps(0, SCX_CAP_ENQ_IMMED, &qa.held_shared.mask);
	cmask_andnot(&qa.held_shared.mask, &qa.held_excl.mask);	/* held only as ENQ_IMMED */

	qa.part.nr_shared = 0;
	qa.part.nr_rr = 0;
	qa.part.rr_pos = 0;

	nr_excl = cmask_weight(&qa.held_excl.mask);
	qa.part.nr_excl = nr_excl;

	/* no excl cid: held_shared stays self-local, the rest unheld */
	if (!nr_excl) {
		bpf_for(cid, 0, nr_cids) {
			if (cmask_test(cid, &qa.held_shared.mask))
				qa.part.cid_owner[cid] = CID_SELF;
			else
				qa.part.cid_owner[cid] = CID_NONE;
		}
		return;
	}

	/*
	 * Snapshot membership and weights so the sum_w and share loops agree. A
	 * mid-compute change would otherwise wrap nr_shared negative. The self
	 * weight is fixed at the default: a cgroup's weight is its parent's
	 * knob, not the scheduler's own business.
	 */
	self_w = 100;
	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		cgid_snap[i] = qa.sub_sched_ctxs[i].cgroup_id;
		w_snap[i] = cgid_snap[i] ? (qa.sub_sched_ctxs[i].weight ?: 100) : 0;
	}

	/*
	 * Participants are self plus each child. Give each a fixed range/rr
	 * slot: self at slot 0, child i at slot i+1.
	 *
	 * sum_w totals every participant's weight.
	 */
	sum_w = self_w;
	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		barrier_var(sum_w);
		sum_w += w_snap[i];
	}

	/*
	 * Split [0, nr_excl) into one contiguous range per participant, each
	 * the floor of its weight share. cp_range_owner[]/cp_range_end[] record
	 * each range's owner and cumulative end, total_excl counts the
	 * exclusive slots, and the rest (nr_excl - total_excl) are shared.
	 * rr_slots[] lists every participant for the round-robin.
	 */
	share = (u64)nr_excl * self_w / sum_w;
	total_excl += share;
	cp_range_owner[0] = CID_SELF;
	cp_range_end[0] = total_excl;
	qa.part.rr_slots[nr_rr++] = 0;		/* self holds slot 0 (cgid 0 = no grant) */

	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		u64 cgid = cgid_snap[i];
		s32 w = w_snap[i];

		barrier_var(total_excl);
		share = (u64)nr_excl * w / sum_w;
		total_excl += share;
		cp_range_owner[i + 1] = cgid ? i : CID_NONE;
		cp_range_end[i + 1] = total_excl;

		if (cgid) {
			barrier_var(nr_rr);
			if (nr_rr < 0 || nr_rr >= MAX_PARTS) {
				scx_bpf_error("-ERANGE");
				return;
			}
			qa.part.rr_slots[nr_rr++] = cgid;
		}
	}

	/* assign each cid: held-excl by position, the rest self/none */
	bpf_for(cid, 0, nr_cids) {
		if (cmask_test(cid, &qa.held_excl.mask)) {
			place_one(cid, n, total_excl);
			n++;
			barrier_var(n);
		} else if (cmask_test(cid, &qa.held_shared.mask)) {
			qa.part.cid_owner[cid] = CID_SELF;	/* time-share, self-local */
		} else {
			qa.part.cid_owner[cid] = CID_NONE;	/* not held */
		}
	}

	qa.part.nr_shared = nr_excl - total_excl;
	qa.part.nr_rr = nr_rr;
}

/*
 * Charge elapsed wall time to each cid's current owner. Runs under the
 * partition guard before every ownership change and from the stats flush, so
 * alloc_ns[] reflects the layout that was in effect. Shared-pool time is
 * charged to the live round-robin holder.
 */
static __noinline void account_alloc(void)
{
	u64 now = bpf_ktime_get_ns();
	s32 rr_owner = CID_SELF;
	s32 nr_cids = qa.nr_cids;
	u64 delta;
	s32 cid, i;

	if (nr_cids < 0 || nr_cids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("-ERANGE");
		return;
	}

	/* first call starts the clock */
	if (!qa.alloc_ts) {
		qa.alloc_ts = now;
		return;
	}
	delta = now - qa.alloc_ts;
	qa.alloc_ts = now;
	qa.alloc_window_ns += delta;

	/* resolve the live shared-pool holder to an owner id */
	if (qa.part.nr_shared && qa.part.nr_rr) {
		u32 pos = qa.part.rr_pos;
		u64 cgid = pos < MAX_PARTS ? qa.part.rr_slots[pos] : 0;

		if (cgid) {
			rr_owner = CID_NONE;
			bpf_for(i, 0, MAX_SUB_SCHEDS)
				if (qa.sub_sched_ctxs[i].cgroup_id == cgid)
					rr_owner = i;
		}
	}

	bpf_for(cid, 0, nr_cids) {
		s32 owner = qa.part.cid_owner[cid];

		if (owner == CID_SHARED)
			owner = rr_owner;
		if (owner >= 0 && owner < MAX_SUB_SCHEDS)
			qa.alloc_ns[owner] += delta;
		else if (owner == CID_SELF)
			qa.self_alloc_ns += delta;
	}
}

/*
 * apply_partition - execute the plan compute_partition() built
 *
 * Turn the owner map into the per-child, shared and self cmasks and issue the
 * grant/revoke kfuncs as a delta against each child's previous grant. If no
 * excl cid, evict every child.
 */
__noinline void apply_partition(void)
{
	s32 nr_cids = qa.nr_cids;
	s32 nr_shared = qa.part.nr_shared;
	s32 i, cid;

	if (nr_cids < 0 || nr_cids > SCX_QMAP_MAX_CPUS ||
	    nr_shared < 0 || nr_shared > MAX_PARTS) {
		scx_bpf_error("-ERANGE");
		return;
	}

	/* no excl cpu: run own tasks on the held shares, evict children */
	if (!qa.part.nr_excl) {
		cmask_copy(&qa.self_cids.mask, &qa.held_shared.mask);
		bpf_for(i, 0, MAX_SUB_SCHEDS)
			if (qa.sub_sched_ctxs[i].cgroup_id)
				scx_bpf_sub_kill(qa.sub_sched_ctxs[i].cgroup_id,
						 "parent holds no excl cpu to distribute");
		return;
	}

	/*
	 * Snapshot the old pool. The per-child revoke below clears ENQ_IMMED on
	 * the previously-granted pool, so a cid that left the pool (now a
	 * sibling's excl) doesn't keep a stale ENQ_IMMED on its last holder.
	 */
	cmask_copy(&qa.prev_rr_cids.mask, &qa.rr_cids.mask);

	/* turn the owner map into the rr pool, per-child excl, and self sets */
	cmask_init(&qa.rr_cids.mask, 0, nr_cids);
	cmask_init(&qa.self_cids.mask, 0, nr_cids);

	/* snapshot each child's grant, then rebuild the new sets below */
	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		cmask_copy(&qa.sub_sched_ctxs[i].prev_granted.mask,
			   &qa.sub_sched_ctxs[i].granted_cids.mask);
		cmask_init(&qa.sub_sched_ctxs[i].granted_cids.mask, 0, nr_cids);
	}

	bpf_for(i, 0, nr_shared)
		cmask_set(qa.part.shared_cids[i], &qa.rr_cids.mask);
	bpf_for(cid, 0, nr_cids) {
		s32 o = qa.part.cid_owner[cid];

		if (cmask_test(cid, &qa.rr_cids.mask))
			continue;
		if (o >= 0 && o < MAX_SUB_SCHEDS)
			cmask_set(cid, &qa.sub_sched_ctxs[o].granted_cids.mask);
		else if (o == CID_SELF)
			cmask_set(cid, &qa.self_cids.mask);
	}

	/*
	 * Apply each child's exclusive cids as a delta against its previous
	 * grant. Separately clear the previous shared grant (ENQ_IMMED on the
	 * old pool), covering cids still pooled and cids that left for a
	 * sibling's excl. The current holder is granted the new pool below.
	 */
	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		struct sub_sched_ctx __arena *ssc = &qa.sub_sched_ctxs[i];
		u64 cgid = ssc->cgroup_id;

		if (!cgid)
			continue;

		cmask_copy(&qa.to_revoke_cids.mask, &ssc->prev_granted.mask);
		cmask_andnot(&qa.to_revoke_cids.mask, &ssc->granted_cids.mask);
		cmask_copy(&qa.to_grant_cids.mask, &ssc->granted_cids.mask);
		cmask_andnot(&qa.to_grant_cids.mask, &ssc->prev_granted.mask);

		scx_bpf_sub_revoke(cgid, SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
				   &qa.prev_rr_cids.mask);
		scx_bpf_sub_revoke(cgid, SCX_CAP_ENQ | SCX_CAP_PREEMPT |
				   SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
				   &qa.to_revoke_cids.mask);
		scx_bpf_sub_grant(cgid, SCX_CAP_ENQ | SCX_CAP_PREEMPT |
				  SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
				  &qa.to_grant_cids.mask, NULL);
	}

	/* the current holder of the shared pool gets ENQ_IMMED on all of it */
	if (nr_shared) {
		s32 pos = qa.part.rr_pos;
		u64 holder_cgid;

		if (pos < 0 || pos >= MAX_PARTS) {
			scx_bpf_error("-ERANGE");
			return;
		}

		holder_cgid = qa.part.rr_slots[pos];	/* 0 = self, nothing to grant */
		if (holder_cgid)
			scx_bpf_sub_grant(holder_cgid,
					  SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
					  &qa.rr_cids.mask, NULL);
	}
}

/*
 * Recompute the split off the node's held caps and apply it. The contexts this
 * runs from (the sub-sched and cgroup callbacks, the rr timer) are not
 * serialized by the kernel, so a single runner does the work. A caller that
 * finds the guard held leaves part_pending set; the holder drains it before
 * releasing, with the rr timer as a backstop.
 */
static void redistribute(void)
{
	s32 i;

	__sync_fetch_and_or(&part_pending, 1);

	if (!part_try_start())
		return;

	bpf_for(i, 0, 1024) {
		__sync_fetch_and_and(&part_pending, 0);
		/* charge elapsed time to the current partition before rebuilding it */
		account_alloc();
		compute_partition();
		apply_partition();
		if (!__sync_fetch_and_or(&part_pending, 0))
			break;
	}

	part_end();
}

/*
 * Userspace pokes this (PROG_RUN) to bring alloc_ns[] current before reading
 * it for the stats display. Skipping when the partition guard is held is
 * fine - alloc_ts is untouched, so the elapsed time is charged next time.
 */
SEC("syscall")
int flush_alloc(void *ctx)
{
	if (part_try_start()) {
		account_alloc();
		part_end();
	}
	return 0;
}

/*
 * Hand the shared pool to the next participant in the rotation. Self's turn
 * just revokes the pool back to this sched. A child's turn grants it ENQ_IMMED
 * on the entire pool. As only excl-held cids are time-shared, a wall-clock
 * rotation works. Driven by the round-robin timer.
 */
static void rr_advance(void)
{
	s32 nr_shared, old_pos, new_pos;
	u64 old_cgid, new_cgid;
	u32 nr_rr;		/* unsigned for % */

	/* a redistribute holds the partition and rebuilds the pool, so skip */
	if (!part_try_start())
		return;

	nr_rr = qa.part.nr_rr;
	nr_shared = qa.part.nr_shared;

	if (nr_shared < 0 || nr_shared > MAX_PARTS) {
		scx_bpf_error("-ERANGE");
		return;
	}

	if (nr_shared && nr_rr >= 2) {
		/* close out the outgoing holder's pool time */
		account_alloc();

		old_pos = qa.part.rr_pos;
		new_pos = (old_pos + 1) % nr_rr;
		old_cgid = qa.part.rr_slots[old_pos];
		new_cgid = qa.part.rr_slots[new_pos];
		qa.part.rr_pos = new_pos;

		/*
		 * Move the ENQ_IMMED cap to the next participant. The shared
		 * cids stay marked CID_SHARED. qmap_dispatch() resolves the
		 * live holder via rr_pos without the guard, so a dispatch
		 * racing this handoff may reenqueue a task once. Harmless for a
		 * time-share.
		 */
		if (old_cgid)
			scx_bpf_sub_revoke(old_cgid,
					   SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
					   &qa.rr_cids.mask);
		if (new_cgid)
			scx_bpf_sub_grant(new_cgid,
					  SCX_CAP_ENQ_IMMED | SCX_CAP_PERF,
					  &qa.rr_cids.mask, NULL);
	}

	part_end();

	/* a resplit queued while we held the guard supersedes this rotation */
	if (__sync_fetch_and_or(&part_pending, 0))
		redistribute();
}

/* advance the time-shared cid pool every round_robin_ns */
static int round_robin_timerfn(void *map, int *key, struct bpf_timer *timer)
{
	rr_advance();
	bpf_timer_start(timer, round_robin_ns, 0);
	return 0;
}

/*
 * Custom cid layout for the cid-override test. On invalid input the kfunc
 * scx_error()s and aborts the scheduler.
 */
s32 BPF_STRUCT_OPS_SLEEPABLE(qmap_init_cids)
{
	u32 nr_cpu_ids = scx_bpf_nr_cpu_ids();

	if (!cid_override_mode)
		return 0;

	/* the arena arrays are sized SCX_QMAP_MAX_CPUS */
	if (nr_cpu_ids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("nr_cpu_ids=%u exceeds SCX_QMAP_MAX_CPUS=%d",
			      nr_cpu_ids, SCX_QMAP_MAX_CPUS);
		return -EINVAL;
	}

	scx_bpf_cid_override(qa.cid_override_cpu_to_cid, nr_cpu_ids,
			     qa.cid_override_shard_start, cid_override_nr_shards);
	return 0;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(qmap_init)
{
	u8 __arena *slab;
	u32 nr_pages, key = 0, i;
	u32 nr_cids, nr_cpu_ids;
	struct bpf_timer *timer;
	s32 ret;

	nr_cids = scx_bpf_nr_cids();
	nr_cpu_ids = scx_bpf_nr_cpu_ids();

	if (nr_cids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("nr_cids=%u exceeds SCX_QMAP_MAX_CPUS=%d",
			      nr_cids, SCX_QMAP_MAX_CPUS);
		return -EINVAL;
	}
	if (nr_cpu_ids > SCX_QMAP_MAX_CPUS) {
		scx_bpf_error("nr_cpu_ids=%u exceeds SCX_QMAP_MAX_CPUS=%d",
			      nr_cpu_ids, SCX_QMAP_MAX_CPUS);
		return -EINVAL;
	}

	/*
	 * Allocate the task_ctx slab in arena and thread the entire slab onto
	 * the free list. max_tasks is set by userspace before load. Each entry
	 * is TASK_CTX_STRIDE bytes - task_ctx's trailing cpus_allowed flex
	 * array extends into the stride tail.
	 */
	if (!max_tasks) {
		scx_bpf_error("max_tasks must be > 0");
		return -EINVAL;
	}

	nr_pages = (max_tasks * TASK_CTX_STRIDE + PAGE_SIZE - 1) / PAGE_SIZE;
	slab = bpf_arena_alloc_pages(&arena, NULL, nr_pages, NUMA_NO_NODE, 0);
	if (!slab) {
		scx_bpf_error("failed to allocate task_ctx slab");
		return -ENOMEM;
	}
	qa.task_ctxs = (task_ctx_t *)slab;

	bpf_for(i, 0, 5)
		qa.fifos[i].idx = i;

	bpf_for(i, 0, max_tasks) {
		task_ctx_t *cur = (task_ctx_t *)(slab + i * TASK_CTX_STRIDE);
		task_ctx_t *next = (i + 1 < max_tasks) ?
			(task_ctx_t *)(slab + (i + 1) * TASK_CTX_STRIDE) : NULL;
		cur->next_free = next;
	}
	qa.task_free_head = (task_ctx_t *)slab;

	/* cache the cid count, trusted to be <= SCX_QMAP_MAX_CPUS hereafter */
	qa.nr_cids = nr_cids;

	/* cmasks are embedded in qa, so they only need initializing */
	cmask_init(&qa.idle_cids.mask, 0, nr_cids);
	cmask_init(&qa.rr_cids.mask, 0, nr_cids);
	cmask_init(&qa.prev_rr_cids.mask, 0, nr_cids);
	cmask_init(&qa.self_cids.mask, 0, nr_cids);
	cmask_init(&qa.to_revoke_cids.mask, 0, nr_cids);
	cmask_init(&qa.to_grant_cids.mask, 0, nr_cids);
	cmask_init(&qa.held_excl.mask, 0, nr_cids);
	cmask_init(&qa.held_shared.mask, 0, nr_cids);

	scx_bpf_sub_caps(0, SCX_CAP_ENQ, &qa.held_excl.mask);
	scx_bpf_sub_caps(0, SCX_CAP_ENQ_IMMED, &qa.held_shared.mask);
	cmask_andnot(&qa.held_shared.mask, &qa.held_excl.mask);

	bpf_for(i, 0, MAX_SUB_SCHEDS) {
		cmask_init(&qa.sub_sched_ctxs[i].granted_cids.mask, 0, nr_cids);
		cmask_init(&qa.sub_sched_ctxs[i].prev_granted.mask, 0, nr_cids);
	}

	/*
	 * The root starts holding every cid. qmap_sub_ecaps_updated() maintains
	 * per-cid shared state as effective caps settle, and redistribute()
	 * rebuilds owner and self from held caps. A non-root node starts with
	 * nothing.
	 */
	bpf_for(i, 0, nr_cids) {
		if (!sub_cgroup_id) {
			cmask_set(i, &qa.self_cids.mask);
			qa.part.cid_owner[i] = CID_SELF;
		} else {
			qa.part.cid_owner[i] = CID_NONE;
		}
	}
	qa.part.nr_shared = 0;

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret) {
		scx_bpf_error("failed to create DSQ %d (%d)", SHARED_DSQ, ret);
		return ret;
	}

	ret = scx_bpf_create_dsq(HIGHPRI_DSQ, -1);
	if (ret) {
		scx_bpf_error("failed to create DSQ %d (%d)", HIGHPRI_DSQ, ret);
		return ret;
	}

	ret = scx_bpf_create_dsq(LOWPRI_DSQ, -1);
	if (ret)
		return ret;

	timer = bpf_map_lookup_elem(&monitor_timer, &key);
	if (!timer)
		return -ESRCH;
	bpf_timer_init(timer, &monitor_timer, CLOCK_MONOTONIC);
	bpf_timer_set_callback(timer, monitor_timerfn);
	ret = bpf_timer_start(timer, ONE_SEC_IN_NS, 0);
	if (ret)
		return ret;

	if (__COMPAT_has_generic_reenq()) {
		/* see lowpri_timerfn() */
		timer = bpf_map_lookup_elem(&lowpri_timer, &key);
		if (!timer)
			return -ESRCH;
		bpf_timer_init(timer, &lowpri_timer, CLOCK_MONOTONIC);
		bpf_timer_set_callback(timer, lowpri_timerfn);
		ret = bpf_timer_start(timer, LOWPRI_INTV_NS, 0);
		if (ret)
			return ret;
	}

	/* sub-sched: drive the boundary-cid round-robin from a bpf timer */
	timer = bpf_map_lookup_elem(&round_robin_timer, &key);
	if (!timer)
		return -ESRCH;
	bpf_timer_init(timer, &round_robin_timer, CLOCK_MONOTONIC);
	bpf_timer_set_callback(timer, round_robin_timerfn);
	ret = bpf_timer_start(timer, round_robin_ns, 0);
	if (ret)
		return ret;

	return 0;
}

void BPF_STRUCT_OPS(qmap_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

/*
 * Seed a new sub slot with the cgroup's current weight. The kernel delivers
 * ops.cpuctl_set_weight() only on value-changing writes, so a weight set
 * before the sub attached would otherwise go unnoticed.
 */
static u32 cgrp_cur_weight(u64 cgid)
{
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;
	u32 weight = 100;

	cgrp = bpf_cgroup_from_id(cgid);
	if (!cgrp)
		return weight;

	css = BPF_CORE_READ(cgrp, subsys[cpu_cgrp_id]);
	if (css) {
		struct task_group *tg = container_of(css, struct task_group, css);
		u32 w = BPF_CORE_READ(tg, scx.weight);

		if (w)
			weight = w;
	}
	bpf_cgroup_release(cgrp);
	return weight;
}

s32 BPF_STRUCT_OPS(qmap_sub_attach, struct scx_sub_attach_args *args)
{
	s32 i;

	/* as long as there is at least one excl cpu, children can attach */
	if (!cmask_weight(&qa.held_excl.mask))
		return -ENOSPC;

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (qa.sub_sched_ctxs[i].cgroup_id)
			continue;

		qa.sub_sched_ctxs[i].cgroup_id = args->ops->sub_cgroup_id;
		qa.sub_sched_ctxs[i].weight = cgrp_cur_weight(args->ops->sub_cgroup_id);
		qa.nr_sub_scheds++;
		bpf_printk("attaching sub-sched[%d] on %s", i, args->cgroup_path);
		redistribute();
		return 0;
	}

	return -ENOSPC;
}

void BPF_STRUCT_OPS(qmap_sub_detach, struct scx_sub_detach_args *args)
{
	s32 i;

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (qa.sub_sched_ctxs[i].cgroup_id != args->ops->sub_cgroup_id)
			continue;

		qa.sub_sched_ctxs[i].cgroup_id = 0;
		qa.sub_sched_ctxs[i].weight = 100;
		cmask_init(&qa.sub_sched_ctxs[i].granted_cids.mask, 0, qa.nr_cids);
		qa.nr_sub_scheds--;
		bpf_printk("detaching sub-sched[%d] on %s", i, args->cgroup_path);
		redistribute();
		break;
	}
}

void BPF_STRUCT_OPS(qmap_sub_caps_updated, const struct scx_cmask *cmask, u64 caps)
{
	/* our held caps changed, redistribute */
	redistribute();
}

void BPF_STRUCT_OPS(qmap_sub_ecaps_updated, s32 cid, u64 before, u64 after)
{
	/*
	 * Effective caps updated. Track which cids hold shared caps so a self
	 * task placed there enqueues IMMED.
	 */
	if (after & SCX_CAP_ENQ_IMMED)
		qa.cid_shared[cid] = (after & SCX_CAP_ENQ) ? 0 : 1;
	else
		qa.cid_shared[cid] = 0;
}

SCX_OPS_CID_DEFINE(qmap_ops,
	       .flags			= SCX_OPS_ENQ_EXITING | SCX_OPS_TID_TO_TASK,
	       .select_cid		= (void *)qmap_select_cid,
	       .enqueue			= (void *)qmap_enqueue,
	       .dequeue			= (void *)qmap_dequeue,
	       .dispatch		= (void *)qmap_dispatch,
	       .tick			= (void *)qmap_tick,
	       .core_sched_before	= (void *)qmap_core_sched_before,
	       .set_cmask		= (void *)qmap_set_cmask,
	       .update_idle		= (void *)qmap_update_idle,
	       .init_task		= (void *)qmap_init_task,
	       .exit_task		= (void *)qmap_exit_task,
	       .dump			= (void *)qmap_dump,
	       .dump_cid		= (void *)qmap_dump_cid,
	       .dump_task		= (void *)qmap_dump_task,
	       .cpuctl_init		= (void *)qmap_cpuctl_init,
	       .cpuctl_set_weight	= (void *)qmap_cpuctl_set_weight,
	       .cpuctl_set_bandwidth	= (void *)qmap_cpuctl_set_bandwidth,
	       .cpuctl_move		= (void *)qmap_cpuctl_move,
	       .sub_attach		= (void *)qmap_sub_attach,
	       .sub_detach		= (void *)qmap_sub_detach,
	       .sub_caps_updated	= (void *)qmap_sub_caps_updated,
	       .sub_ecaps_updated	= (void *)qmap_sub_ecaps_updated,
	       .init_cids		= (void *)qmap_init_cids,
	       .init			= (void *)qmap_init,
	       .exit			= (void *)qmap_exit,
	       .timeout_ms		= 5000U,
	       .name			= "qmap");
