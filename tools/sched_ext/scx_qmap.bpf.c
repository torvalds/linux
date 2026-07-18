/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A simple five-level FIFO queue scheduler.
 *
 * There are five FIFOs implemented as arena-backed doubly-linked lists
 * threaded through per-task context. A task gets assigned to one depending on
 * its compound weight. Each CPU round robins through the FIFOs and dispatches
 * more from FIFOs with higher indices - 1 from queue0, 2 from queue1, 4 from
 * queue2 and so on.
 *
 * This scheduler demonstrates:
 *
 * - BPF-side queueing using TIDs.
 * - BPF arena for scheduler state.
 * - Core-sched support.
 *
 * This scheduler is primarily for demonstration and testing of sched_ext
 * features and unlikely to be useful for actual workloads.
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
const volatile bool always_enq_immed;
const volatile u32 immed_stress_nth;
const volatile u32 max_tasks;

/*
 * Optional cid-override test harness. When cid_override_mode is non-zero,
 * qmap_init() calls scx_bpf_cid_override() with the caller-supplied
 * cpu_to_cid array to exercise the kfunc's acceptance and error paths.
 *
 *   0 = disabled
 *   1 = valid reverse mapping
 *   2 = invalid: duplicate cid assignment
 *   3 = invalid: out-of-range cid
 */
const volatile u32 cid_override_mode;
/*
 * Array lives in bss (writable) because scx_bpf_cid_override()'s BPF
 * verifier signature treats its len-paired pointer as read/write - rodata
 * fails verification with "write into map forbidden". Userspace populates
 * it before SCX_OPS_LOAD, same as rodata, and nothing writes it after.
 */
s32 cid_override_cpu_to_cid[SCX_QMAP_MAX_CPUS];

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

/*
 * Global idle-cid tracking, maintained via update_idle / cpu_offline and
 * scanned by the direct-dispatch path. Allocated in qmap_init() from one
 * arena page, sized to the full cid space.
 */
struct scx_cmask __arena *qa_idle_cids;

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
 * Try prev_cid, then scan taskc->cpus_allowed AND qa_idle_cids round-robin
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

	if (!always_enq_immed && p->nr_cpus_allowed == 1)
		return prev_cid;

	if (cmask_test_and_clear(prev_cid, qa_idle_cids))
		return prev_cid;

	cid = prev_cid;
	bpf_for(i, 0, IDLE_PICK_RETRIES) {
		cid = cmask_next_and_set_wrap(&taskc->cpus_allowed,
					      qa_idle_cids, cid + 1);
		barrier_var(cid);
		if (cid >= nr_cids)
			return -1;
		if (cmask_test_and_clear(cid, qa_idle_cids))
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
		__sync_fetch_and_add(&qa.nr_reenqueued, 1);
		if (scx_bpf_task_cid(p) == 0)
			__sync_fetch_and_add(&qa.nr_reenqueued_cid0, 1);
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
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice_ns, enq_flags);
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
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cid, slice_ns, enq_flags);
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
		cid = cmask_next_and_set_wrap(&taskc->cpus_allowed,
					      qa_idle_cids, 0);
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
 * To demonstrate the use of scx_bpf_dsq_move(), implement silly selective
 * priority boosting mechanism by scanning SHARED_DSQ looking for highpri tasks,
 * moving them to HIGHPRI_DSQ and then consuming them first. This makes minor
 * difference only when dsp_batch is larger than 1.
 *
 * scx_bpf_dispatch[_vtime]_from_dsq() are allowed both from ops.dispatch() and
 * non-rq-lock holding BPF programs. As demonstration, this function is called
 * from qmap_dispatch() and monitor_timerfn().
 */
