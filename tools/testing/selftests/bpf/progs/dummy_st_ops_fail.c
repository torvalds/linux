// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops.s/test_2")
__failure __msg("attach to unsupported member test_2 of struct bpf_dummy_ops")
int BPF_PROG(test_unsupported_field_sleepable,
	     struct bpf_dummy_ops_state *state, int a1, unsigned short a2,
	     char a3, unsigned long a4)
{
	/* Tries to mark an unsleepable field in struct bpf_dummy_ops as sleepable. */
	return 0;
}

SEC(".struct_ops")
struct bpf_dummy_ops dummy_1 = {
	.test_1 = NULL,
	.test_2 = (void *)test_unsupported_field_sleepable,
	.test_sleepable = (void *)NULL,
};
