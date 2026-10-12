// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 KylinSoft Corporation. */
#include <vmlinux.h>
#include <stdbool.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

extern bool CONFIG_PREEMPT_RT __kconfig __weak;

/*
 * Bounds for the hardirq busy-wait that holds the scheduling handler
 * inside the real window: task_work_add() has published the callback,
 * but the handler's tail state transition has not run yet.
 * irq_work_single() clears BUSY only after the handler returns, so
 * while this wait runs, the context's scheduling irq_work is BUSY,
 * even though the first callback may already have completed and
 * returned the context to STANDBY.
 *
 * The wait is bounded both by a bpf_ktime_get_ns() check and by an
 * iteration budget.  The time bound is checked against a monotonic
 * clock: host vCPU preemption can stretch the wall-clock duration of
 * the window, which only makes the window longer, never shorter.
 */
#define BUSY_WAIT_NS		4000000ULL
#define BUSY_WAIT_ITERS		1000000ULL

int trigger_tid;
int target_tid;

/*
 * High 32 bits of the result slots hold the sequence, low 32 bits the
 * signed error.  BUSY_WINDOW low bits: 1 open, 2 the in-window attempt
 * was seen before the handler returned, 3 the wait bound was reached
 * without it.  Must match the test.
 */
enum task_work_busy_status {
	BUSY_ARM_SEQ,
	BUSY_WINDOW,
	BUSY_ACK,
	BUSY_GEN1_RESULT,
	BUSY_GEN2_RESULT,
	BUSY_CB1_SEQ,
	BUSY_CB2_COUNT,
	BUSY_CB2_SEQ,
	BUSY_RECOVER_SEQ,
	BUSY_LAST_WAIT_NS,
	BUSY_STATUS_MAX,
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, BUSY_STATUS_MAX);
	__type(key, int);
	__type(value, __s64);
} status SEC(".maps");

struct task_work_busy_value {
	__u32 seq;
	char data[60];
	struct bpf_task_work tw;
};

/*
 * max_entries must cover TASK_WORK_BUSY_MAX_ATTEMPTS in the test: each
 * attempt schedules into its own fresh value.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 128);
	__type(key, int);
	__type(value, struct task_work_busy_value);
} hmap SEC(".maps");

static __always_inline void set_status(int key, __s64 value)
{
	__s64 *slot;

	slot = bpf_map_lookup_elem(&status, &key);
	if (slot)
		*slot = value;
}

static __always_inline __s64 get_status(int key)
{
	__s64 *slot = bpf_map_lookup_elem(&status, &key);

	return slot ? *slot : 0;
}

struct busy_wait_ctx {
	__u64 seq;
	__u64 start;
	bool acked;
};

/* bpf_loop() callback: return 0 continues the loop, return 1 stops it. */
static long busy_wait_cb(__u32 i, void *pctx)
{
	struct busy_wait_ctx *w = pctx;

	if (((__u64)get_status(BUSY_ACK) >> 32) == w->seq) {
		w->acked = true;
		return 1;
	}
	if (bpf_ktime_get_ns() - w->start > BUSY_WAIT_NS)
		return 1;
	return 0;
}

static int busy_cb_gen1(struct bpf_map *map, void *key, void *value)
{
	struct task_work_busy_value *work = value;
	__u64 we = (__u64)get_status(BUSY_WINDOW) >> 32;

	/*
	 * Backstop for deliveries from abandoned attempts: accept only
	 * while no window for a different sequence is published.  The
	 * test waits for the old workers to disappear from PID lookup,
	 * after their exit task_work, before resetting the shared state.
	 * This callback may run before fexit publishes the window state.
	 */
	if (we && we != (__u64)work->seq)
		return 0;
	set_status(BUSY_CB1_SEQ, work->seq);
	return 0;
}

static int busy_cb_gen2(struct bpf_map *map, void *key, void *value)
{
	struct task_work_busy_value *work = value;
	__s64 *count;
	int key2 = BUSY_CB2_COUNT;

	/*
	 * Count only deliveries belonging to the sequence whose window is
	 * (or was) published: a late delivery from an abandoned attempt
	 * must not satisfy a later exactly-once check.
	 */
	if ((__u64)work->seq != ((__u64)get_status(BUSY_WINDOW) >> 32))
		return 0;
	set_status(BUSY_CB2_SEQ, work->seq);
	count = bpf_map_lookup_elem(&status, &key2);
	if (count)
		__sync_fetch_and_add(count, 1);
	return 0;
}

SEC("fexit/task_work_add")
int BPF_PROG(busy_fexit_task_work_add, struct task_struct *task,
	     struct callback_head *work, int notify, int retval)
{
	struct busy_wait_ctx w = {};
	__u64 seq, elapsed;

	seq = (__u64)get_status(BUSY_ARM_SEQ);
	if (!seq)
		return 0;

	/*
	 * One-shot arm.  Only this test's own first-round add may consume
	 * it and hold the window: a successful TWA_SIGNAL add of the
	 * bpf_task_work callback, issued from the trigger thread's CPU by
	 * the self-IPI'd scheduling handler and targeting the target
	 * thread.  Everything else leaves the arm untouched.
	 */
	if (retval != 0 || notify != TWA_SIGNAL ||
	    BPF_CORE_READ(task, pid) != target_tid)
		return 0;
	if ((__u32)bpf_get_current_pid_tgid() != (__u32)trigger_tid)
		return 0;

	set_status(BUSY_ARM_SEQ, 0);
	set_status(BUSY_WINDOW, (seq << 32) | 1);

