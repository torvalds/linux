// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Bytedance */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

unsigned long span = 0;

SEC("fentry/sched_balance_rq")
int BPF_PROG(fentry_fentry, int this_cpu, struct rq *this_rq,
		struct sched_domain *sd)
{
	span = sd->span[0];

	return 0;
}

char _license[] SEC("license") = "GPL";
