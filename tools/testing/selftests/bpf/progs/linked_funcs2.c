// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* weak and shared between both files */
const volatile int my_tid __weak;
long syscall_id __weak;

int output_val2;
int output_ctx2;
int output_weak2; /* should stay zero */

/* same "subprog" name in all files, but it's ok because they all are static */
static __noinline int subprog(int x)
{
	/* but different formula */
	return x * 2;
}

/* Global functions can't be void */
int set_output_val2(int x)
{
	output_val2 = 2 * x + 2 * subprog(x);
	return 2 * x;
}

/* This function can't be verified as global, as it assumes raw_tp/sys_enter
 * context and accesses syscall id (second argument). So we mark it as
 * __hidden, so that libbpf will mark it as static in the final object file,
 * right before verifying it in the kernel.
 *
 * But we don't mark it as __hidden here, rather at extern site. __hidden is
 * "contaminating" visibility, so it will get propagated from either extern or
 * actual definition (including from the losing __weak definition).
 */
void set_output_ctx2(__u64 *ctx)
{
	output_ctx2 = ctx[1]; /* long id, same as in BPF_PROG below */
}

/* this weak instance should lose, because it will be processed second */
__weak int set_output_weak(int x)
{
	output_weak2 = x;
	return 2 * x;
}

extern int set_output_val1(int x);

/* here we'll force set_output_ctx1() to be __hidden in the final obj file */
__hidden extern void set_output_ctx1(__u64 *ctx);

SEC("raw_tp/sys_enter")
int BPF_PROG(handler2, struct pt_regs *regs, long id)
{
	if (my_tid != (u32)bpf_get_current_pid_tgid() || id != syscall_id)
		return 0;

	set_output_val1(2000);
	set_output_ctx1(ctx); /* ctx definition is hidden in BPF_PROG macro */

	/* keep input value the same across both files to avoid dependency on
	 * handler call order; differentiate by output_weak1 vs output_weak2.
	 */
	set_output_weak(42);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
