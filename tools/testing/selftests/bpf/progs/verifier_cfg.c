// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/cfg.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
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

static __naked __noinline __used int cross_subprog_target(void)
{
	asm volatile ("					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("jump across subprogram boundary")
__failure __msg("jump out of range from insn 1")
__msg("jump_across_subprog_boundary @ verifier_cfg.c")
__naked void jump_across_subprog_boundary(void)
{
	asm volatile ("					\
	call cross_subprog_target;			\
	goto +1;					\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __used int fallthrough_subprog(void)
{
	asm volatile ("					\
	r0 = 0;						\
"	::: __clobber_all);
}

SEC("socket")
__description("subprogram fallthrough")
__failure __msg("last insn is not an exit or jmp")
__msg("fallthrough_subprog @ verifier_cfg.c")
__naked void subprog_fallthrough(void)
{
	asm volatile ("					\
	call fallthrough_subprog;			\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("invalid DW LDSX instruction in diagnostics")
__failure __msg("BUG_ldx_99")
__log_level(2)
__naked void invalid_dw_ldsx(void)
{
	asm volatile ("					\
	.8byte %[ldsx_dw];				\
"	:
	: __imm_insn(ldsx_dw, BPF_RAW_INSN(BPF_LDX | BPF_MEMSX | BPF_DW, BPF_REG_0, BPF_REG_0, 0, 0))
	: __clobber_all);
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

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} map_array SEC(".maps");

extern void bpf_throw(__u64 cookie) __ksym;

/*
 * The subprogram leaves through the tail call or the exception and never
 * reaches an exit insn, so the hidden edge of the tail call has no exit to
 * point at. The stack access makes the liveness pass walk the subprogram,
 * and the loop compiles to a single jump back onto itself.
 */
__noinline __used
static int tail_call_or_throw_subprog(void *ctx)
{
	volatile __u32 idx = 0;

	bpf_tail_call(ctx, &map_array, idx);
	bpf_throw(0);
	for (;;) {}
	return 0;
}

SEC("tc")
__description("tail call in a subprogram without an exit")
__arch_x86_64
__arch_arm64
__success
__naked void tail_call_in_subprog_without_exit(void)
{
	asm volatile ("					\
	call tail_call_or_throw_subprog;		\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
