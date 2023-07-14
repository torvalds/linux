// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/cfg.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("unreachable")
__failure __msg("unreachable")
__failure_unpriv
__naked void unreachable(void)
{
	asm volatile ("					\
	exit;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("unreachable2")
__failure __msg("unreachable")
__failure_unpriv
__naked void unreachable2(void)
{
	asm volatile ("					\
	goto l0_%=;					\
	goto l0_%=;					\
l0_%=:	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("out of range jump")
__failure __msg("jump out of range")
__failure_unpriv
__naked void out_of_range_jump(void)
{
	asm volatile ("					\
	goto l0_%=;					\
	exit;						\
l0_%=:							\
"	::: __clobber_all);
}

SEC("socket")
__description("out of range jump2")
__failure __msg("jump out of range")
__failure_unpriv
__naked void out_of_range_jump2(void)
{
	asm volatile ("					\
	goto -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("loop (back-edge)")
__failure __msg("unreachable insn 1")
__msg_unpriv("back-edge")
__naked void loop_back_edge(void)
{
	asm volatile ("					\
l0_%=:	goto l0_%=;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("loop2 (back-edge)")
__failure __msg("unreachable insn 4")
__msg_unpriv("back-edge")
__naked void loop2_back_edge(void)
{
	asm volatile ("					\
l0_%=:	r1 = r0;					\
	r2 = r0;					\
	r3 = r0;					\
	goto l0_%=;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("conditional loop")
__failure __msg("infinite loop detected")
__msg_unpriv("back-edge")
__naked void conditional_loop(void)
{
	asm volatile ("					\
	r0 = r1;					\
l0_%=:	r2 = r0;					\
	r3 = r0;					\
	if r1 == 0 goto l0_%=;				\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
