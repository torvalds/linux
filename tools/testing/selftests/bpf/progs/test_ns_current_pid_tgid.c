// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Carlos Neira cneirabustos@gmail.com */

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>

__u64 user_pid = 0;
__u64 user_tgid = 0;
__u64 dev = 0;
__u64 ino = 0;

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int handler(const void *ctx)
{
	struct bpf_pidns_info nsdata;

	if (bpf_get_ns_current_pid_tgid(dev, ino, &nsdata, sizeof(struct bpf_pidns_info)))
		return 0;

	user_pid = nsdata.pid;
	user_tgid = nsdata.tgid;

	return 0;
}

char _license[] SEC("license") = "GPL";
