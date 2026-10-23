// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/*
 * kernel/bpf/loops.c:compute_loops_in_subprog() distinguishes between
 * the following cases:
 * - B: backedge -> simple loop
 * - C: cross edge to non-loop node -> no-op
 * - D: edge to node whose header is in DFS path -> nested loop
 * - E: edge to node whose header is NOT in DFS path -> irreducible
 *
 * Below test cases cover the above branches in various combinations.
 */

/* Case B: single bounded loop. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  1       0   1: {{.*}} (07) r0 += 1")
__msg("  1   1   1   2: {{.*}} (a5) if r0 < 0xa goto pc-2")
__msg("          2   3: {{.*}} (95) exit")
__naked void loop_single(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* Case B: two independent loops at the same nesting level. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("loop at 4{{$}}")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  2       0   1: {{.*}} (07) r0 += 1")
__msg("  2   1   1   2: {{.*}} (a5) if r0 < 0xa goto pc-2")
__msg("          2   3: {{.*}} (b7) r1 = 0")
__msg("  1       3   4: {{.*}} (07) r1 += 1")
__msg("  1   4   4   5: {{.*}} (a5) if r1 < 0xa goto pc-2")
__msg("          5   6: {{.*}} (95) exit")
__naked void loop_two_independent(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 < 10 goto 1b;				\
	r1 = 0;						\
2:	r1 += 1;					\
	if r1 < 10 goto 2b;				\
	exit;						\
"	::: __clobber_all);
}

/* Case B + D: nested loops. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("loop at 3, nested in 1")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  1       0   1: {{.*}} (07) r0 += 1")			/* outer loop header */
__msg("  1   1   1   2: {{.*}} (b7) r1 = 0")			/* outer loop insn */
__msg("  1   1   2   3: {{.*}} (07) r1 += 1")			/* inner loop header */
__msg("  1   3   3   4: {{.*}} (a5) if r1 < 0x5 goto pc-2")	/* inner loop insn */
__msg("  1   1   4   5: {{.*}} (a5) if r0 < 0xa goto pc-5")	/* outer loop insn */
__msg("          5   6: {{.*}} (95) exit")
__naked void loop_nested(void)
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

