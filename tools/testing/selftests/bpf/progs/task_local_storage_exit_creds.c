// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, __u64);
} task_storage SEC(".maps");

int run_count = 0;
int valid_ptr_count = 0;
int null_ptr_count = 0;

SEC("fentry/exit_creds")
int BPF_PROG(trace_exit_creds, struct task_struct *task)
{
	__u64 *ptr;

	ptr = bpf_task_storage_get(&task_storage, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		__sync_fetch_and_add(&valid_ptr_count, 1);
	else
		__sync_fetch_and_add(&null_ptr_count, 1);

	__sync_fetch_and_add(&run_count, 1);
	return 0;
}
