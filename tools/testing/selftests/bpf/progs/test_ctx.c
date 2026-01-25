// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Valve Corporation.
 * Author: Changwoo Min <changwoo@igalia.com>
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

extern void bpf_kfunc_trigger_ctx_check(void) __ksym;

int count_hardirq;
int count_softirq;
int count_task;

/* Triggered via bpf_prog_test_run from user-space */
SEC("syscall")
int trigger_all_contexts(void *ctx)
{
	if (bpf_in_task())
		__sync_fetch_and_add(&count_task, 1);

	/* Trigger the firing of a hardirq and softirq for test. */
	bpf_kfunc_trigger_ctx_check();
	return 0;
}

/* Observer for HardIRQ */
SEC("fentry/bpf_testmod_test_hardirq_fn")
int BPF_PROG(on_hardirq)
{
	if (bpf_in_hardirq())
		__sync_fetch_and_add(&count_hardirq, 1);
	return 0;
}

/* Observer for SoftIRQ */
SEC("fentry/bpf_testmod_test_softirq_fn")
int BPF_PROG(on_softirq)
{
	if (bpf_in_serving_softirq())
		__sync_fetch_and_add(&count_softirq, 1);
	return 0;
}
