// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* task->flags for off-cpu analysis */
#define PF_KTHREAD   0x00200000  /* I am a kernel thread */

/* task->state for off-cpu analysis */
#define TASK_INTERRUPTIBLE	0x0001
#define TASK_UNINTERRUPTIBLE	0x0002

#define MAX_STACKS   32
#define MAX_ENTRIES  102400

struct tstamp_data {
	__u32 stack_id;
	__u32 state;
	__u64 timestamp;
};

struct offcpu_key {
	__u32 pid;
	__u32 tgid;
	__u32 stack_id;
	__u32 state;
	__u64 cgroup_id;
};

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, MAX_STACKS * sizeof(__u64));
	__uint(max_entries, MAX_ENTRIES);
} stacks SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct tstamp_data);
} tstamp SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct offcpu_key));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, MAX_ENTRIES);
} off_cpu SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} cpu_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} task_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} cgroup_filter SEC(".maps");

/* new kernel task_struct definition */
struct task_struct___new {
	long __state;
} __attribute__((preserve_access_index));

/* old kernel task_struct definition */
struct task_struct___old {
	long state;
} __attribute__((preserve_access_index));

int enabled = 0;
int has_cpu = 0;
int has_task = 0;
int has_cgroup = 0;

const volatile bool has_prev_state = false;
const volatile bool needs_cgroup = false;
const volatile bool uses_cgroup_v1 = false;

/*
 * Old kernel used to call it task_struct->state and now it's '__state'.
 * Use BPF CO-RE "ignored suffix rule" to deal with it like below:
 *
 * https://nakryiko.com/posts/bpf-core-reference-guide/#handling-incompatible-field-and-type-changes
 */
static inline int get_task_state(struct task_struct *t)
{
	/* recast pointer to capture new type for compiler */
	struct task_struct___new *t_new = (void *)t;

	if (bpf_core_field_exists(t_new->__state)) {
		return BPF_CORE_READ(t_new, __state);
	} else {
		/* recast pointer to capture old type for compiler */
		struct task_struct___old *t_old = (void *)t;

		return BPF_CORE_READ(t_old, state);
	}
}

static inline __u64 get_cgroup_id(struct task_struct *t)
{
	struct cgroup *cgrp;

	if (uses_cgroup_v1)
		cgrp = BPF_CORE_READ(t, cgroups, subsys[perf_event_cgrp_id], cgroup);
	else
		cgrp = BPF_CORE_READ(t, cgroups, dfl_cgrp);

	return BPF_CORE_READ(cgrp, kn, id);
}

static inline int can_record(struct task_struct *t, int state)
{
	/* kernel threads don't have user stack */
	if (t->flags & PF_KTHREAD)
		return 0;

	if (state != TASK_INTERRUPTIBLE &&
	    state != TASK_UNINTERRUPTIBLE)
		return 0;

	if (has_cpu) {
		__u32 cpu = bpf_get_smp_processor_id();
		__u8 *ok;

		ok = bpf_map_lookup_elem(&cpu_filter, &cpu);
		if (!ok)
			return 0;
	}

	if (has_task) {
		__u8 *ok;
		__u32 pid = t->pid;

		ok = bpf_map_lookup_elem(&task_filter, &pid);
		if (!ok)
			return 0;
	}

	if (has_cgroup) {
		__u8 *ok;
		__u64 cgrp_id = get_cgroup_id(t);

		ok = bpf_map_lookup_elem(&cgroup_filter, &cgrp_id);
		if (!ok)
			return 0;
	}

	return 1;
}

static int off_cpu_stat(u64 *ctx, struct task_struct *prev,
			struct task_struct *next, int state)
{
	__u64 ts;
	__u32 stack_id;
	struct tstamp_data *pelem;

	ts = bpf_ktime_get_ns();

	if (!can_record(prev, state))
		goto next;

	stack_id = bpf_get_stackid(ctx, &stacks,
				   BPF_F_FAST_STACK_CMP | BPF_F_USER_STACK);

	pelem = bpf_task_storage_get(&tstamp, prev, NULL,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!pelem)
		goto next;

	pelem->timestamp = ts;
	pelem->state = state;
	pelem->stack_id = stack_id;

next:
	pelem = bpf_task_storage_get(&tstamp, next, NULL, 0);

	if (pelem && pelem->timestamp) {
		struct offcpu_key key = {
			.pid = next->pid,
			.tgid = next->tgid,
			.stack_id = pelem->stack_id,
			.state = pelem->state,
			.cgroup_id = needs_cgroup ? get_cgroup_id(next) : 0,
		};
		__u64 delta = ts - pelem->timestamp;
		__u64 *total;

		total = bpf_map_lookup_elem(&off_cpu, &key);
		if (total)
			*total += delta;
		else
			bpf_map_update_elem(&off_cpu, &key, &delta, BPF_ANY);

		/* prevent to reuse the timestamp later */
		pelem->timestamp = 0;
	}

	return 0;
}

SEC("tp_btf/sched_switch")
int on_switch(u64 *ctx)
{
	struct task_struct *prev, *next;
	int prev_state;

	if (!enabled)
		return 0;

	prev = (struct task_struct *)ctx[1];
	next = (struct task_struct *)ctx[2];

	if (has_prev_state)
		prev_state = (int)ctx[3];
	else
		prev_state = get_task_state(prev);

	return off_cpu_stat(ctx, prev, next, prev_state);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
