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
int while_true(struct pt_regs *ctx)
{
	volatile __u64 i = 0, sum = 0;
	do {
		i++;
		sum += PT_REGS_RC(ctx);
	} while (i < 0x100000000ULL);
	return sum;
}
