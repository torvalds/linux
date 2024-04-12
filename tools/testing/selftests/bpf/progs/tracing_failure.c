// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("?fentry/bpf_spin_lock")
int BPF_PROG(test_spin_lock, struct bpf_spin_lock *lock)
{
	return 0;
}

SEC("?fentry/bpf_spin_unlock")
int BPF_PROG(test_spin_unlock, struct bpf_spin_lock *lock)
{
	return 0;
}
