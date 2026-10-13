// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

/* Ensure module parameter has PTR_MAYBE_NULL */
SEC("tp_btf/bpf_testmod_test_raw_tp_null_tp")
__failure __msg("R1 invalid mem access 'trusted_ptr_or_null_'")
int test_raw_tp_null_bpf_testmod_test_raw_tp_null_arg_1(void *ctx) {
    asm volatile("r1 = *(u64 *)(r1 +0); r1 = *(u64 *)(r1 +0);" ::: __clobber_all);
    return 0;
}

/* Check NULL marking */
SEC("tp_btf/sched_pi_setprio")
__failure __msg("R1 invalid mem access 'trusted_ptr_or_null_'")
int test_raw_tp_null_sched_pi_setprio_arg_2(void *ctx) {
    asm volatile("r1 = *(u64 *)(r1 +8); r1 = *(u64 *)(r1 +0);" ::: __clobber_all);
    return 0;
}

/* Plain raw tracepoint arguments remain scalar values. */
SEC("raw_tp/signal_generate")
__success
int test_raw_tp_signal_generate_info_scalar(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +8); if r1 != 1 goto +0;" ::: __clobber_all);
	return 0;
}

/* tp_btf programs may inspect the sentinel as a scalar value. */
SEC("tp_btf/signal_generate")
__success
int test_tp_btf_signal_generate_info_scalar(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +8); if r1 != 1 goto +0;" ::: __clobber_all);
	return 0;
}

/* SEND_SIG_PRIV is non-NULL, so a NULL check cannot make info safe. */
SEC("tp_btf/signal_generate")
__failure __msg("R1 invalid mem access 'scalar'")
int test_tp_btf_signal_generate_info_no_deref(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +8); if r1 == 0 goto +1; "
		     "r1 = *(u32 *)(r1 +0);" ::: __clobber_all);
	return 0;
}

SEC("tp_btf/signal_deliver")
__failure __msg("R1 invalid mem access 'scalar'")
int test_tp_btf_signal_deliver_info_no_deref(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +8); r1 = *(u32 *)(r1 +0);" ::: __clobber_all);
	return 0;
}

SEC("tp_btf/sched_process_wait")
__failure __msg("R1 invalid mem access 'trusted_ptr_or_null_'")
int test_raw_tp_null_sched_process_wait_arg_1(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +0); r1 = *(u32 *)(r1 +0);" ::: __clobber_all);
	return 0;
}

SEC("tp_btf/sched_process_wait")
__success
int test_raw_tp_null_sched_process_wait_arg_1_checked(void *ctx)
{
	asm volatile("r1 = *(u64 *)(r1 +0); if r1 == 0 goto +1; "
		     "r1 = *(u32 *)(r1 +0);" ::: __clobber_all);
	return 0;
}
