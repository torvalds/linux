// SPDX-License-Identifier: GPL-2.0
/*
 * Verifier tests for single- and multi-level pointer parameter handling
 * Copyright (c) 2026 CrowdStrike, Inc.
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

SEC("fentry/bpf_fentry_test_ppvoid")
__description("fentry/void**: void ** inferred as scalar")
__success __retval(0)
__log_level(2)
__msg("R1=ctx() R2=scalar()")
__naked void fentry_ppvoid_as_scalar(void)
{
    asm volatile ("					\
	r2 = *(u64 *)(r1 + 0);	\
	r0 = 0;	\
	exit;	\
	" ::: __clobber_all);
}

SEC("fentry/bpf_fentry_test_pppvoid")
__description("fentry/void***: void *** inferred as scalar")
__success __retval(0)
__log_level(2)
__msg("R1=ctx() R2=scalar()")
__naked void fentry_pppvoid_as_scalar(void)
{
    asm volatile ("					\
	r2 = *(u64 *)(r1 + 0);	\
	r0 = 0;	\
	exit;	\
	" ::: __clobber_all);
}

SEC("fentry/bpf_fentry_test_ppfile")
__description("fentry/struct file**: struct file ** inferred as scalar")
__success __retval(0)
__log_level(2)
__msg("R1=ctx() R2=scalar()")
__naked void fentry_ppfile_as_scalar(void)
{
    asm volatile ("					\
	r2 = *(u64 *)(r1 + 0);	\
	r0 = 0;	\
	exit;	\
	" ::: __clobber_all);
}

SEC("fexit/bpf_fexit_test_ret_ppfile")
__description("fexit/return struct file**: returned struct file ** inferred as scalar")
__success __retval(0)
__log_level(2)
__msg("R1=ctx() R2=scalar()")
__naked void fexit_ppfile_as_scalar(void)
{
    asm volatile ("					\
	r2 = *(u64 *)(r1 + 0);	\
	r0 = 0;	\
	exit;	\
	" ::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
