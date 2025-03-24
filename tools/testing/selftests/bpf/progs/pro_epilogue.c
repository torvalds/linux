// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

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
__xlated("0: r6 = *(u64 *)(r1 +0)")
__xlated("1: r7 = *(u64 *)(r6 +0)")
__xlated("2: r7 += 1000")
__xlated("3: *(u64 *)(r6 +0) = r7")
/* main prog */
__xlated("4: r1 = *(u64 *)(r1 +0)")
__xlated("5: r6 = r1")
__xlated("6: call kernel-function")
__xlated("7: r1 = r6")
__xlated("8: call pc+1")
__xlated("9: exit")
SEC("struct_ops/test_prologue")
__naked int test_prologue(void)
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

__success
/* save __u64 *ctx to stack */
__xlated("0: *(u64 *)(r10 -8) = r1")
/* main prog */
__xlated("1: r1 = *(u64 *)(r1 +0)")
__xlated("2: r6 = r1")
__xlated("3: call kernel-function")
__xlated("4: r1 = r6")
__xlated("5: call pc+")
/* epilogue */
__xlated("6: r1 = *(u64 *)(r10 -8)")
__xlated("7: r1 = *(u64 *)(r1 +0)")
__xlated("8: r6 = *(u64 *)(r1 +0)")
__xlated("9: r6 += 10000")
__xlated("10: *(u64 *)(r1 +0) = r6")
__xlated("11: r0 = r6")
__xlated("12: r0 *= 2")
__xlated("13: exit")
SEC("struct_ops/test_epilogue")
__naked int test_epilogue(void)
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

__success
/* prologue */
__xlated("0: r6 = *(u64 *)(r1 +0)")
__xlated("1: r7 = *(u64 *)(r6 +0)")
__xlated("2: r7 += 1000")
__xlated("3: *(u64 *)(r6 +0) = r7")
/* save __u64 *ctx to stack */
__xlated("4: *(u64 *)(r10 -8) = r1")
/* main prog */
__xlated("5: r1 = *(u64 *)(r1 +0)")
__xlated("6: r6 = r1")
__xlated("7: call kernel-function")
__xlated("8: r1 = r6")
__xlated("9: call pc+")
/* epilogue */
__xlated("10: r1 = *(u64 *)(r10 -8)")
__xlated("11: r1 = *(u64 *)(r1 +0)")
__xlated("12: r6 = *(u64 *)(r1 +0)")
__xlated("13: r6 += 10000")
__xlated("14: *(u64 *)(r1 +0) = r6")
__xlated("15: r0 = r6")
__xlated("16: r0 *= 2")
__xlated("17: exit")
SEC("struct_ops/test_pro_epilogue")
__naked int test_pro_epilogue(void)
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
__retval(1011) /* PROLOGUE_A [1000] + KFUNC_INC10 + SUBPROG_A [1] */
int syscall_prologue(void *ctx)
{
	struct st_ops_args args = {};

	return bpf_kfunc_st_ops_test_prologue(&args);
}

SEC("syscall")
__retval(20022) /* (KFUNC_INC10 + SUBPROG_A [1] + EPILOGUE_A [10000]) * 2 */
int syscall_epilogue(void *ctx)
{
	struct st_ops_args args = {};

	return bpf_kfunc_st_ops_test_epilogue(&args);
}

SEC("syscall")
__retval(22022) /* (PROLOGUE_A [1000] + KFUNC_INC10 + SUBPROG_A [1] + EPILOGUE_A [10000]) * 2 */
int syscall_pro_epilogue(void *ctx)
{
	struct st_ops_args args = {};

	return bpf_kfunc_st_ops_test_pro_epilogue(&args);
}

SEC(".struct_ops.link")
struct bpf_testmod_st_ops pro_epilogue = {
	.test_prologue = (void *)test_prologue,
	.test_epilogue = (void *)test_epilogue,
	.test_pro_epilogue = (void *)test_pro_epilogue,
};
