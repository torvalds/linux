// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/loops1.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("xdp")
__description("bounded loop, count to 4")
__success __retval(4)
__naked void bounded_loop_count_to_4(void)
{
	asm volatile ("					\
	r0 = 0;						\
l0_%=:	r0 += 1;					\
	if r0 < 4 goto l0_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop, count to 20")
__success
__naked void bounded_loop_count_to_20(void)
{
	asm volatile ("					\
	r0 = 0;						\
l0_%=:	r0 += 3;					\
	if r0 < 20 goto l0_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop, count from positive unknown to 4")
__success
__naked void from_positive_unknown_to_4(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	if r0 s< 0 goto l0_%=;				\
l1_%=:	r0 += 1;					\
	if r0 < 4 goto l1_%=;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop, count from totally unknown to 4")
__success
__naked void from_totally_unknown_to_4(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
l0_%=:	r0 += 1;					\
	if r0 < 4 goto l0_%=;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop, count to 4 with equality")
__success
__naked void count_to_4_with_equality(void)
{
	asm volatile ("					\
	r0 = 0;						\
l0_%=:	r0 += 1;					\
	if r0 != 4 goto l0_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop, start in the middle")
__failure __msg("back-edge")
__naked void loop_start_in_the_middle(void)
{
	asm volatile ("					\
	r0 = 0;						\
	goto l0_%=;					\
l1_%=:	r0 += 1;					\
l0_%=:	if r0 < 4 goto l1_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__description("bounded loop containing a forward jump")
__success __retval(4)
__naked void loop_containing_a_forward_jump(void)
{
	asm volatile ("					\
	r0 = 0;						\
l1_%=:	r0 += 1;					\
	if r0 == r0 goto l0_%=;				\
l0_%=:	if r0 < 4 goto l1_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("bounded loop that jumps out rather than in")
__success
__naked void jumps_out_rather_than_in(void)
{
	asm volatile ("					\
	r6 = 0;						\
l1_%=:	r6 += 1;					\
	if r6 > 10000 goto l0_%=;			\
	call %[bpf_get_prandom_u32];			\
	goto l1_%=;					\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("tracepoint")
__description("infinite loop after a conditional jump")
__failure __msg("program is too large")
__naked void loop_after_a_conditional_jump(void)
{
	asm volatile ("					\
	r0 = 5;						\
	if r0 < 4 goto l0_%=;				\
l1_%=:	r0 += 1;					\
	goto l1_%=;					\
l0_%=:	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("bounded recursion")
__failure __msg("back-edge")
__naked void bounded_recursion(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call bounded_recursion__1;			\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void bounded_recursion__1(void)
{
	asm volatile ("					\
	r1 += 1;					\
	r0 = r1;					\
	if r1 < 4 goto l0_%=;				\
	exit;						\
l0_%=:	call bounded_recursion__1;			\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("infinite loop in two jumps")
__failure __msg("loop detected")
__naked void infinite_loop_in_two_jumps(void)
{
	asm volatile ("					\
	r0 = 0;						\
l1_%=:	goto l0_%=;					\
l0_%=:	if r0 < 4 goto l1_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("infinite loop: three-jump trick")
__failure __msg("loop detected")
__naked void infinite_loop_three_jump_trick(void)
{
	asm volatile ("					\
	r0 = 0;						\
l2_%=:	r0 += 1;					\
	r0 &= 1;					\
	if r0 < 2 goto l0_%=;				\
	exit;						\
l0_%=:	r0 += 1;					\
	r0 &= 1;					\
	if r0 < 2 goto l1_%=;				\
	exit;						\
l1_%=:	r0 += 1;					\
	r0 &= 1;					\
	if r0 < 2 goto l2_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__description("not-taken loop with back jump to 1st insn")
__success __retval(123)
__naked void back_jump_to_1st_insn_1(void)
{
	asm volatile ("					\
l0_%=:	r0 = 123;					\
	if r0 == 4 goto l0_%=;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__description("taken loop with back jump to 1st insn")
__success __retval(55)
__naked void back_jump_to_1st_insn_2(void)
{
	asm volatile ("					\
	r1 = 10;					\
	r2 = 0;						\
	call back_jump_to_1st_insn_2__1;		\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void back_jump_to_1st_insn_2__1(void)
{
	asm volatile ("					\
l0_%=:	r2 += r1;					\
	r1 -= 1;					\
	if r1 != 0 goto l0_%=;				\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__description("taken loop with back jump to 1st insn, 2")
__success __retval(55)
__naked void jump_to_1st_insn_2(void)
{
	asm volatile ("					\
	r1 = 10;					\
	r2 = 0;						\
	call jump_to_1st_insn_2__1;			\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void jump_to_1st_insn_2__1(void)
{
	asm volatile ("					\
l0_%=:	r2 += r1;					\
	r1 -= 1;					\
	if w1 != 0 goto l0_%=;				\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
