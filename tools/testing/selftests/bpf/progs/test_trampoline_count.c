// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct task_struct;

SEC("fentry/__set_task_comm")
int BPF_PROG(prog1, struct task_struct *tsk, const char *buf, bool exec)
{
	return 0;
}

SEC("fexit/__set_task_comm")
int BPF_PROG(prog2, struct task_struct *tsk, const char *buf, bool exec)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
