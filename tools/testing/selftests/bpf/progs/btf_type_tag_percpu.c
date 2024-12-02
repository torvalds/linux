// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Google */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct bpf_testmod_btf_type_tag_1 {
	int a;
};

struct bpf_testmod_btf_type_tag_2 {
	struct bpf_testmod_btf_type_tag_1 *p;
};

__u64 g;

SEC("fentry/bpf_testmod_test_btf_type_tag_percpu_1")
int BPF_PROG(test_percpu1, struct bpf_testmod_btf_type_tag_1 *arg)
{
	g = arg->a;
	return 0;
}

SEC("fentry/bpf_testmod_test_btf_type_tag_percpu_2")
int BPF_PROG(test_percpu2, struct bpf_testmod_btf_type_tag_2 *arg)
{
	g = arg->p->a;
	return 0;
}

/* trace_cgroup_mkdir(struct cgroup *cgrp, const char *path)
 *
 * struct cgroup_rstat_cpu {
 *   ...
 *   struct cgroup *updated_children;
 *   ...
 * };
 *
 * struct cgroup {
 *   ...
 *   struct cgroup_rstat_cpu __percpu *rstat_cpu;
 *   ...
 * };
 */
SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_percpu_load, struct cgroup *cgrp, const char *path)
{
	g = (__u64)cgrp->rstat_cpu->updated_children;
	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_percpu_helper, struct cgroup *cgrp, const char *path)
{
	struct cgroup_rstat_cpu *rstat;
	__u32 cpu;

	cpu = bpf_get_smp_processor_id();
	rstat = (struct cgroup_rstat_cpu *)bpf_per_cpu_ptr(cgrp->rstat_cpu, cpu);
	if (rstat) {
		/* READ_ONCE */
		*(volatile int *)rstat;
	}

	return 0;
}
