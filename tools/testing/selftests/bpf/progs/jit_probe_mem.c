// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

static struct prog_test_ref_kfunc __kptr *v;
long total_sum = -1;

SEC("tc")
int test_jit_probe_mem(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long zero = 0, sum;

	p = bpf_kfunc_call_test_acquire(&zero);
	if (!p)
		return 1;

	p = bpf_kptr_xchg(&v, p);
	if (p)
		goto release_out;

	/* Direct map value access of kptr, should be PTR_UNTRUSTED */
	p = v;
	if (!p)
		return 1;

	asm volatile (
		"r9 = %[p];"
		"%[sum] = 0;"

		/* r8 = p->a */
		"r8 = *(u32 *)(r9 + 0);"
		"%[sum] += r8;"

		/* r8 = p->b */
		"r8 = *(u32 *)(r9 + 4);"
		"%[sum] += r8;"

		"r9 += 8;"
		/* r9 = p->a */
		"r9 = *(u32 *)(r9 - 8);"
		"%[sum] += r9;"

		: [sum] "=r"(sum)
		: [p] "r"(p)
		: "r8", "r9"
	);

	total_sum = sum;
	return 0;
release_out:
	bpf_kfunc_call_test_release(p);
	return 1;
}

char _license[] SEC("license") = "GPL";
