// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_test_utils.h"

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table SEC(".maps");

SEC("?uprobe")
int uprobe_normal(void *ctx)
{
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

SEC("?uprobe.s")
int uprobe_sleepable_1(void *ctx)
{
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

int executed = 0;
int my_pid = 0;

SEC("?uprobe.s")
int uprobe_sleepable_2(void *ctx)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	executed++;
	return 0;
}

char __license[] SEC("license") = "GPL";
