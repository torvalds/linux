// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bench_bpf_timing.bpf.h"

SEC("syscall")
int bench_nop(void *ctx)
{
	return BENCH_BPF_LOOP(0, ({}));
}

char _license[] SEC("license") = "GPL";
