// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

void bpf_testmod_ops3_call_test_2(void) __ksym;

int val_i, val_j;

__noinline static int subprog2(int *a, int *b)
{
	return val_i + a[10] + b[20];
}

__noinline static int subprog1(int *a)
{
	/* stack size 200 bytes */
	int b[50] = {};

	b[20] = 2;
	return subprog2(a, b);
}

/*
 * A chain of 480-byte frames under test_2, so that its call chain exceeds
 * the 2 KiB budget of JITs with large stacks as well as the 512 bytes
 * allowed elsewhere. The compiler caps a single function at 512 bytes, and
 * the buffers are volatile so that it cannot shrink them.
 */
__noinline static int subprog_deep4(int *a)
{
	volatile char b[480] = {};

	__sink(b[479]);
	return a[10] + b[20];
}

__noinline static int subprog_deep3(int *a)
{
	volatile char b[480] = {};

	__sink(b[479]);
	return subprog_deep4(a) + b[20];
}

__noinline static int subprog_deep2(int *a)
{
	volatile char b[480] = {};

	__sink(b[479]);
	return subprog_deep3(a) + b[20];
}

__noinline static int subprog_deep1(int *a)
{
	volatile char b[480] = {};

	__sink(b[479]);
	return subprog_deep2(a) + b[20];
}


SEC("struct_ops")
int BPF_PROG(test_1)
{
	/* stack size 100 bytes */
	int a[25] = {};

	a[10] = 1;
	val_i = subprog1(a);
	bpf_testmod_ops3_call_test_2();
	return 0;
}

SEC("struct_ops")
int BPF_PROG(test_2)
{
	/* stack size 476 bytes, over 2 KiB with the four 480-byte deep subprogs */
	volatile char buf[376] = {};
	int a[25] = {};

	__sink(buf[375]);
	a[10] = 3;
	val_j = subprog1(a) + subprog_deep1(a);
	return 0;
}

SEC(".struct_ops")
struct bpf_testmod_ops3 testmod_1 = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2,
};
