// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/cgroup_inv_retcode.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test1")
__failure __msg("R0 has value (0x0; 0xffffffff)")
__naked void with_invalid_return_code_test1(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test2")
__success
__naked void with_invalid_return_code_test2(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + 0);				\
	r0 &= 1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test3")
__failure __msg("R0 has value (0x0; 0x3)")
__naked void with_invalid_return_code_test3(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + 0);				\
	r0 &= 3;					\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test4")
__success
__naked void with_invalid_return_code_test4(void)
{
	asm volatile ("					\
	r0 = 1;						\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test5")
__failure __msg("R0 has value (0x2; 0x0)")
__naked void with_invalid_return_code_test5(void)
{
	asm volatile ("					\
	r0 = 2;						\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test6")
__failure __msg("R0 is not a known value (ctx)")
__naked void with_invalid_return_code_test6(void)
{
	asm volatile ("					\
	r0 = r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("cgroup/sock")
__description("bpf_exit with invalid return code. test7")
__failure __msg("R0 has unknown scalar value")
__naked void with_invalid_return_code_test7(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + 0);				\
	r2 = *(u32*)(r1 + 4);				\
	r0 *= r2;					\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
