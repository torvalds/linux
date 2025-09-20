// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Isovalent, Inc.
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
} sock_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
} sock_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, __u32);
	__type(value, __u64);
} socket_storage SEC(".maps");

static int prog_msg_verdict_common(struct sk_msg_md *msg)
{
	struct task_struct *task = (struct task_struct *)bpf_get_current_task();
	int verdict = SK_PASS;
	__u32 pid, tpid;
	__u64 *sk_stg;

	pid = bpf_get_current_pid_tgid() >> 32;
	sk_stg = bpf_sk_storage_get(&socket_storage, msg->sk, 0, BPF_SK_STORAGE_GET_F_CREATE);
	if (!sk_stg)
		return SK_DROP;
	*sk_stg = pid;
	bpf_probe_read_kernel(&tpid , sizeof(tpid), &task->tgid);
	if (pid != tpid)
		verdict = SK_DROP;
	bpf_sk_storage_delete(&socket_storage, (void *)msg->sk);
	return verdict;
}

SEC("sk_msg")
int prog_msg_verdict(struct sk_msg_md *msg)
{
	return prog_msg_verdict_common(msg);
}

SEC("sk_msg")
int prog_msg_verdict_clone(struct sk_msg_md *msg)
{
	return prog_msg_verdict_common(msg);
}

SEC("sk_msg")
int prog_msg_verdict_clone2(struct sk_msg_md *msg)
{
	return prog_msg_verdict_common(msg);
}

SEC("sk_skb/stream_verdict")
int prog_skb_verdict(struct __sk_buff *skb)
{
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
