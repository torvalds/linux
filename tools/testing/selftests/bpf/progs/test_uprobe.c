// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Hengqi Chen */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

pid_t my_pid = 0;

int test1_result = 0;
int test2_result = 0;
int test3_result = 0;
int test4_result = 0;

SEC("uprobe/./liburandom_read.so:urandlib_api_sameoffset")
int BPF_UPROBE(test1)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	test1_result = 1;
	return 0;
}

SEC("uprobe/./liburandom_read.so:urandlib_api_sameoffset@LIBURANDOM_READ_1.0.0")
int BPF_UPROBE(test2)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	test2_result = 1;
	return 0;
}

SEC("uretprobe/./liburandom_read.so:urandlib_api_sameoffset@@LIBURANDOM_READ_2.0.0")
int BPF_URETPROBE(test3, int ret)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	test3_result = ret;
	return 0;
}

SEC("uprobe")
int BPF_UPROBE(test4)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != my_pid)
		return 0;

	test4_result = 1;
	return 0;
}
