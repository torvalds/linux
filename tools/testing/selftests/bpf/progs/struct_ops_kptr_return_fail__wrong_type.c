#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct cgroup *bpf_cgroup_acquire(struct cgroup *p) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;

/* This test struct_ops BPF programs returning referenced kptr. The verifier should
 * reject programs returning a referenced kptr of the wrong type.
 */
SEC("struct_ops/test_return_ref_kptr")
__failure __msg("At program exit the register R0 is not a known value (ptr_or_null_)")
struct task_struct *BPF_PROG(kptr_return_fail__wrong_type, int dummy,
			     struct task_struct *task, struct cgroup *cgrp)
{
	struct task_struct *ret;

	ret = (struct task_struct *)bpf_cgroup_acquire(cgrp);
	bpf_task_release(task);

	return ret;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_kptr_return = {
	.test_return_ref_kptr = (void *)kptr_return_fail__wrong_type,
};