static bool dispatch_highpri(bool from_timer)
{
	struct task_struct *p;
	s32 this_cid = scx_bpf_this_cid();
	u32 nr_cids = scx_bpf_nr_cids();

	/* scan SHARED_DSQ and move highpri tasks to HIGHPRI_DSQ */
	bpf_for_each(scx_dsq, p, SHARED_DSQ, 0) {
		static u64 highpri_seq;
		task_ctx_t *taskc;

		if (!(taskc = lookup_task_ctx(p)))
			return false;

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

		if (cmask_test(this_cid, &taskc->cpus_allowed))
			cid = this_cid;
		else
			cid = cmask_next_set_wrap(&taskc->cpus_allowed,
						  this_cid + 1);
		if (cid >= nr_cids)
			continue;

		if (scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_LOCAL_ON | cid,
				     SCX_ENQ_PREEMPT)) {
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
	s32 i;

	if (dispatch_highpri(false))
		return;

	if (!qa.nr_highpri_queued && scx_bpf_dsq_move_to_local(SHARED_DSQ, 0))
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
				if (dispatch_highpri(false))
					return;
				scx_bpf_dsq_move_to_local(SHARED_DSQ, 0);
				return;
			}
			if (!cpuc->dsp_cnt)
				break;
		}

		cpuc->dsp_cnt = 0;
	}

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (qa.sub_sched_cgroup_ids[i] &&
		    scx_bpf_sub_dispatch(qa.sub_sched_cgroup_ids[i]))
			return;
	}

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
static s64 task_qdist(struct task_struct *p)
{
	int idx = weight_to_idx(p->scx.weight);
	task_ctx_t *taskc;
	s64 qdist;

	taskc = lookup_task_ctx(p);
	if (!taskc)
		return 0;

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
	return task_qdist(a) > task_qdist(b);
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

s32 BPF_STRUCT_OPS(qmap_cgroup_init, struct cgroup *cgrp, struct scx_cgroup_init_args *args)
{
	if (print_msgs)
		bpf_printk("CGRP INIT %llu weight=%u period=%lu quota=%ld burst=%lu",
			   cgrp->kn->id, args->weight, args->bw_period_us,
			   args->bw_quota_us, args->bw_burst_us);
	return 0;
}

void BPF_STRUCT_OPS(qmap_cgroup_set_weight, struct cgroup *cgrp, u32 weight)
{
	if (print_msgs)
		bpf_printk("CGRP SET %llu weight=%u", cgrp->kn->id, weight);
}

void BPF_STRUCT_OPS(qmap_cgroup_set_bandwidth, struct cgroup *cgrp,
		    u64 period_us, u64 quota_us, u64 burst_us)
{
	if (print_msgs)
		bpf_printk("CGRP SET %llu period=%lu quota=%ld burst=%lu",
			   cgrp->kn->id, period_us, quota_us, burst_us);
}

void BPF_STRUCT_OPS(qmap_update_idle, s32 cid, bool idle)
{
	QMAP_TOUCH_ARENA();
	if (idle)
		cmask_set(cid, qa_idle_cids);
	else
		cmask_clear(cid, qa_idle_cids);
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
	dispatch_highpri(true);
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
	 * cid-override test hook. Must run before anything that reads the
	 * cid space (scx_bpf_nr_cids, cmask_init, etc.). On invalid input,
	 * the kfunc calls scx_error() which aborts the scheduler.
	 */
	if (cid_override_mode) {
		scx_bpf_cid_override((const s32 *)cid_override_cpu_to_cid,
				     nr_cpu_ids * sizeof(s32));
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

	/*
	 * Allocate and initialize the idle cmask. Starts empty - update_idle
	 * fills it as cpus enter idle.
	 */
	qa_idle_cids = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	if (!qa_idle_cids) {
		scx_bpf_error("failed to allocate idle cmask");
		return -ENOMEM;
	}
	cmask_init(qa_idle_cids, 0, nr_cids);

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

	return 0;
}

void BPF_STRUCT_OPS(qmap_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

s32 BPF_STRUCT_OPS(qmap_sub_attach, struct scx_sub_attach_args *args)
{
	s32 i;

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (!qa.sub_sched_cgroup_ids[i]) {
			qa.sub_sched_cgroup_ids[i] = args->ops->sub_cgroup_id;
			bpf_printk("attaching sub-sched[%d] on %s",
				   i, args->cgroup_path);
			return 0;
		}
	}

	return -ENOSPC;
}

void BPF_STRUCT_OPS(qmap_sub_detach, struct scx_sub_detach_args *args)
{
	s32 i;

	for (i = 0; i < MAX_SUB_SCHEDS; i++) {
		if (qa.sub_sched_cgroup_ids[i] == args->ops->sub_cgroup_id) {
			qa.sub_sched_cgroup_ids[i] = 0;
			bpf_printk("detaching sub-sched[%d] on %s",
				   i, args->cgroup_path);
			break;
		}
	}
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
	       .cgroup_init		= (void *)qmap_cgroup_init,
	       .cgroup_set_weight	= (void *)qmap_cgroup_set_weight,
	       .cgroup_set_bandwidth	= (void *)qmap_cgroup_set_bandwidth,
	       .sub_attach		= (void *)qmap_sub_attach,
	       .sub_detach		= (void *)qmap_sub_detach,
	       .init			= (void *)qmap_init,
	       .exit			= (void *)qmap_exit,
	       .timeout_ms		= 5000U,
	       .name			= "qmap");
