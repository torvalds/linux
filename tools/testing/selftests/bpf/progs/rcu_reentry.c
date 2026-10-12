// SPDX-License-Identifier: GPL-2.0
/*
 * Re-enter call_srcu() from a BPF program.  fentry on rcu_segcblist_enqueue()
 * fires inside call_srcu()'s enqueue (reached from srcu_gp_start_if_needed()
 * with the srcu_data ->lock held); the handler then calls call_rcu_tasks_trace()
 * -- itself call_srcu() on rcu_tasks_trace_srcu_struct -- re-entering the same
 * srcu_data on the same CPU.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, __u64);
} task_stg SEC(".maps");

int target_pid;
int hits;
int get_errs;
int del_errs;
int done;

SEC("fentry/rcu_segcblist_enqueue")
int BPF_PROG(reenter)
{
	struct task_struct *cur;

	if (done || !target_pid)
		return 0;

	cur = bpf_get_current_task_btf();
	if (cur->pid != target_pid)
		return 0;

	/* Issue the nested call exactly once, so the test is deterministic. */
	done = 1;
	__sync_fetch_and_add(&hits, 1);

	/* Re-enter via a task-storage delete, which calls call_rcu_tasks_trace(). */
	if (!bpf_task_storage_get(&task_stg, cur, 0, BPF_LOCAL_STORAGE_GET_F_CREATE))
		__sync_fetch_and_add(&get_errs, 1);
	else if (bpf_task_storage_delete(&task_stg, cur))
		__sync_fetch_and_add(&del_errs, 1);

	return 0;
}
