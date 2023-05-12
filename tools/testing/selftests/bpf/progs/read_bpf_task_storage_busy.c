// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022. Huawei Technologies Co., Ltd */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern bool CONFIG_PREEMPT __kconfig __weak;
extern const int bpf_task_storage_busy __ksym;

char _license[] SEC("license") = "GPL";

int pid = 0;
int busy = 0;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} task SEC(".maps");

SEC("raw_tp/sys_enter")
int BPF_PROG(read_bpf_task_storage_busy)
{
	int *value;

	if (!CONFIG_PREEMPT)
		return 0;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	value = bpf_this_cpu_ptr(&bpf_task_storage_busy);
	if (value)
		busy = *value;

	return 0;
}
