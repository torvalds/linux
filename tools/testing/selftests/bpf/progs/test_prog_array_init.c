/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Hengqi Chen */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

const volatile pid_t my_pid = 0;
int value = 0;

SEC("raw_tp/sys_enter")
int tailcall_1(void *ctx)
{
	value = 42;
	return 0;
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(__u32));
	__array(values, int (void *));
} prog_array_init SEC(".maps") = {
	.values = {
		[1] = (void *)&tailcall_1,
	},
};

SEC("raw_tp/sys_enter")
int entry(void *ctx)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	bpf_tail_call(ctx, &prog_array_init, 1);
	return 0;
}
