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
