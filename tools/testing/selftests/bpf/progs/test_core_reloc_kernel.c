// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";

static volatile struct data {
	char in[256];
	char out[256];
} data;

struct task_struct {
	int pid;
	int tgid;
};

SEC("raw_tracepoint/sys_enter")
int test_core_kernel(void *ctx)
{
	struct task_struct *task = (void *)bpf_get_current_task();
	uint64_t pid_tgid = bpf_get_current_pid_tgid();
	int pid, tgid;

	if (BPF_CORE_READ(&pid, &task->pid) ||
	    BPF_CORE_READ(&tgid, &task->tgid))
		return 1;

	/* validate pid + tgid matches */
	data.out[0] = (((uint64_t)pid << 32) | tgid) == pid_tgid;

	return 0;
}

