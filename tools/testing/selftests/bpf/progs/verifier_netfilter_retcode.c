// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("netfilter")
__description("bpf_exit with invalid return code. test1")
__failure __msg("R0 is not a known value")
__naked void with_invalid_return_code_test1(void)
{
	asm volatile ("					\
	r0 = *(u64*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("netfilter")
__description("bpf_exit with valid return code. test2")
__success
__naked void with_valid_return_code_test2(void)
{
	asm volatile ("					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("netfilter")
__description("bpf_exit with valid return code. test3")
__success
__naked void with_valid_return_code_test3(void)
{
	asm volatile ("					\
	r0 = 1;						\
	exit;						\
"	::: __clobber_all);
}

SEC("netfilter")
__description("bpf_exit with invalid return code. test4")
__failure __msg("R0 has smin=2 smax=2 should have been in [0, 1]")
__naked void with_invalid_return_code_test4(void)
{
	asm volatile ("					\
	r0 = 2;						\
	exit;						\
"	::: __clobber_all);
}
