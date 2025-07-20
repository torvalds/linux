// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

SEC("freplace/xdp_prog1")
int new_xdp_prog2(struct xdp_md *xd)
{
	return XDP_DROP;
}
