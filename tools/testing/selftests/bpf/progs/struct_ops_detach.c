// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/*
 * This subprogram validates that libbpf handles the situation in which BPF
 * object has subprograms in .text section, but has no entry BPF programs.
 * At some point that was causing issues due to legacy logic of treating such
 * subprogram as entry program (with unknown program type, which would fail).
 */
int dangling_subprog(void)
{
	/* do nothing, just be here */
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_do_detach;
