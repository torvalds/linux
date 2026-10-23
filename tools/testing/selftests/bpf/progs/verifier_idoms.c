// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../../../include/linux/filter.h"

/*
 * The immediate dominator is printed by log_program() as the numeric column
 * immediately before the "<insn#>:" field of every "Program dump" line:
 *
 *   Program dump (scc? loop_header? idom insn#: live_regs_before):
 *            -1   0: ....... (b7) r0 = 0     <- idom(0) = -1 (subprog entry)
 *             0   1: ....... (07) r0 += 1    <- idom(1) = 0
 *             ^^^^^^
 *             idom insn#
 */

/* Straight-line code, with an ldimm64 in the middle. */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  0   1: {{.*}} (18) r1 = 0x1122334455667788")
__msg("{{.*}}  1   3: {{.*}} (07) r0 += 1")
__msg("{{.*}}  3   4: {{.*}} (0f) r0 += r1")
__msg("{{.*}}  4   5: {{.*}} (95) exit")
__naked void straight_line_ldimm64(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = 0x1122334455667788 ll;			\
	r0 += 1;					\
	r0 += r1;					\
	exit;						\
"	::: __clobber_all);
}

/*
 * Asymmetric if-then-else diamond: the "then" arm is 5 instructions long, the
 * "else" arm is a single instruction. The merge point (insn 8) is dominated by
 * the branch (insn 1), not by either arm. This forces idoms_intersect() to walk
 * the two predecessors up unequal postorder depths before they meet.
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (85) call bpf_get_prandom_u32#7")
__msg("{{.*}}  0   1: {{.*}} (25) if r0 > 0x0 goto pc+5")
__msg("{{.*}}  1   2: {{.*}} (b7) r1 = 1")
__msg("{{.*}}  2   3: {{.*}} (b7) r1 = 2")
__msg("{{.*}}  3   4: {{.*}} (b7) r1 = 3")
__msg("{{.*}}  4   5: {{.*}} (b7) r1 = 4")
__msg("{{.*}}  5   6: {{.*}} (05) goto pc+1")
__msg("{{.*}}  1   7: {{.*}} (b7) r1 = 9")
__msg("{{.*}}  1   8: {{.*}} (95) exit")
__naked void asymmetric_diamond(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	if r0 > 0 goto 1f;				\
	r1 = 1;						\
	r1 = 2;						\
	r1 = 3;						\
	r1 = 4;						\
	goto 2f;					\
1:	r1 = 9;						\
2:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Simple loop: the header (insn 1) is dominated by the pre-header (insn 0). The
 * back edge (insn 2 -> insn 1) must not change idom(1); the back-edge
 * predecessor folds up via idoms_intersect().
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  0   1: {{.*}} (07) r0 += 1")
__msg("{{.*}}  1   2: {{.*}} (a5) if r0 < 0xa goto pc-2")
__msg("{{.*}}  2   3: {{.*}} (95) exit")
__naked void simple_loop(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Loop with an if-else in its body. The in-loop merge point (insn 5) is
 * dominated by the in-loop branch (insn 3), combining a back-edge intersect
 * with a forward-diamond intersect.
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  0   1: {{.*}} (07) r0 += 1")
__msg("{{.*}}  1   2: {{.*}} (bf) r1 = r0")
__msg("{{.*}}  2   3: {{.*}} (25) if r1 > 0x5 goto pc+1")
__msg("{{.*}}  3   4: {{.*}} (b7) r1 = 1")
__msg("{{.*}}  3   5: {{.*}} (0f) r0 += r1")
__msg("{{.*}}  5   6: {{.*}} (a5) if r0 < 0x64 goto pc-6")
__msg("{{.*}}  6   7: {{.*}} (95) exit")
__naked void loop_with_if_else_body(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = r0;					\
	if r1 > 5 goto 2f;				\
	r1 = 1;						\
2:	r0 += r1;					\
	if r0 < 100 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Nested loops: the idom chain runs inner-header -> outer-body -> outer-header
 * -> pre-header. Exercises intersect across back edges at two nesting depths.
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  0   1: {{.*}} (07) r0 += 1")
__msg("{{.*}}  1   2: {{.*}} (b7) r1 = 0")
__msg("{{.*}}  2   3: {{.*}} (07) r1 += 1")
__msg("{{.*}}  3   4: {{.*}} (a5) if r1 < 0x5 goto pc-2")
__msg("{{.*}}  4   5: {{.*}} (a5) if r0 < 0xa goto pc-5")
__msg("{{.*}}  5   6: {{.*}} (95) exit")
__naked void nested_loops(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = 0;						\
2:	r1 += 1;					\
	if r1 < 5 goto 2b;				\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Loop header (insn 1) with two back edges (from insn 4 and insn 5), i.e. three
 * predecessors. Both back edges route through the incrementing header, so the
 * loop is bounded and verifies. Repeated idoms_intersect() at the header must
 * stay stable and keep idom(1) at the pre-header (insn 0).
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r6 = 0")
__msg("{{.*}}  0   1: {{.*}} (07) r6 += 1")
__msg("{{.*}}  1   2: {{.*}} (25) if r6 > 0xa goto pc+3")
__msg("{{.*}}  2   3: {{.*}} (bf) r7 = r6")
__msg("{{.*}}  3   4: {{.*}} (a5) if r7 < 0x5 goto pc-4")
__msg("{{.*}}  4   5: {{.*}} (05) goto pc-5")
__msg("{{.*}}  2   6: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  6   7: {{.*}} (95) exit")
__naked void multi_backedge_header(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:	r6 += 1;					\
	if r6 > 10 goto 2f;				\
	r7 = r6;					\
	if r7 < 5 goto 1b;				\
	goto 1b;					\
2:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * Irreducible CFG (loop with two entries, insn 3 and insn 5, reached from the
 * insn 2 branch). Dominators remain well-defined; this is a convergence test
 * for the fixpoint under irreducibility. Note insn 6 is dominated by the branch
 * (insn 2), not by either arm.
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (85) call bpf_get_prandom_u32#7")
__msg("{{.*}}  0   1: {{.*}} (b7) r1 = 0")
__msg("{{.*}}  1   2: {{.*}} (25) if r0 > 0x5 goto pc+2")
__msg("{{.*}}  2   3: {{.*}} (b7) r1 = 1")
__msg("{{.*}}  3   4: {{.*}} (05) goto pc+1")
__msg("{{.*}}  2   5: {{.*}} (b7) r1 = 2")
__msg("{{.*}}  2   6: {{.*}} (0f) r0 += r1")
__msg("{{.*}}  6   7: {{.*}} (a5) if r0 < 0x10 goto pc-5")
__msg("{{.*}}  7   8: {{.*}} (95) exit")
__naked void irreducible(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0;						\
	if r0 > 5 goto 2f;				\
1:	r1 = 1;						\
	goto 3f;					\
2:	r1 = 2;						\
3:	r0 += r1;					\
	if r0 < 16 goto 1b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Idoms are computed per subprog. Layout (libbpf preorder) is:
 *   main: 0..1, sub: 2..7
 * The callee entry (insn 2) must have idom -1 (subprog reset), and idoms inside
 * the callee must reference only the callee's instructions, never main's. The
 * in-callee merge (insn 7) is dominated by the in-callee branch (insn 3). The
 * branch is on a prandom value so it is not constant-folded away.
 */
