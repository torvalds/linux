// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "runqslower.h"

#define TASK_RUNNING 0
#define BPF_F_CURRENT_CPU 0xffffffffULL

const volatile __u64 min_us = 0;
const volatile pid_t targ_pid = 0;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, u64);
} start SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u32));
} events SEC(".maps");

/* record enqueue timestamp */
__always_inline
static int trace_enqueue(struct task_struct *t)
{
	u32 pid = t->pid;
	u64 *ptr;

	if (!pid || (targ_pid && targ_pid != pid))
		return 0;

	ptr = bpf_task_storage_get(&start, t, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return 0;

	*ptr = bpf_ktime_get_ns();
	return 0;
}

SEC("tp_btf/sched_wakeup")
int handle__sched_wakeup(u64 *ctx)
{
	/* TP_PROTO(struct task_struct *p) */
	struct task_struct *p = (void *)ctx[0];

	return trace_enqueue(p);
}

SEC("tp_btf/sched_wakeup_new")
int handle__sched_wakeup_new(u64 *ctx)
{
	/* TP_PROTO(struct task_struct *p) */
	struct task_struct *p = (void *)ctx[0];

	return trace_enqueue(p);
}

SEC("tp_btf/sched_switch")
int handle__sched_switch(u64 *ctx)
{
	/* TP_PROTO(bool preempt, struct task_struct *prev,
	 *	    struct task_struct *next)
	 */
	struct task_struct *prev = (struct task_struct *)ctx[1];
	struct task_struct *next = (struct task_struct *)ctx[2];
	struct runq_event event = {};
	u64 *tsp, delta_us;
	u32 pid;

	/* ivcsw: treat like an enqueue event and store timestamp */
	if (prev->__state == TASK_RUNNING)
		trace_enqueue(prev);

	pid = next->pid;

	/* For pid mismatch, save a bpf_task_storage_get */
	if (!pid || (targ_pid && targ_pid != pid))
		return 0;

	/* fetch timestamp and calculate delta */
	tsp = bpf_task_storage_get(&start, next, 0, 0);
	if (!tsp)
		return 0;   /* missed enqueue */

	delta_us = (bpf_ktime_get_ns() - *tsp) / 1000;
	if (min_us && delta_us <= min_us)
		return 0;

	event.pid = pid;
	event.delta_us = delta_us;
	bpf_get_current_comm(&event.task, sizeof(event.task));

	/* output */
	bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
			      &event, sizeof(event));

	bpf_task_storage_delete(&start, next);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
