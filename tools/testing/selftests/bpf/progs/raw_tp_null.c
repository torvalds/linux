// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int tid;
int i;

SEC("tp_btf/bpf_testmod_test_raw_tp_null")
int BPF_PROG(test_raw_tp_null, struct sk_buff *skb)
{
	struct task_struct *task = bpf_get_current_task_btf();

	if (task->pid != tid)
		return 0;

	i = i + skb->mark + 1;
	/* The compiler may move the NULL check before this deref, which causes
	 * the load to fail as deref of scalar. Prevent that by using a barrier.
	 */
	barrier();
	/* If dead code elimination kicks in, the increment below will
	 * be removed. For raw_tp programs, we mark input arguments as
	 * PTR_MAYBE_NULL, so branch prediction should never kick in.
	 */
	if (!skb)
		i += 2;
	return 0;
}
