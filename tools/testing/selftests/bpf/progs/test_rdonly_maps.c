// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

const struct {
	unsigned a[4];
	/*
	 * if the struct's size is multiple of 16, compiler will put it into
	 * .rodata.cst16 section, which is not recognized by libbpf; work
	 * around this by ensuring we don't have 16-aligned struct
	 */
	char _y;
} rdonly_values = { .a = {2, 3, 4, 5} };

struct {
	unsigned did_run;
	unsigned iters;
	unsigned sum;
} res = {};

SEC("raw_tracepoint/sys_enter:skip_loop")
int skip_loop(struct pt_regs *ctx)
{
	/* prevent compiler to optimize everything out */
	unsigned * volatile p = (void *)&rdonly_values.a;
	unsigned iters = 0, sum = 0;

	/* we should never enter this loop */
	while (*p & 1) {
		iters++;
		sum += *p;
		p++;
	}
	res.did_run = 1;
	res.iters = iters;
	res.sum = sum;
	return 0;
}

SEC("raw_tracepoint/sys_enter:part_loop")
int part_loop(struct pt_regs *ctx)
{
	/* prevent compiler to optimize everything out */
	unsigned * volatile p = (void *)&rdonly_values.a;
	unsigned iters = 0, sum = 0;

	/* validate verifier can derive loop termination */
	while (*p < 5) {
		iters++;
		sum += *p;
		p++;
	}
	res.did_run = 1;
	res.iters = iters;
	res.sum = sum;
	return 0;
}

SEC("raw_tracepoint/sys_enter:full_loop")
int full_loop(struct pt_regs *ctx)
{
	/* prevent compiler to optimize everything out */
	unsigned * volatile p = (void *)&rdonly_values.a;
	int i = ARRAY_SIZE(rdonly_values.a);
	unsigned iters = 0, sum = 0;

	/* validate verifier can allow full loop as well */
	while (i > 0 ) {
		iters++;
		sum += *p;
		p++;
		i--;
	}
	res.did_run = 1;
	res.iters = iters;
	res.sum = sum;
	return 0;
}

char _license[] SEC("license") = "GPL";
