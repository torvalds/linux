// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops/test_1")
int BPF_PROG(test_1_forgotten)
{
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops ops = {
	/* we forgot to reference test_1_forgotten above, oops */
};

