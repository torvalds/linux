// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

long subprog_call_mem_kfunc(long a, long b, long c, long d, long e, long size)
{
	char buf[8] = {};

	return bpf_kfunc_call_stack_arg_mem(a, b, c, d, e, buf, size);
}

#else

long subprog_call_mem_kfunc(void)
{
	return 0;
}

#endif
