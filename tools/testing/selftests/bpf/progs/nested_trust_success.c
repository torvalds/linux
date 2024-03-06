// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#include "nested_trust_common.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, u64);
} sk_storage_map SEC(".maps");

SEC("tp_btf/task_newtask")
__success
int BPF_PROG(test_read_cpumask, struct task_struct *task, u64 clone_flags)
{
	bpf_cpumask_test_cpu(0, task->cpus_ptr);
	return 0;
}

SEC("tp_btf/tcp_probe")
__success
int BPF_PROG(test_skb_field, struct sock *sk, struct sk_buff *skb)
{
	bpf_sk_storage_get(&sk_storage_map, skb->sk, 0, 0);
	return 0;
}
