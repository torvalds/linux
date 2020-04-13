// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Carlos Neira cneirabustos@gmail.com */

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>

static volatile struct {
	__u64 dev;
	__u64 ino;
	__u64 pid_tgid;
	__u64 user_pid_tgid;
} res;

SEC("raw_tracepoint/sys_enter")
int trace(void *ctx)
{
	__u64  ns_pid_tgid, expected_pid;
	struct bpf_pidns_info nsdata;
	__u32 key = 0;

	if (bpf_get_ns_current_pid_tgid(res.dev, res.ino, &nsdata,
		   sizeof(struct bpf_pidns_info)))
		return 0;

	ns_pid_tgid = (__u64)nsdata.tgid << 32 | nsdata.pid;
	expected_pid = res.user_pid_tgid;

	if (expected_pid != ns_pid_tgid)
		return 0;

	res.pid_tgid = ns_pid_tgid;

	return 0;
}

char _license[] SEC("license") = "GPL";
