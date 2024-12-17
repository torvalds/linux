// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops/unsupported_ops")
__failure
__msg("attach to unsupported member unsupported_ops of struct bpf_testmod_ops")
int BPF_PROG(unsupported_ops)
{
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod = {
	.unsupported_ops = (void *)unsupported_ops,
};
