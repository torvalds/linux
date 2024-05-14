/* Copyright (c) 2016 Sargun Dhillon <sargun@sargun.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include <linux/version.h>
#include <bpf/bpf_helpers.h>
#include <uapi/linux/utsname.h>
#include "trace_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u32));
	__uint(max_entries, 1);
} cgroup_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, u64);
	__uint(max_entries, 1);
} perf_map SEC(".maps");

/* Writes the last PID that called sync to a map at index 0 */
SEC("kprobe/" SYSCALL(sys_sync))
int bpf_prog1(struct pt_regs *ctx)
{
	u64 pid = bpf_get_current_pid_tgid();
	int idx = 0;

	if (!bpf_current_task_under_cgroup(&cgroup_map, 0))
		return 0;

	bpf_map_update_elem(&perf_map, &idx, &pid, BPF_ANY);
	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
