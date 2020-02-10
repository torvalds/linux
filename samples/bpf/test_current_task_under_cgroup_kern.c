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

struct bpf_map_def SEC("maps") cgroup_map = {
	.type			= BPF_MAP_TYPE_CGROUP_ARRAY,
	.key_size		= sizeof(u32),
	.value_size		= sizeof(u32),
	.max_entries	= 1,
};

struct bpf_map_def SEC("maps") perf_map = {
	.type			= BPF_MAP_TYPE_ARRAY,
	.key_size		= sizeof(u32),
	.value_size		= sizeof(u64),
	.max_entries	= 1,
};

/* Writes the last PID that called sync to a map at index 0 */
SEC("kprobe/sys_sync")
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
