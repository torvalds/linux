// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Google */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

__u64 out__runqueues_addr = -1;
__u64 out__bpf_prog_active_addr = -1;

__u32 out__rq_cpu = -1; /* percpu struct fields */
int out__bpf_prog_active = -1; /* percpu int */

__u32 out__this_rq_cpu = -1;
int out__this_bpf_prog_active = -1;

__u32 out__cpu_0_rq_cpu = -1; /* cpu_rq(0)->cpu */

extern const struct rq runqueues __ksym; /* struct type global var. */
extern const int bpf_prog_active __ksym; /* int type global var. */

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	struct rq *rq;
	int *active;
	__u32 cpu;

	out__runqueues_addr = (__u64)&runqueues;
	out__bpf_prog_active_addr = (__u64)&bpf_prog_active;

	cpu = bpf_get_smp_processor_id();

	/* test bpf_per_cpu_ptr() */
	rq = (struct rq *)bpf_per_cpu_ptr(&runqueues, cpu);
	if (rq)
		out__rq_cpu = rq->cpu;
	active = (int *)bpf_per_cpu_ptr(&bpf_prog_active, cpu);
	if (active)
		out__bpf_prog_active = *active;

	rq = (struct rq *)bpf_per_cpu_ptr(&runqueues, 0);
	if (rq) /* should always be valid, but we can't spare the check. */
		out__cpu_0_rq_cpu = rq->cpu;

	/* test bpf_this_cpu_ptr */
	rq = (struct rq *)bpf_this_cpu_ptr(&runqueues);
	out__this_rq_cpu = rq->cpu;
	active = (int *)bpf_this_cpu_ptr(&bpf_prog_active);
	out__this_bpf_prog_active = *active;

	return 0;
}

char _license[] SEC("license") = "GPL";
