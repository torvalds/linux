// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/* The member is trusted, but __nullable, so it needs a check before the kfunc. */
SEC("struct_ops/test_nullable")
__failure __msg("Possibly NULL pointer passed to trusted")
int BPF_PROG(unchecked_nullable, struct bpf_testmod_nullable_args *args)
{
	struct task_struct *t;

	t = bpf_task_acquire(args->task_or_null);
	if (t)
		bpf_task_release(t);
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops3 testmod_nullable_fail = {
	.test_nullable = (void *)unchecked_nullable,
};
