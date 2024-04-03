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

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

SEC("tp_btf/task_newtask")
__failure __msg("R2 must be")
int BPF_PROG(test_invalid_nested_user_cpus, struct task_struct *task, u64 clone_flags)
{
	bpf_cpumask_test_cpu(0, task->user_cpus_ptr);
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("R1 must have zero offset when passed to release func or trusted arg to kfunc")
int BPF_PROG(test_invalid_nested_offset, struct task_struct *task, u64 clone_flags)
{
	bpf_cpumask_first_zero(&task->cpus_mask);
	return 0;
}

/* Although R2 is of type sk_buff but sock_common is expected, we will hit untrusted ptr first. */
SEC("tp_btf/tcp_probe")
__failure __msg("R2 type=untrusted_ptr_ expected=ptr_, trusted_ptr_, rcu_ptr_")
int BPF_PROG(test_invalid_skb_field, struct sock *sk, struct sk_buff *skb)
{
	bpf_sk_storage_get(&sk_storage_map, skb->next, 0, 0);
	return 0;
}
