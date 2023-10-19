// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Google */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern const int bpf_prog_active __ksym;

SEC("fentry/security_inode_getattr")
int BPF_PROG(d_path_check_rdonly_mem, struct path *path, struct kstat *stat,
	     __u32 request_mask, unsigned int query_flags)
{
	void *active;
	__u32 cpu;

	cpu = bpf_get_smp_processor_id();
	active = (void *)bpf_per_cpu_ptr(&bpf_prog_active, cpu);
	if (active) {
		/* FAIL here! 'active' points to readonly memory. bpf helpers
		 * that update its arguments can not write into it.
		 */
		bpf_d_path(path, active, sizeof(int));
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
