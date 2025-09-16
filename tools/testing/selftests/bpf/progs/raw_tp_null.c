// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int tid;
int i;

SEC("tp_btf/bpf_testmod_test_raw_tp_null_tp")
int BPF_PROG(test_raw_tp_null, struct sk_buff *skb)
{
	struct task_struct *task = bpf_get_current_task_btf();

	if (task->pid != tid)
		return 0;

	/* If dead code elimination kicks in, the increment +=2 will be
	 * removed. For raw_tp programs attaching to tracepoints in kernel
	 * modules, we mark input arguments as PTR_MAYBE_NULL, so branch
	 * prediction should never kick in.
	 */
	asm volatile ("%[i] += 1; if %[ctx] != 0 goto +1; %[i] += 2;"
			: [i]"+r"(i)
			: [ctx]"r"(skb)
			: "memory");
	return 0;
}
