// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} task_storage SEC(".maps");

long hits;
long gp_hits;
long gp_times;
long current_gp_start;
long unexpected;
bool postgp_seen;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int get_local(void *ctx)
{
	struct task_struct *task;
	int idx;
	int *s;

	idx = 0;
	task = bpf_get_current_task_btf();
	s = bpf_task_storage_get(&task_storage, task, &idx,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!s)
		return 0;

	*s = 3;
	bpf_task_storage_delete(&task_storage, task);
	__sync_add_and_fetch(&hits, 1);
	return 0;
}

SEC("fentry/rcu_tasks_trace_pregp_step")
int pregp_step(struct pt_regs *ctx)
{
	current_gp_start = bpf_ktime_get_ns();
	return 0;
}

SEC("fentry/rcu_tasks_trace_postgp")
int postgp(struct pt_regs *ctx)
{
	if (!current_gp_start && postgp_seen) {
		/* Will only happen if prog tracing rcu_tasks_trace_pregp_step doesn't
		 * execute before this prog
		 */
		__sync_add_and_fetch(&unexpected, 1);
		return 0;
	}

	__sync_add_and_fetch(&gp_times, bpf_ktime_get_ns() - current_gp_start);
	__sync_add_and_fetch(&gp_hits, 1);
	current_gp_start = 0;
	postgp_seen = true;
	return 0;
}

char _license[] SEC("license") = "GPL";
