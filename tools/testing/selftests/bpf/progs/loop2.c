// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("raw_tracepoint/consume_skb")
int while_true(volatile struct pt_regs* ctx)
{
	int i = 0;

	while (true) {
		if (PT_REGS_RC(ctx) & 1)
			i += 3;
		else
			i += 7;
		if (i > 40)
			break;
	}

	return i;
}