static __naked __noinline __used
unsigned long idoms_diamond_sub(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	if r0 > 0 goto 1f;				\
	r0 = 1;						\
	goto 2f;					\
1:	r0 = 2;						\
2:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (85) call pc+1")
__msg("{{.*}}  0   1: {{.*}} (95) exit")
__msg("{{.*}} -1   2: {{.*}} (85) call bpf_get_prandom_u32#7")
__msg("{{.*}}  2   3: {{.*}} (25) if r0 > 0x0 goto pc+2")
__msg("{{.*}}  3   4: {{.*}} (b7) r0 = 1")
__msg("{{.*}}  4   5: {{.*}} (05) goto pc+1")
__msg("{{.*}}  3   6: {{.*}} (b7) r0 = 2")
__msg("{{.*}}  3   7: {{.*}} (95) exit")
__naked void multi_subprog(void)
{
	asm volatile ("					\
	call idoms_diamond_sub;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * The first instruction of a subprog (insn 3) is itself a loop header, i.e. it
 * has an incoming back edge. Its idom must still be -1 (the subprog entry has no
 * dominator). Exercises the idoms[start]=start ... idoms[start]=-1 handling in
 * compute_subprog_idoms().
 */
static __naked __noinline __used
unsigned long idoms_entry_header_sub(void)
{
	asm volatile ("					\
1:	r1 += 1;					\
	if r1 < 10 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (b7) r1 = 0")
__msg("{{.*}}  0   1: {{.*}} (85) call pc+1")
__msg("{{.*}}  1   2: {{.*}} (95) exit")
__msg("{{.*}} -1   3: {{.*}} (07) r1 += 1")
__msg("{{.*}}  3   4: {{.*}} (a5) if r1 < 0xa goto pc-2")
__msg("{{.*}}  4   5: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  5   6: {{.*}} (95) exit")
__naked void entry_is_loop_header(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call idoms_entry_header_sub;			\
	exit;						\
"	::: __clobber_all);
}

/*
 * Self-loop: insn 1 is its own predecessor. idom(1) must be the pre-header
 * (insn 0); the self-edge is ignored (idoms[pred] == -1 on the first pass, then
 * idoms_intersect(a == b) short-circuits). Program is rejected later for an
 * infinite loop, but the dump (and idoms) are printed before that.
 */
SEC("socket")
__failure
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (85) call bpf_get_prandom_u32#7")
__msg("{{.*}}  0   1: {{.*}} (a5) if r0 < 0xa goto pc-1")
__msg("{{.*}}  1   2: {{.*}} (95) exit")
__msg("infinite loop detected")
__naked void self_loop(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
1:	if r0 < 10 goto 1b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

#if defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_powerpc)
/*
 * Indirect jump (gotox) with a two-entry jump table. The gotox (insn 4) has two
 * successors (insn 5 and insn 7), so both targets have the gotox as their only
 * predecessor and idom.
 */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("{{.*}} -1   0: {{.*}} (18) r0 = {{0x[0-9a-f]+}}")
__msg("{{.*}}  0   2: {{.*}} (07) r0 += 8")
__msg("{{.*}}  2   3: {{.*}} (79) r0 = *(u64 *)(r0 +0)")
__msg("{{.*}}  3   4: {{.*}} (0d) gotox r0")
__msg("{{.*}}  4   5: {{.*}} (b7) r0 = 0")
__msg("{{.*}}  5   6: {{.*}} (95) exit")
__msg("{{.*}}  4   7: {{.*}} (b7) r0 = 1")
__msg("{{.*}}  7   8: {{.*}} (95) exit")
__naked void gotox_jump_table(void)
{
	asm volatile ("						\
	.pushsection .jumptables,\"\",@progbits;		\
jt0_%=:								\
	.quad ret0_%= - socket;					\
	.quad ret1_%= - socket;					\
	.size jt0_%=, 16;					\
	.global jt0_%=;						\
	.popsection;						\
								\
	r0 = jt0_%= ll;						\
	r0 += 8;						\
	r0 = *(u64 *)(r0 + 0);					\
	.8byte %[gotox_r0];					\
ret0_%=:							\
	r0 = 0;							\
	exit;							\
ret1_%=:							\
	r0 = 1;							\
	exit;							\
"	:
	: __imm_insn(gotox_r0, BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_0, 0, 0, 0))
	: __clobber_all);
}
#endif

char _license[] SEC("license") = "GPL";
