// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <limits.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("scalars: find linked scalars")
__failure
__msg("math between fp pointer and 2147483647 is not allowed")
__naked void scalars(void)
{
	asm volatile ("				\
	r0 = 0;					\
	r1 = 0x80000001 ll;			\
	r1 /= 1;				\
	r2 = r1;				\
	r4 = r1;				\
	w2 += 0x7FFFFFFF;			\
	w4 += 0;				\
	if r2 == 0 goto l0_%=;			\
	exit;					\
l0_%=:						\
	r4 >>= 63;				\
	r3 = 1;					\
	r3 -= r4;				\
	r3 *= 0x7FFFFFFF;			\
	r3 += r10;				\
	*(u8*)(r3 - 1) = r0;			\
	exit;					\
"	::: __clobber_all);
}

/*
 * Test that sync_linked_regs() preserves register IDs.
 *
 * The sync_linked_regs() function copies bounds from known_reg to linked
 * registers. When doing so, it must preserve each register's original id
 * to allow subsequent syncs from the same source to work correctly.
 *
 */
SEC("socket")
__success
__naked void sync_linked_regs_preserves_id(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	r0 &= 0xff;	/* r0 in [0, 255] */			\
	r1 = r0;	/* r0, r1 linked with id 1 */		\
	r1 += 4;	/* r1 has id=1 and off=4 in [4, 259] */ \
	if r1 < 10 goto l0_%=;					\
	/* r1 in [10, 259], r0 synced to [6, 255] */		\
	r2 = r0;	/* r2 has id=1 and in [6, 255] */	\
	if r1 < 14 goto l0_%=;					\
	/* r1 in [14, 259], r0 synced to [10, 255] */		\
	if r0 >= 10 goto l0_%=;					\
	/* Never executed */					\
	r0 /= 0;						\
l0_%=:								\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success
__naked void scalars_neg(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r1 += -4;					\
	if r1 s< 0 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Same test but using BPF_SUB instead of BPF_ADD with negative immediate */
SEC("socket")
__success
__naked void scalars_neg_sub(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r1 -= 4;					\
	if r1 s< 0 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* alu32 with negative offset */
SEC("socket")
__success
__naked void scalars_neg_alu32_add(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w0 &= 0xff;					\
	w1 = w0;					\
	w1 += -4;					\
	if w1 s< 0 goto l0_%=;				\
	if w0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* alu32 with negative offset using SUB */
SEC("socket")
__success
__naked void scalars_neg_alu32_sub(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w0 &= 0xff;					\
	w1 = w0;					\
	w1 -= 4;					\
	if w1 s< 0 goto l0_%=;				\
	if w0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Positive offset: r1 = r0 + 4, then if r1 >= 6, r0 >= 2, so r0 != 0 */
SEC("socket")
__success
__naked void scalars_pos(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r1 += 4;					\
	if r1 < 6 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* SUB with negative immediate: r1 -= -4 is equivalent to r1 += 4 */
SEC("socket")
__success
__naked void scalars_sub_neg_imm(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r1 -= -4;					\
	if r1 < 6 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Double ADD clears the ID (can't accumulate offsets) */
SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_double_add(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r1 += 2;					\
	r1 += 2;					\
	if r1 < 6 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that sync_linked_regs() correctly handles large offset differences.
 * r1.off = S32_MIN, r2.off = 1, delta = S32_MIN - 1 requires 64-bit math.
 */
SEC("socket")
__success
__naked void scalars_sync_delta_overflow(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r2 = r0;					\
	r1 += %[s32_min];				\
	r2 += 1;					\
	if r2 s< 100 goto l0_%=;			\
	if r1 s< 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  [s32_min]"i"(INT_MIN)
	: __clobber_all);
}

/*
 * Another large delta case: r1.off = S32_MAX, r2.off = -1.
 * delta = S32_MAX - (-1) = S32_MAX + 1 requires 64-bit math.
 */
SEC("socket")
__success
__naked void scalars_sync_delta_overflow_large_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r1 = r0;					\
	r2 = r0;					\
	r1 += %[s32_max];				\
	r2 += -1;					\
	if r2 s< 0 goto l0_%=;				\
	if r1 s>= 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  [s32_max]"i"(INT_MAX)
	: __clobber_all);
}

/*
 * Test linked scalar tracking with alu32 and large positive offset (0x7FFFFFFF).
 * After w1 += 0x7FFFFFFF, w1 wraps to negative for any r0 >= 1.
 * If w1 is signed-negative, then r0 >= 1, so r0 != 0.
 */
SEC("socket")
__success
__naked void scalars_alu32_big_offset(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w0 &= 0xff;					\
	w1 = w0;					\
	w1 += 0x7FFFFFFF;				\
	if w1 s>= 0 goto l0_%=;				\
	if w0 != 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_alu32_basic(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	w1 += 1;					\
	if r1 > 10 goto 1f;				\
	r0 >>= 32;					\
	if r0 == 0 goto 1f;				\
	r0 /= 0;					\
1:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test alu32 linked register tracking with wrapping.
 * R0 is bounded to [0xffffff00, 0xffffffff] (high 32-bit values)
 * w1 += 0x100 causes R1 to wrap to [0, 0xff]
 *
 * After sync_linked_regs, if bounds are computed correctly:
 *   R0 should be [0x00000000_ffffff00, 0x00000000_ffffff80]
 *   R0 >> 32 == 0, so div by zero is unreachable
 *
 * If bounds are computed incorrectly (64-bit underflow):
 *   R0 becomes [0xffffffff_ffffff00, 0xffffffff_ffffff80]
 *   R0 >> 32 == 0xffffffff != 0, so div by zero is reachable
 */
SEC("socket")
__success
__naked void scalars_alu32_wrap(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w0 |= 0xffffff00;				\
	r1 = r0;					\
	w1 += 0x100;					\
	if r1 > 0x80 goto l0_%=;			\
	r2 = r0;					\
	r2 >>= 32;					\
	if r2 == 0 goto l0_%=;				\
	r0 /= 0;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that sync_linked_regs() checks reg->id (the linked target register)
 * for BPF_ADD_CONST32 rather than known_reg->id (the branch register).
 */
SEC("socket")
__success
__naked void scalars_alu32_zext_linked_reg(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	w6 = w0;		/* r6 in [0, 0xFFFFFFFF] */	\
	r7 = r6;		/* linked: same id as r6 */	\
	w7 += 1;		/* alu32: r7.id |= BPF_ADD_CONST32 */ \
	r8 = 0xFFFFffff ll;					\
	if r6 < r8 goto l0_%=;					\
	/* r6 in [0xFFFFFFFF, 0xFFFFFFFF] */			\
	/* sync_linked_regs: known_reg=r6, reg=r7 */		\
	/* CPU: w7 = (u32)(0xFFFFFFFF + 1) = 0, zext -> r7 = 0 */ \
	/* With fix: r7 64-bit = [0, 0] (zext applied) */	\
	/* Without fix: r7 64-bit = [0x100000000] (no zext) */	\
	r7 >>= 32;						\
	if r7 == 0 goto l0_%=;					\
	r0 /= 0;		/* unreachable with fix */	\
l0_%=:								\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that sync_linked_regs() skips propagation when one register used
 * alu32 (BPF_ADD_CONST32) and the other used alu64 (BPF_ADD_CONST64).
 * The delta relationship doesn't hold across different ALU widths.
 */
SEC("socket")
__failure __msg("div by zero")
__naked void scalars_alu32_alu64_cross_type(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	w6 = w0;		/* r6 in [0, 0xFFFFFFFF] */	\
	r7 = r6;		/* linked: same id as r6 */	\
	w7 += 1;		/* alu32: BPF_ADD_CONST32, delta = 1 */ \
	r8 = r6;		/* linked: same id as r6 */	\
	r8 += 2;		/* alu64: BPF_ADD_CONST64, delta = 2 */ \
	r9 = 0xFFFFffff ll;					\
	if r7 < r9 goto l0_%=;					\
	/* r7 = 0xFFFFFFFF */					\
	/* sync: known_reg=r7 (ADD_CONST32), reg=r8 (ADD_CONST64) */ \
	/* Without fix: r8 = zext(0xFFFFFFFF + 1) = 0 */	\
	/* With fix: r8 stays [2, 0x100000001] (r8 >= 2) */	\
	if r8 > 0 goto l1_%=;					\
	goto l0_%=;						\
l1_%=:								\
	r0 /= 0;		/* div by zero */		\
l0_%=:								\
	r0 = 0;						\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that regsafe() prevents pruning when two paths reach the same program
 * point with linked registers carrying different ADD_CONST flags (one
 * BPF_ADD_CONST32 from alu32, another BPF_ADD_CONST64 from alu64).
 */
SEC("socket")
__failure __msg("div by zero")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void scalars_alu32_alu64_regsafe_pruning(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	w6 = w0;		/* r6 in [0, 0xFFFFFFFF] */	\
	r7 = r6;		/* linked: same id as r6 */	\
	/* Get another random value for the path branch */	\
	call %[bpf_get_prandom_u32];				\
	if r0 > 0 goto l_pathb_%=;				\
	/* Path A: alu32 */					\
	w7 += 1;		/* BPF_ADD_CONST32, delta = 1 */\
	goto l_merge_%=;					\
l_pathb_%=:							\
	/* Path B: alu64 */					\
	r7 += 1;		/* BPF_ADD_CONST64, delta = 1 */\
l_merge_%=:							\
	/* Merge point: regsafe() compares path B against cached path A. */ \
	/* Narrow r6 to trigger sync_linked_regs for r7 */	\
	r9 = 0xFFFFffff ll;					\
	if r6 < r9 goto l0_%=;					\
	/* r6 = 0xFFFFFFFF */					\
	/* sync: r7 = 0xFFFFFFFF + 1 = 0x100000000 */		\
	/* Path A: zext -> r7 = 0 */				\
	/* Path B: no zext -> r7 = 0x100000000 */		\
	r7 >>= 32;						\
	if r7 == 0 goto l0_%=;					\
	r0 /= 0;		/* div by zero on path B */	\
l0_%=:								\
	r0 = 0;						\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success
void alu32_negative_offset(void)
{
	volatile char path[5];
	volatile int offset = bpf_get_prandom_u32();
	int off = offset;

	if (off >= 5 && off < 10)
		path[off - 5] = '.';

	/* So compiler doesn't say: error: variable 'path' set but not used */
	__sink(path[0]);
}

void dummy_calls(void)
{
	bpf_iter_num_new(0, 0, 0);
	bpf_iter_num_next(0);
	bpf_iter_num_destroy(0);
}

SEC("socket")
__success
__flag(BPF_F_TEST_STATE_FREQ)
int spurious_precision_marks(void *ctx)
{
	struct bpf_iter_num iter;

	asm volatile(
		"r1 = %[iter];"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"
	"1:"
		"r1 = %[iter];"
		"call %[bpf_iter_num_next];"
		"if r0 == 0 goto 4f;"
		"r7 = *(u32 *)(r0 + 0);"
		"r8 = *(u32 *)(r0 + 0);"
		/* This jump can't be predicted and does not change r7 or r8 state. */
		"if r7 > r8 goto 2f;"
		/* Branch explored first ties r2 and r7 as having the same id. */
		"r2 = r7;"
		"goto 3f;"
	"2:"
		/* Branch explored second does not tie r2 and r7 but has a function call. */
		"call %[bpf_get_prandom_u32];"
	"3:"
		/*
		 * A checkpoint.
		 * When first branch is explored, this would inject linked registers
		 * r2 and r7 into the jump history.
		 * When second branch is explored, this would be a cache hit point,
		 * triggering propagate_precision().
		 */
		"if r7 <= 42 goto +0;"
		/*
		 * Mark r7 as precise using an if condition that is always true.
		 * When reached via the second branch, this triggered a bug in the backtrack_insn()
		 * because r2 (tied to r7) was propagated as precise to a call.
		 */
		"if r7 <= 0xffffFFFF goto +0;"
		"goto 1b;"
	"4:"
		"r1 = %[iter];"
		"call %[bpf_iter_num_destroy];"
		:
		: __imm_ptr(iter),
		  __imm(bpf_iter_num_new),
		  __imm(bpf_iter_num_next),
		  __imm(bpf_iter_num_destroy),
		  __imm(bpf_get_prandom_u32)
		: __clobber_common, "r7", "r8"
	);

	return 0;
}

/*
 * Test that r += r (self-add, src_reg == dst_reg) clears the scalar ID
 * so that sync_linked_regs() does not propagate an incorrect delta.
 */
SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_self_add_clears_id(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	r6 = r0;		/* r6 unknown, id A */		\
	r7 = r6;		/* r7 linked to r6, id A */	\
	call %[bpf_get_prandom_u32];				\
	r8 = r0;		/* r8 unknown, id B */		\
	r9 = r8;		/* r9 linked to r8, id B */	\
	if r7 != 1 goto l_exit_%=;				\
	/* r7 == 1; sync propagates: r6 = 1 (known, id A) */	\
	r6 += r6;		/* r6 = 2; should clear id */	\
	if r7 == r9 goto l_exit_%=;				\
	/* Bug: r6 synced to r7(1)+delta(2)=3; Fix: r6 = 2 */	\
	if r6 == 3 goto l_exit_%=;				\
	r0 /= 0;						\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Same as above but with alu32 such that w6 += w6 also clears id. */
SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_self_add_alu32_clears_id(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	w6 = w0;						\
	w7 = w6;						\
	call %[bpf_get_prandom_u32];				\
	w8 = w0;						\
	w9 = w8;						\
	if w7 != 1 goto l_exit_%=;				\
	w6 += w6;						\
	if w7 == w9 goto l_exit_%=;				\
	if w6 == 3 goto l_exit_%=;				\
	r0 /= 0;						\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that stale delta from a cleared BPF_ADD_CONST does not leak
 * through assign_scalar_id_before_mov() into a new id, causing
 * sync_linked_regs() to compute an incorrect offset.
 */
SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_stale_delta_from_cleared_id(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	r6 = r0;		/* r6 unknown, gets id A */	\
	r6 += 5;		/* id A|ADD_CONST, delta 5 */	\
	r6 ^= 0;		/* id cleared; delta stays 5 */	\
	r8 = r6;		/* new id B, stale delta 5 */	\
	r8 += 3;		/* id B|ADD_CONST, delta 3 */	\
	r9 = r6;		/* id B, stale delta 5 */	\
	if r9 != 10 goto l_exit_%=;				\
	/* Bug: r8 = 10+(3-5) = 8; Fix: r8 = 10+(3-0) = 13 */	\
	if r8 == 8 goto l_exit_%=;				\
	r0 /= 0;						\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Same as above but with alu32. */
SEC("socket")
__failure
__msg("div by zero")
__naked void scalars_stale_delta_from_cleared_id_alu32(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	w6 = w0;						\
	w6 += 5;						\
	w6 ^= 0;						\
	w8 = w6;						\
	w8 += 3;						\
	w9 = w6;						\
	if w9 != 10 goto l_exit_%=;				\
	if w8 == 8 goto l_exit_%=;				\
	r0 /= 0;						\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that regsafe() verifies base_id consistency for BPF_ADD_CONST
 * linked scalars during state pruning.
 *
 * The false branch (explored first) links R3 to R2 via ADD_CONST.
 * The true branch (runtime path) links R3 to R4 (unrelated base_id).
 * At the merge point, pruning must fail because the linkage topology
 * differs.
 */
SEC("socket")
__description("linked scalars: add_const base_id must be consistent for pruning")
__failure __msg("invalid variable-offset")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void add_const_base_id_pruning(void)
{
	asm volatile ("						\
	r1 = 0;							\
	*(u64*)(r10 - 16) = r1;					\
	call %[bpf_get_prandom_u32];				\
	r6 = r0;						\
	r6 &= 1;						\
	if r6 >= 1 goto l_true_%=;				\
								\
	/* False branch (explored first, old state) */		\
	call %[bpf_get_prandom_u32];				\
	r2 = r0;						\
	r2 &= 0xff;		/* R2 = scalar(id=A) [0,255] */	\
	r3 = r2;		/* R3 linked to R2 (id=A) */	\
	r3 += 10;		/* R3 id=A|ADD_CONST, delta=10 */\
	r6 = 0;							\
	goto l_merge_%=;					\
								\
l_true_%=:							\
	/* True branch (runtime path, cur state) */		\
	call %[bpf_get_prandom_u32];				\
	r2 = r0;						\
	r2 &= 0xff;		/* R2 = scalar [0,255], id=0 */	\
	r4 = r0;						\
	r4 &= 0xff;		/* R4 = scalar [0,255], id=0 */	\
	r3 = r4;		/* R3 linked to R4 (new id=C) */\
	r3 += 10;		/* R3 id=C|ADD_CONST, delta=10 */\
	r6 = 0;							\
								\
l_merge_%=:							\
	/* At merge, old R3 linked to R2, cur R3 linked to R4. */\
	/* Pruning must fail: base_ids A vs C inconsistent. */	\
	if r2 >= 6 goto l_exit_%=;				\
	/* sync_linked_regs: R2<6 => R3<16 in old state. */	\
	/* Without fix: R3 in [10,15] from incorrect pruning. */\
	/* With fix: R3 in [10,265], not synced from R2. */	\
	r3 -= 10;		/* [0,5] vs [0,255] */		\
	r9 = r10;						\
	r9 += -16;						\
	r9 += r3;		/* fp-16+[0,5] vs fp-16+[0,255] */\
	*(u8*)(r9 + 0) = r6;	/* within 16B vs past fp */	\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