/* Case C: forward edges, no loops. */
SEC("socket")
__success
__log_level(2)
__msg("Program dump")
__msg("         -1   0: {{.*}} (85) call bpf_get_prandom_u32")
__msg("          0   1: {{.*}} (25) if r0 > 0x0 goto pc+2")
__msg("          1   2: {{.*}} (b7) r0 = 2")
__msg("          2   3: {{.*}} (05) goto pc+1")
__msg("          1   4: {{.*}} (b7) r0 = 3")
__msg("          1   5: {{.*}} (95) exit")
__naked void fwd_edges_no_loop(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	if r0 > 0 goto 1f;				\
	r0 = 2;						\
	goto 2f;					\
1:	r0 = 3;						\
2:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Case B + D: two sibling inner loops within one outer loop. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("loop at 3, nested in 1")
__msg("loop at 6, nested in 1")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  1       0   1: {{.*}} (07) r0 += 1")
__msg("  1   1   1   2: {{.*}} (b7) r1 = 0")
__msg("  1   1   2   3: {{.*}} (07) r1 += 1")
__msg("  1   3   3   4: {{.*}} (a5) if r1 < 0x5 goto pc-2")
__msg("  1   1   4   5: {{.*}} (b7) r2 = 0")
__msg("  1   1   5   6: {{.*}} (07) r2 += 1")
__msg("  1   6   6   7: {{.*}} (a5) if r2 < 0x5 goto pc-2")
__msg("  1   1   7   8: {{.*}} (a5) if r0 < 0xa goto pc-8")
__msg("          8   9: {{.*}} (95) exit")
__naked void loop_nested_siblings(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = 0;						\
2:	r1 += 1;					\
	if r1 < 5 goto 2b;				\
	r2 = 0;						\
3:	r2 += 1;					\
	if r2 < 5 goto 3b;				\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* Three levels of nesting. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("loop at 3, nested in 1")
__msg("loop at 5, nested in 3")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  1       0   1: {{.*}} (07) r0 += 1")
__msg("  1   1   1   2: {{.*}} (b7) r1 = 0")
__msg("  1   1   2   3: {{.*}} (07) r1 += 1")
__msg("  1   3   3   4: {{.*}} (b7) r2 = 0")
__msg("  1   3   4   5: {{.*}} (07) r2 += 1")
__msg("  1   5   5   6: {{.*}} (a5) if r2 < 0x3 goto pc-2")
__msg("  1   3   6   7: {{.*}} (a5) if r1 < 0x5 goto pc-5")
__msg("  1   1   7   8: {{.*}} (a5) if r0 < 0xa goto pc-8")
__msg("          8   9: {{.*}} (95) exit")
__naked void loop_three_levels(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = 0;						\
2:	r1 += 1;					\
	r2 = 0;						\
3:	r2 += 1;					\
	if r2 < 3 goto 3b;				\
	if r1 < 5 goto 2b;				\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* Loop with an if-else body (forward branch inside loop, Case D). */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1")
__msg("Program dump")
__msg("         -1   0: {{.*}} (b7) r0 = 0")
__msg("  1       0   1: {{.*}} (07) r0 += 1")
__msg("  1   1   1   2: {{.*}} (bf) r1 = r0")
__msg("  1   1   2   3: {{.*}} (25) if r1 > 0x5 goto pc+1")
__msg("  1   1   3   4: {{.*}} (b7) r1 = 1")
__msg("  1   1   3   5: {{.*}} (0f) r0 += r1")
__msg("  1   1   5   6: {{.*}} (a5) if r0 < 0x64 goto pc-6")
__msg("          6   7: {{.*}} (95) exit")
__naked void loop_with_if_else(void)
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

/* Case E: irreducible loop. */
SEC("socket")
__success
__log_level(2)
__msg("loop at 3, irreducible")
__msg("Program dump")
__msg("         -1   0: {{.*}} (85) call bpf_get_prandom_u32#7")
__msg("          0   1: {{.*}} (b7) r1 = 0")
__msg("          1   2: {{.*}} (25) if r0 > 0x5 goto pc+2")
__msg("  1       2   3: {{.*}} (b7) r1 = 1")
__msg("  1   3   3   4: {{.*}} (05) goto pc+1")
__msg("          2   5: {{.*}} (b7) r1 = 2")
__msg("  1   3   2   6: {{.*}} (0f) r0 += r1")
__msg("  1   3   6   7: {{.*}} (a5) if r0 < 0x10 goto pc-5")
__msg("          7   8: {{.*}} (95) exit")
__naked void loop_irreducible(void)
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

SEC("socket")
__failure
__log_level(2)
__msg("loop at 1")
__msg("infinite loop detected at insn 1")
__naked void loop_self(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
1:	if r0 < 10 goto 1b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Loops at 5 and 7 are nested inside loops at 3 and 1.
 * The edge 6 -> 7 exits loop at 5 and enters loop at 7.
 * Check that it is logged only as an exit from loop at 5.
 *
 *  0: r0 = 0;
 *     do {
 *  1:     r0++;
 *  2:     r1 = 0;
 *         do {
 *  3:         r1++;
 *  4:         r2 = 0;
 *             do {
 *  5:             r2++;
 *  6:         } while (r2 < 3);
 *             do {
 *  7:             r2--;
 *  8:         } while (r2 != 0);
 *  9:     } while (r1 < 4);
 * 10: } while (r0 < 5);
 * 11: return r0;
 */
SEC("socket")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg_next("  backedge from 10, latch at 10")
__msg_next("  exit from 10 to 11")
__msg_next("loop at 3, nested in 1")
__msg_next("  backedge from 9, latch at 9")
__msg_next("  exit from 9 to 10")
__msg_next("loop at 5, nested in 3")
__msg_next("  backedge from 6, latch at 6")
__msg_next("  exit from 6 to 7")
__msg_next("loop at 7, nested in 3")
__msg_next("  backedge from 8, latch at 8")
__msg_next("  exit from 8 to 9")
__naked void loop_nested_siblings_common_ancestors(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = 0;						\
2:	r1 += 1;					\
	r2 = 0;						\
3:	r2 += 1;					\
	if r2 < 3 goto 3b;				\
4:	r2 += -1;					\
	if r2 != 0 goto 4b;				\
	if r1 < 4 goto 2b;				\
	if r0 < 5 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * The outer loop backedge (4 -> 2) originates in an inner loop.
 * For now, latch should not be identified in such cases.
 */
SEC("socket")
__success
__log_level(2)
__msg("loop at 2{{$}}")
__msg_next("  backedge from 4{{$}}")
__msg_next("  exit from 5 to 7")
__msg_next("loop at 3, nested in 2")
__msg_next("  backedge from 6, latch at 5")
__msg_next("  exit from 4 to 2")
__msg_next("  exit from 5 to 7")
__naked void loop_outer_backedge_from_inner(void)
{
	asm volatile ("					\
	r6 = 99;					\
	r2 = 2;						\
2:	r6 += 1;			/* loop at 2 */	\
3:	r2 += 1;			/* loop at 3 */	\
4:	if r2 == 3 goto 2b;				\
5:	if r6 > 100 goto 7f;				\
	goto 3b;					\
7:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
