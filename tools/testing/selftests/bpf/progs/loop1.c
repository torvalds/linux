// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

char _license[] SEC("license") = "GPL";

SEC("raw_tracepoint/kfree_skb")
int nested_loops(volatile struct pt_regs* ctx)
{
	int i, j, sum = 0, m;

	for (j = 0; j < 300; j++)
		for (i = 0; i < j; i++) {
			if (j & 1)
				m = PT_REGS_RC(ctx);
			else
				m = j;
			sum += i * m;
		}

	return sum;
}
