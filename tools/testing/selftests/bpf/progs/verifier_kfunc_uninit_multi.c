// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

/* Keep the kfunc BTF records used by the inline assembly. */
void __kfunc_btf_root(void)
{
	asm volatile ("" :
		: "r"(&bpf_kfunc_test_uninit_multi),
		  "r"(&bpf_kfunc_test_uninit_pair),
		  "r"(&bpf_kfunc_test_uninit_stack));
}

SEC("tc")
__success __retval(42)
__flag(BPF_F_TEST_STATE_FREQ)
__caps_unpriv(CAP_BPF | CAP_NET_ADMIN)
__prepare_priv
__success_unpriv
__naked void multiple_outputs(void)
{
	asm volatile (
		"*(u64 *)(r10 - 16) = 0;"
		"*(u64 *)(r10 - 8) = 0;"
		"goto +0;"
		"r1 = r10;"
		"r1 += -16;"
		"r2 = r10;"
		"r2 += -8;"
		"r3 = 8;"
		"call %[bpf_kfunc_test_uninit_multi];"
		"r0 = *(u32 *)(r10 - 16);"
		"r1 = *(u32 *)(r10 - 8);"
		"if r1 != 0x2a2a2a2a goto 1f;"
		"r1 = *(u32 *)(r10 - 4);"
		"if r1 == 0x2a2a2a2a goto 2f;"
	"1:;"
		"r0 = 0;"
	"2:;"
		"exit;"
		: : __imm(bpf_kfunc_test_uninit_multi) : __clobber_all);
}

SEC("tc")
__success __retval(42)
__caps_unpriv(CAP_BPF | CAP_NET_ADMIN)
__prepare_priv
__failure_unpriv __msg_unpriv("invalid read from stack off -16+0 size 4")
__naked void variable_size_preserves_other_output(void)
{
	asm volatile (
		"r3 = *(u32 *)(r1 + 0);"
		"r3 &= 7;"
		"*(u64 *)(r10 - 8) = 0;"
		"r1 = r10;"
		"r1 += -16;"
		"r2 = r10;"
		"r2 += -8;"
		"call %[bpf_kfunc_test_uninit_multi];"
		"r0 = *(u32 *)(r10 - 16);"
		"exit;"
		: : __imm(bpf_kfunc_test_uninit_multi) : __clobber_all);
}

SEC("tc")
__arch_x86_64 __arch_arm64
__success __retval(42)
__caps_unpriv(CAP_BPF | CAP_NET_ADMIN)
__prepare_priv
__failure_unpriv __msg_unpriv("invalid read from stack off -8+0 size 4")
__naked void output_after_by_value_argument(void)
{
	asm volatile (
		"r1 = 20;"
		"r2 = 22;"
		"r3 = r10;"
		"r3 += -8;"
		"call %[bpf_kfunc_test_uninit_pair];"
		"r0 = *(u32 *)(r10 - 8);"
		"exit;"
		: : __imm(bpf_kfunc_test_uninit_pair) : __clobber_all);
}

#if defined(__BPF_FEATURE_STACK_ARGUMENT)
SEC("tc")
__arch_x86_64 __arch_arm64 __arch_riscv64
__success __retval(15)
__caps_unpriv(CAP_BPF | CAP_NET_ADMIN)
__prepare_priv
__failure_unpriv __msg_unpriv("invalid read from stack off -8+0 size 4")
__naked void output_passed_on_stack(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"r6 = r10;"
		"r6 += -8;"
		"*(u64 *)(r11 - 8) = r6;"
		"call %[bpf_kfunc_test_uninit_stack];"
		"r0 = *(u32 *)(r10 - 8);"
		"exit;"
		: : __imm(bpf_kfunc_test_uninit_stack) : __clobber_all);
}
#endif

char _license[] SEC("license") = "GPL";
