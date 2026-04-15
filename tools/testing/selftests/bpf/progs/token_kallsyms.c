// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__weak
int token_ksym_subprog(void)
{
	return 0;
}

SEC("xdp")
int xdp_main(struct xdp_md *xdp)
{
	return token_ksym_subprog();
}
