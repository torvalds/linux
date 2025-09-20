// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct task_ls_map {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} task_ls_map SEC(".maps");

long gp_seq;

SEC("syscall")
int do_call_rcu_tasks_trace(void *ctx)
{
    struct task_struct *current;
    int *v;

    current = bpf_get_current_task_btf();
    v = bpf_task_storage_get(&task_ls_map, current, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
    if (!v)
        return 1;
    /* Invoke call_rcu_tasks_trace */
    return bpf_task_storage_delete(&task_ls_map, current);
}

SEC("kprobe/rcu_tasks_trace_postgp")
int rcu_tasks_trace_postgp(void *ctx)
{
    __sync_add_and_fetch(&gp_seq, 1);
    return 0;
}

char _license[] SEC("license") = "GPL";
