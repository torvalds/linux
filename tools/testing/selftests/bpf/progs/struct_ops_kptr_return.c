#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

void bpf_task_release(struct task_struct *p) __ksym;

/* This test struct_ops BPF programs returning referenced kptr. The verifier should
 * allow a referenced kptr or a NULL pointer to be returned. A referenced kptr to task
 * here is acquired automatically as the task argument is tagged with "__ref".
 */
SEC("struct_ops/test_return_ref_kptr")
struct task_struct *BPF_PROG(kptr_return, int dummy,
			     struct task_struct *task, struct cgroup *cgrp)
{
	if (dummy % 2) {
		bpf_task_release(task);
		return NULL;
	}
	return task;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_kptr_return = {
	.test_return_ref_kptr = (void *)kptr_return,
};


