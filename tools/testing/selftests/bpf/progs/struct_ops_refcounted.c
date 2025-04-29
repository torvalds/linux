#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

__attribute__((nomerge)) extern void bpf_task_release(struct task_struct *p) __ksym;

/* This is a test BPF program that uses struct_ops to access a referenced
 * kptr argument. This is a test for the verifier to ensure that it
 * 1) recongnizes the task as a referenced object (i.e., ref_obj_id > 0), and
 * 2) the same reference can be acquired from multiple paths as long as it
 *    has not been released.
 */
SEC("struct_ops/test_refcounted")
int BPF_PROG(refcounted, int dummy, struct task_struct *task)
{
	if (dummy == 1)
		bpf_task_release(task);
	else
		bpf_task_release(task);
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_refcounted = {
	.test_refcounted = (void *)refcounted,
};


