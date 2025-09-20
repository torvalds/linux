// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Red Hat, Inc. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/* Dummy fentry bpf prog for testing fentry attachment chains */
SEC("fentry/XXX")
int BPF_PROG(recursive_attach, int a)
{
	return 0;
}
