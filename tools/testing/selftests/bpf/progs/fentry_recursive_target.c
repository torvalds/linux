// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Red Hat, Inc. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/* Dummy fentry bpf prog for testing fentry attachment chains. It's going to be
 * a start of the chain.
 */
SEC("fentry/bpf_testmod_fentry_test1")
int BPF_PROG(test1, int a)
{
	return 0;
}
