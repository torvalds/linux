// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

/* taken from /sys/kernel/tracing/events/sched/sched_switch/format */
struct sched_switch_args {
	unsigned long long pad;
	char prev_comm[TASK_COMM_LEN];
	int prev_pid;
	int prev_prio;
	long long prev_state;
	char next_comm[TASK_COMM_LEN];
	int next_pid;
	int next_prio;
};

SEC("tracepoint/sched/sched_switch")
int oncpu(struct sched_switch_args *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
