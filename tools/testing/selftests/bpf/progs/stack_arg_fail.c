// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"
#include "bpf_misc.h"

#if defined(__BPF_FEATURE_STACK_ARGUMENT)

SEC("tc")
__failure __msg("Unrecognized *(R11-8) type STRUCT")
int test_stack_arg_big(struct __sk_buff *skb)
{
	struct prog_test_big_arg s = { .a = 1, .b = 2 };

	return bpf_kfunc_call_stack_arg_big(1, 2, 3, 4, 5, s);
}

SEC("socket")
__description("r11 in ALU instruction")
__failure __msg("R11 is invalid")
__naked void r11_alu_reject(void)
{
	asm volatile (
	"r11 += 1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 store with non-DW size")
__failure __msg("R11 is invalid")
__naked void r11_store_non_dw(void)
{
	asm volatile (
	"*(u32 *)(r11 - 8) = r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 store with unaligned offset")
__failure __msg("R11 is invalid")
__naked void r11_store_unaligned(void)
{
	asm volatile (
	"*(u64 *)(r11 - 4) = r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 store with positive offset")
__failure __msg("R11 is invalid")
__naked void r11_store_positive_off(void)
{
	asm volatile (
	"*(u64 *)(r11 + 8) = r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 load with negative offset")
__failure __msg("R11 is invalid")
__naked void r11_load_negative_off(void)
{
	asm volatile (
	"r0 = *(u64 *)(r11 - 8);"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 load with non-DW size")
__failure __msg("R11 is invalid")
__naked void r11_load_non_dw(void)
{
	asm volatile (
	"r0 = *(u32 *)(r11 + 8);"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__description("r11 store with zero offset")
__failure __msg("R11 is invalid")
__naked void r11_store_zero_off(void)
{
	asm volatile (
	"*(u64 *)(r11 + 0) = r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

#else

SEC("tc")
__description("stack_arg_fail: not supported, dummy test")
__success
int test_stack_arg_big(struct __sk_buff *skb)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
