// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

int run_cnt = 0;

SEC("perf_event")
int handler(struct pt_regs *ctx)
{
	__sync_fetch_and_add(&run_cnt, 1);
	return 0;
}

char _license[] SEC("license") = "GPL";
