// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/*
 * The member is per-CPU and __nullable: per_cpu_ptr() shifts NULL like any
 * other pointer, so a check has to come before the helper.
 */
SEC("struct_ops/test_nullable")
__failure __msg("Possibly NULL pointer passed to trusted R1")
int BPF_PROG(unchecked_percpu, struct bpf_testmod_nullable_args *args)
{
	struct bpf_testmod_pcpu *v = bpf_this_cpu_ptr(args->pcpu_or_null);

	return v->val;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_nullable_fail = {
	.test_nullable = (void *)unchecked_percpu,
};
