// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

void __kfunc_btf_root(void)
{
	bpf_kfunc_st_ops_inc10(NULL);
}

static __noinline __used int subprog(struct st_ops_args *args)
{
	args->a += 1;
	return args->a;
}

__success
/* prologue */
__xlated("0: r8 = r1")
__xlated("1: r1 = 0")
__xlated("2: call kernel-function")
__xlated("3: if r0 != 0x0 goto pc+5")
__xlated("4: r6 = *(u64 *)(r8 +0)")
__xlated("5: r7 = *(u64 *)(r6 +0)")
__xlated("6: r7 += 1000")
__xlated("7: *(u64 *)(r6 +0) = r7")
__xlated("8: goto pc+2")
__xlated("9: r1 = r0")
__xlated("10: call kernel-function")
__xlated("11: r1 = r8")
/* save __u64 *ctx to stack */
__xlated("12: *(u64 *)(r10 -8) = r1")
/* main prog */
__xlated("13: r1 = *(u64 *)(r1 +0)")
__xlated("14: r6 = r1")
__xlated("15: call kernel-function")
__xlated("16: r1 = r6")
__xlated("17: call pc+")
/* epilogue */
__xlated("18: r1 = 0")
__xlated("19: r6 = 0")
__xlated("20: call kernel-function")
__xlated("21: if r0 != 0x0 goto pc+6")
__xlated("22: r1 = *(u64 *)(r10 -8)")
__xlated("23: r1 = *(u64 *)(r1 +0)")
__xlated("24: r6 = *(u64 *)(r1 +0)")
__xlated("25: r6 += 10000")
__xlated("26: *(u64 *)(r1 +0) = r6")
__xlated("27: goto pc+2")
__xlated("28: r1 = r0")
__xlated("29: call kernel-function")
__xlated("30: r0 = r6")
__xlated("31: r0 *= 2")
__xlated("32: exit")
SEC("struct_ops/test_pro_epilogue")
__naked int test_kfunc_pro_epilogue(void)
{
	asm volatile (
	"r1 = *(u64 *)(r1 +0);"
	"r6 = r1;"
	"call %[bpf_kfunc_st_ops_inc10];"
	"r1 = r6;"
	"call subprog;"
	"exit;"
	:
	: __imm(bpf_kfunc_st_ops_inc10)
	: __clobber_all);
}

SEC("syscall")
__retval(22022) /* (PROLOGUE_A [1000] + KFUNC_INC10 + SUBPROG_A [1] + EPILOGUE_A [10000]) * 2 */
int syscall_pro_epilogue(void *ctx)
{
	struct st_ops_args args = {};

	return bpf_kfunc_st_ops_test_pro_epilogue(&args);
}

SEC(".struct_ops.link")
struct bpf_testmod_st_ops pro_epilogue_with_kfunc = {
	.test_pro_epilogue = (void *)test_kfunc_pro_epilogue,
};
