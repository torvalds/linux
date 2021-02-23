// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

extern const struct rq runqueues __ksym; /* struct type global var. */
extern const int bpf_prog_active __ksym; /* int type global var. */

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	struct rq *rq;
	int *active;
	__u32 cpu;

	cpu = bpf_get_smp_processor_id();
	rq = (struct rq *)bpf_per_cpu_ptr(&runqueues, cpu);
	active = (int *)bpf_per_cpu_ptr(&bpf_prog_active, cpu);
	if (active) {
		/* READ_ONCE */
		*(volatile int *)active;
		/* !rq has not been tested, so verifier should reject. */
		*(volatile int *)(&rq->cpu);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
