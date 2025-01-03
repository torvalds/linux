// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

#if defined(__TARGET_ARCH_x86)
bool skip __attribute((__section__(".data"))) = false;
#else
bool skip = true;
#endif

void bpf_testmod_ops3_call_test_1(void) __ksym;

int val_i, val_j;

__noinline static int subprog2(int *a, int *b)
{
	return val_i + a[1] + b[20];
}

__noinline static int subprog1(int *a)
{
	/* stack size 400 bytes */
	int b[100] = {};

	b[20] = 2;
	return subprog2(a, b);
}


SEC("struct_ops")
int BPF_PROG(test_1)
{
	/* stack size 20 bytes */
	int a[5] = {};

	a[1] = 1;
	val_j += subprog1(a);
	bpf_testmod_ops3_call_test_1();
	return 0;
}

SEC(".struct_ops")
struct bpf_testmod_ops3 testmod_1 = {
	.test_1 = (void *)test_1,
};
