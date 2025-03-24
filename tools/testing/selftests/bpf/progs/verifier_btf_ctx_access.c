// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/btf_ctx_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("fentry/bpf_modify_return_test")
__description("btf_ctx_access accept")
__success __retval(0)
__naked void btf_ctx_access_accept(void)
{
	asm volatile ("					\
	r2 = *(u64 *)(r1 + 8);		/* load 2nd argument value (int pointer) */\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("fentry/bpf_fentry_test9")
__description("btf_ctx_access u32 pointer accept")
__success __retval(0)
__naked void ctx_access_u32_pointer_accept(void)
{
	asm volatile ("					\
	r2 = *(u64 *)(r1 + 0);		/* load 1nd argument value (u32 pointer) */\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("fentry/bpf_fentry_test9")
__description("btf_ctx_access u32 pointer reject u32")
__failure __msg("size 4 must be 8")
__naked void ctx_access_u32_pointer_reject_32(void)
{
	asm volatile ("					\
	r2 = *(u32 *)(r1 + 0);		/* load 1st argument with narrow load */\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("fentry/bpf_fentry_test9")
__description("btf_ctx_access u32 pointer reject u16")
__failure __msg("size 2 must be 8")
__naked void ctx_access_u32_pointer_reject_16(void)
{
	asm volatile ("					\
	r2 = *(u16 *)(r1 + 0);		/* load 1st argument with narrow load */\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("fentry/bpf_fentry_test9")
__description("btf_ctx_access u32 pointer reject u8")
__failure __msg("size 1 must be 8")
__naked void ctx_access_u32_pointer_reject_8(void)
{
	asm volatile ("					\
	r2 = *(u8 *)(r1 + 0);		/* load 1st argument with narrow load */\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