	w.seq = seq;
	w.start = bpf_ktime_get_ns();
	bpf_loop(BUSY_WAIT_ITERS, busy_wait_cb, &w, 0);
	elapsed = bpf_ktime_get_ns() - w.start;

	/*
	 * Publish elapsed time for logging after worker cleanup.  Window
	 * validity depends on seeing ACK here, not on this diagnostic.
	 */
	set_status(BUSY_LAST_WAIT_NS, (__s64)elapsed);
	if (w.acked)
		set_status(BUSY_WINDOW, (seq << 32) | 2);
	else
		set_status(BUSY_WINDOW, (seq << 32) | 3);
	return 0;
}

/*
 * Attempt the second scheduling round on the same context, targeting the
 * trigger thread.  The attempt uses the value keyed by its own sequence,
 * so it can never touch another attempt's context.  Returns true if a
 * terminal result was published into res_key; a transient -EBUSY is left
 * unpublished for the caller's retry policy, mirroring the race test's
 * convention.
 */
static __always_inline bool busy_try_gen2(__u64 seq, int res_key)
{
	struct task_work_busy_value *work;
	struct task_struct *task;
	int key = (int)seq, err;

	task = bpf_task_from_pid(trigger_tid);
	if (!task) {
		set_status(res_key, ((__s64)seq << 32) | (__u32)-ESRCH);
		return true;
	}
	work = bpf_map_lookup_elem(&hmap, &key);
	if (!work) {
		bpf_task_release(task);
		set_status(res_key, ((__s64)seq << 32) | (__u32)-ENOENT);
		return true;
	}
	work->seq = seq;
	err = bpf_task_work_schedule_signal(task, &work->tw, &hmap,
					    busy_cb_gen2);
	bpf_task_release(task);
	if (err == -EBUSY)
		return false;
	set_status(res_key, ((__s64)seq << 32) | (__u32)err);
	return true;
}

SEC("tracepoint/syscalls/sys_enter_getppid")
int busy_sched_work(void *ctx)
{
	__u32 tid = (__u32)bpf_get_current_pid_tgid();
	struct task_work_busy_value *work;
	struct task_struct *task;
	__u64 window, seq;
	int key, err;

	/* Also keep the kconfig extern in object BTF for the userspace skip. */
	if (CONFIG_PREEMPT_RT)
		return 0;

	if (tid == (__u32)trigger_tid) {
		/*
		 * Trigger thread: schedule the first round once per armed
		 * sequence, targeting the target thread.  The kfunc queues
		 * the scheduling irq_work and returns; the handler then
		 * parks inside the fexit window above.  A fresh value has no
		 * expected BUSY source, so publish every scheduling result.
		 */
		seq = (__u64)get_status(BUSY_ARM_SEQ);
		if (!seq ||
		    ((__u64)get_status(BUSY_GEN1_RESULT) >> 32) == seq)
			return 0;
		key = (int)seq;
		task = bpf_task_from_pid(target_tid);
		if (!task) {
			set_status(BUSY_GEN1_RESULT,
				   ((__s64)seq << 32) | (__u32)-ESRCH);
			set_status(BUSY_ARM_SEQ, 0);
			return 0;
		}
		work = bpf_map_lookup_elem(&hmap, &key);
		if (!work) {
			bpf_task_release(task);
			set_status(BUSY_GEN1_RESULT,
				   ((__s64)seq << 32) | (__u32)-ENOENT);
			set_status(BUSY_ARM_SEQ, 0);
			return 0;
		}
		work->seq = seq;
		err = bpf_task_work_schedule_signal(task, &work->tw, &hmap,
						    busy_cb_gen1);
		bpf_task_release(task);
		set_status(BUSY_GEN1_RESULT, ((__s64)seq << 32) | (__u32)err);
		return 0;
	}

	if (tid != (__u32)target_tid)
		return 0;

	window = (__u64)get_status(BUSY_WINDOW);
	seq = window >> 32;
	if (!seq || (__u32)window == 0)
		return 0;
	/*
	 * The first callback must have completed for this sequence.  Being
	 * inside the target thread's own subsequent syscall proves the
	 * callback wrapper has returned and the context is STANDBY, which
	 * is stronger than observing the completion marker from another
	 * thread.
	 */
	if ((__u64)get_status(BUSY_CB1_SEQ) != seq)
		return 0;

	if ((__u64)get_status(BUSY_ACK) >> 32 != seq) {
		/*
		 * The in-window attempt, exactly once per sequence.  On a
		 * fixed kernel it must be rejected with -EBUSY: the context
		 * is STANDBY, the map and program references are alive and
		 * no other scheduler exists, so only the BUSY gate can
		 * reject here.
		 */
		if (!busy_try_gen2(seq, BUSY_ACK))
			set_status(BUSY_ACK,
				   ((__s64)seq << 32) | (__u32)-EBUSY);
		return 0;
	}

	/*
	 * Recovery, gated by the test: the fexit program releasing the
	 * window does not mean the whole old handler has returned and
	 * BUSY has cleared yet, so retry until the schedule succeeds.
	 */
	if ((__u64)get_status(BUSY_RECOVER_SEQ) != seq)
		return 0;
	if ((__s32)get_status(BUSY_ACK) == 0)
		return 0;	/* scheduled in-window; nothing to recover */
	if (((__u64)get_status(BUSY_GEN2_RESULT) >> 32) == seq)
		return 0;	/* terminal recovery result published */
	busy_try_gen2(seq, BUSY_GEN2_RESULT);
	return 0;
}
