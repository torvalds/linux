// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* freplace target: a global subprogram returning 16 bytes in R0:R2. */
__naked unsigned __int128 agg_ret_target_func(void)
{
	asm volatile (
	"r0 = 0x1234;"
	"r2 = 0x5678;"
	"exit;"
	);
}

SEC("tc")
__naked int agg_ret_target(void)
{
	asm volatile (
	"call %[agg_ret_target_func];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(agg_ret_target_func)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
