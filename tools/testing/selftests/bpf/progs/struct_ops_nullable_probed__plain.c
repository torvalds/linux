// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/*
 * Through a plain pointer the member is probed, which tolerates NULL, so the
 * tag asks for no check there, as the BTF_TYPE_SAFE_*_OR_NULL lists never did.
 */
SEC("struct_ops/test_nullable")
__success
int BPF_PROG(unchecked_plain, struct bpf_testmod_nullable_args *args)
{
	return args->self->task_or_null->pid;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_nullable_probed = {
	.test_nullable = (void *)unchecked_plain,
};
