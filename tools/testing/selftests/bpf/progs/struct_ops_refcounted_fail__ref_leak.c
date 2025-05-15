#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

/* Test that the verifier rejects a program that acquires a referenced
 * kptr through context without releasing the reference
 */
SEC("struct_ops/test_refcounted")
__failure __msg("Unreleased reference id=1 alloc_insn=0")
int BPF_PROG(refcounted_fail__ref_leak, int dummy,
	     struct task_struct *task)
{
	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_ref_acquire = {
	.test_refcounted = (void *)refcounted_fail__ref_leak,
};
