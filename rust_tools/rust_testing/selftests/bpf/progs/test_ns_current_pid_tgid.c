// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Carlos Neira cneirabustos@gmail.com */

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u32);
} sock_map SEC(".maps");

__u64 user_pid = 0;
__u64 user_tgid = 0;
__u64 dev = 0;
__u64 ino = 0;

static void get_pid_tgid(void)
{
	struct bpf_pidns_info nsdata;

	if (bpf_get_ns_current_pid_tgid(dev, ino, &nsdata, sizeof(struct bpf_pidns_info)))
		return;

	user_pid = nsdata.pid;
	user_tgid = nsdata.tgid;
}

SEC("?tracepoint/syscalls/sys_enter_nanosleep")
int tp_handler(const void *ctx)
{
	get_pid_tgid();
	return 0;
}

SEC("?cgroup/bind4")
int cgroup_bind4(struct bpf_sock_addr *ctx)
{
	get_pid_tgid();
	return 1;
}

SEC("?sk_msg")
int sk_msg(struct sk_msg_md *msg)
{
	get_pid_tgid();
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
