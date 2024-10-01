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

SEC("socket")
__description("conditional loop (2)")
__success
__failure_unpriv __msg_unpriv("back-edge from insn 10 to 11")
__naked void conditional_loop2(void)
{
	asm volatile ("					\
	r9 = 2 ll;					\
	r3 = 0x20 ll;					\
	r4 = 0x35 ll;					\
	r8 = r4;					\
	goto l1_%=;					\
l0_%=:	r9 -= r3;					\
	r9 -= r4;					\
	r9 -= r8;					\
l1_%=:	r8 += r4;					\
	if r8 < 0x64 goto l0_%=;			\
	r0 = r9;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("unconditional loop after conditional jump")
__failure __msg("infinite loop detected")
__failure_unpriv __msg_unpriv("back-edge from insn 3 to 2")
__naked void uncond_loop_after_cond_jmp(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 > 0 goto l1_%=;				\
l0_%=:	r0 = 1;						\
	goto l0_%=;					\
l1_%=:	exit;						\
"	::: __clobber_all);
}


__naked __noinline __used
static unsigned long never_ending_subprog()
{
	asm volatile ("					\
	r0 = r1;					\
	goto -1;					\
"	::: __clobber_all);
}

SEC("socket")
__description("unconditional loop after conditional jump")
/* infinite loop is detected *after* check_cfg() */
__failure __msg("infinite loop detected")
__naked void uncond_loop_in_subprog_after_cond_jmp(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 > 0 goto l1_%=;				\
l0_%=:	r0 += 1;					\
	call never_ending_subprog;			\
l1_%=:	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
