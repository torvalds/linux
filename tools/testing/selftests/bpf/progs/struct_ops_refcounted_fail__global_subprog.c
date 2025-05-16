#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

extern void bpf_task_release(struct task_struct *p) __ksym;

__noinline int subprog_release(__u64 *ctx __arg_ctx)
{
	struct task_struct *task = (struct task_struct *)ctx[1];
	int dummy = (int)ctx[0];

	bpf_task_release(task);

	return dummy + 1;
}

/* Test that the verifier rejects a program that contains a global
 * subprogram with referenced kptr arguments
 */
SEC("struct_ops/test_refcounted")
__failure __log_level(2)
__msg("Validating subprog_release() func#1...")
__msg("invalid bpf_context access off=8. Reference may already be released")
int refcounted_fail__global_subprog(unsigned long long *ctx)
{
	struct task_struct *task = (struct task_struct *)ctx[1];

	bpf_task_release(task);

	return subprog_release(ctx);
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_ref_acquire = {
	.test_refcounted = (void *)refcounted_fail__global_subprog,
};
