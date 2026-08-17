// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

/* Sleepable program on a non-faultable tracepoint should fail to load */
SEC("tp_btf.s/sched_switch")
__failure __msg("Sleepable program cannot attach to non-faultable tracepoint")
int BPF_PROG(handle_sched_switch, bool preempt,
	     struct task_struct *prev, struct task_struct *next)
{
	return 0;
}
