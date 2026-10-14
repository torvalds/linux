// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Tests for the linear "base + step * k" description tracked per scalar register. */

#define __no_step __not_msg("step=") __msg("\n")

/*
 * Safe ALU64 arithmetic preserves a non-power-of-two stride and scales its base:
 * step=0+3 -> step=1+3 -> step=2+6 -> step=4+12.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 *= 3 {{.*}}step=0+3)")
__msg("r0 += 1 {{.*}}step=1+3)")
__msg("r0 *= 2 {{.*}}step=2+6)")
__msg("r0 <<= 1 {{.*}}step=4+12)")
__naked void step_arith64_no_overflow(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	r0 *= 3;					\
	r0 += 1;					\
	r0 *= 2;					\
	r0 <<= 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Safe ALU32 multiplication and addition preserve the stride and scale its base:
 * step=0+3 -> step=1+3 -> step=2+6. MOV32 preserves the result.
 * LSH32 currently resets the stride, but must preserve the correct upper bound.
 */
SEC("socket")
__success __log_level(2)
__msg("w0 *= 3 {{.*}}step=0+3)")
__msg("w0 += 1 {{.*}}step=1+3)")
__msg("w0 *= 2 {{.*}}step=2+6)")
__msg("w1 = w0 {{.*}}R0=scalar({{.*}}step=2+6) R1=scalar({{.*}}step=2+6)")
__msg("w0 <<= 1 {{.*}}smax=umax=smax32=umax32=3064,") __no_step
__naked void step_arith32_no_overflow(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0xff;					\
	w0 *= 3;					\
	w0 += 1;					\
	w0 *= 2;					\
	w1 = w0;					\
	w0 <<= 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Sign-crossing range with a non-power-of-2 step. After "*= 3; += -3" the value
 * set is {-3, 0, 3, 6}. The line description is tracked in signed space, so the
 * intersection keeps smax=6.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 += -3 {{.*}}smax=smax32=6,step=0+3)")
__naked void step_neg_value_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 3;					\
	r0 *= 3;					\
	r0 += -3;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * 0x49249249 * 7 wraps to U32_MAX; inferring step=0+7 would incorrectly
 * cap the result at 0xfffffffc instead of U32_MAX.
 * Adding 4 to {0xfffffffc, U32_MAX} wraps to {0, 3}; keeping step=1+3
 * incorrectly narrows the result to 1.
 * Shifting the same input left by 1 gives {0xfffffff8, 0xfffffffe};
 * inferring step=0+6 incorrectly narrows the result to 0xfffffffc.
 */
SEC("socket")
__success __log_level(2)
__msg("w0 *= 7 {{.*}}umax=0xffffffff,") __no_step
__msg("r2 = r0 {{.*}}R0=scalar({{.*}}step=0+3) R2=scalar({{.*}}step=0+3)")
__msg("w0 += 4 {{.*}}smax=umax=smax32=umax32=3,") __no_step
__msg("w2 <<= 1 {{.*}}smax=umax=umax32=0xfffffffe,") __no_step
__naked void step_arith32_overflow(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w0 *= 7;					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 1;					\
	r0 *= 3;					\
	r1 = 0xfffffffc ll;				\
	r0 += r1;					\
	r2 = r0;					\
	w0 += 4;					\
	w2 <<= 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Multiplying {0x2aaaaaaaaaaaaaab, 0x2aaaaaaaaaaaaaac} by 3 crosses S64_MAX
 * and gives {S64_MIN + 1, S64_MIN + 4}. Keeping step=0+3 excludes both.
 *
 * Adding 5 to {S64_MAX - 4, S64_MAX - 1} gives {S64_MIN, S64_MIN + 3}.
 * Updating the line to step=2+3 incorrectly narrows it to S64_MIN + 1.
 *
 * Adding -5 to {S64_MIN + 1, S64_MIN + 4} wraps to
 * {S64_MAX - 3, S64_MAX}. The stale base 0 modulo 3 excludes S64_MAX.
 *
 * Shifting {0x4000000000000008, 0x400000000000000b} left by 1 gives
 * {S64_MIN + 16, S64_MIN + 22}. Keeping step=0+6 excludes both.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 *= 3 {{.*}}smax=0x8000000000000004,") __no_step
__msg("r0 += r1 {{.*}}step=0+3)")
__msg("r0 += 5 {{.*}}smax=0x8000000000000003,") __no_step
__msg("r0 += r1 {{.*}}step=2+3)")
__msg("r0 += -5 {{.*}}umax=0x7fffffffffffffff,") __no_step
__msg("r0 += r1 {{.*}}step=0+3)")
__msg("r0 <<= 1 {{.*}}smax=0x8000000000000016,") __no_step
__naked void step_arith64_overflow(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 1;					\
	r2 = r0;					\
	r1 = 0x2aaaaaaaaaaaaaab ll;			\
	r0 += r1;					\
	r0 *= 3;					\
	r2 *= 3;					\
	r0 = r2;					\
	r1 = 0x7ffffffffffffffb ll;			\
	r0 += r1;					\
	r0 += 5;					\
	r0 = r2;					\
	r1 = 0x8000000000000001 ll;			\
	r0 += r1;					\
	r0 += -5;					\
	r0 = r2;					\
	r1 = 0x4000000000000008 ll;			\
	r0 += r1;					\
	r0 <<= 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * MOV32 maps {2^32, 2^32 + 3} to {0, 3}. The old base 1 modulo 3
 * excludes both results.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 += r1 {{.*}}step=1+3)")
__msg("w0 = w0 {{.*}}smax=umax=smax32=umax32=3,") __no_step
__naked void step_mov32_truncate(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 1;					\
	r0 *= 3;					\
	r1 = 0x100000000 ll;				\
	r0 += r1;					\
	w0 = w0;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * MOV32 maps {-4, -1} to {0xfffffffc, U32_MAX}. The old base 2
 * modulo 3 excludes U32_MAX.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 += -4 {{.*}}step=2+3)")
__msg("w0 = w0 {{.*}}umax=0xffffffff,") __no_step
__naked void step_mov32_negative(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 1;					\
	r0 *= 3;					\
	r0 += -4;					\
	w0 = w0;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Sign-extending {128, 131} from s8 gives {-128, -125}.
 * Keeping base 2 modulo 3 incorrectly narrows the result to -127.
 *
 * Sign-extending {32768, 32771} from s16 gives {-32768, -32765}.
 * Keeping base 2 modulo 3 incorrectly narrows the result to -32767.
 *
 * Sign-extending {2^31, 2^31 + 3} from s32 gives {S32_MIN, S32_MIN + 3}.
 * Keeping base 2 modulo 3 incorrectly narrows the result to S32_MIN + 1.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 += 128 {{.*}}step=2+3)")
__msg("r0 = (s8)r0 {{.*}}smax=smax32=-125,") __no_step
__msg("r0 += 32768 {{.*}}step=2+3)")
__msg("r0 = (s16)r0 {{.*}}smax=smax32=-32765,") __no_step
__msg("r0 += r1 {{.*}}step=2+3)")
__msg("r0 = (s32)r0 {{.*}}smax=0xffffffff80000003,") __no_step
__naked void step_movsx(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 1;					\
	r0 *= 3;					\
	r2 = r0;					\
	r0 += 128;					\
	r0 = (s8)r0;					\
	r0 = r2;					\
	r0 += 32768;					\
	r0 = (s16)r0;					\
	r0 = r2;					\
	r1 = 0x80000000 ll;				\
	r0 += r1;					\
	r0 = (s32)r0;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * R1 = R0 + 1 shares R0's scalar ID. Refining R0 to {0, 3, 6} must
 * translate its line as well as its bounds when updating R1 to {1, 4, 7}.
 * Copying R0's base 0 modulo 3 excludes the reachable value 7.
 */
SEC("socket")
__success __log_level(2)
__msg("r1 += 1 {{.*}}step=1+3)")
__msg("if r0 > 0x6 {{.*}}R1=scalar({{.*}}smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=7,{{.*}}step=1+3)")
__naked void step_linked_regs_add64(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 3;					\
	r0 *= 3;					\
	r1 = r0;					\
	r1 += 1;					\
	if r0 > 6 goto l_out_%=;			\
	r0 = r1;					\
l_out_%=:						\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * r0 &= 7       -> [0, 7]
 * r0 *= 24      -> [0, 168]   step=0+24
 * r0 += 8       -> [8, 176]   step=8+24
 * r0 & 0x10     -> [32, 176]  step=8+24  (var_off alone: umin=16)
 * r0 > 80       -> [104, 176] step=8+24  (var_off alone: umin=88)
 * r1 += 24      -> [128, 200] step=8+24
 * Bounds and step stay in sync after each refinement.
 */
SEC("socket")
__success __log_level(2)
__msg("r0 += 8 {{.*}}smin=umin=smin32=umin32=8,smax=umax=smax32=umax32=176,{{.*}}step=8+24)")
__msg("from 4 to 6: R0=scalar(smin=umin=smin32=umin32=32,smax=umax=smax32=umax32=176,{{.*}}step=8+24)")
__msg("r1 = r0 {{.*}}R1=scalar(id={{[0-9]+}},smin=umin=smin32=umin32=104,smax=umax=smax32=umax32=176,{{.*}}step=8+24)")
__msg("r1 += 24 {{.*}}R1=scalar({{.*}}smin=umin=smin32=umin32=128,smax=umax=smax32=umax32=200,{{.*}}step=8+24)")
__naked void step_bounds_resync_after_arith(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 7;					\
	r0 *= 24;					\
	r0 += 8;					\
	if r0 & 0x10 goto l_set_%=;			\
	goto l_out_%=;					\
l_set_%=:						\
	if r0 <= 80 goto l_out_%=;			\
	r1 = r0;					\
	r1 += 24;					\
	*(u64 *)(r10 - 8) = r1;				\
l_out_%=:						\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * r0 &= 7       -> [0, 7]
 * r0 *= 24      -> [0, 168]    step=0+24
 * r1 = (s16)r0  -> [0, 168]    step=0+24
 * r0 += -24     -> [-24, 144]  step=0+24
 * r2 = (s16)r0  -> [-24, 144]  step=0+24
 * r3 = (s8)r0   -> [-128, 127] no step
 * w4 = (s16)w1  -> [0, 168]    step=0+24
 * w5 = (s8)w1   -> [-128, 127] no step
 * Bounds and step stay in sync, the step is dropped when the value does not fit.
 */
SEC("socket")
__success __log_level(2)
__msg("r1 = (s16)r0 {{.*}}R1=scalar(id={{[0-9]+}},smin=smin32=0,smax=umax=smax32=umax32=168,{{.*}}step=0+24)")
__msg("r2 = (s16)r0 {{.*}}R2=scalar(smin=smin32=-24,smax=smax32=144,{{.*}}step=0+24)")
__msg("r3 = (s8)r0 {{.*}}R3=scalar(smin=smin32=-128,smax=smax32=127)")
__msg("w4 = (s16)w1 {{.*}}R4=scalar(id={{[0-9]+}},smin=smin32=0,smax=umax=smax32=umax32=168,{{.*}}step=0+24)")
__msg("w5 = (s8)w1 {{.*}}R5=scalar({{.*}}smin32=-128,smax32=127,var_off=(0x0; 0xffffffff))")
__naked void step_movsx_bounds_on_line(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 7;					\
	r0 *= 24;					\
	r1 = (s16)r0;					\
	r0 += -24;					\
	r2 = (s16)r0;					\
	r3 = (s8)r0;					\
	w4 = (s16)w1;					\
	w5 = (s8)w1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

struct step_val {
	__u8 data[1024];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct step_val);
} step_map SEC(".maps");

/* Old register [4..130, step 2] should prune cur register [8..68, step 4]. */
SEC("socket")
__success __log_level(2)
__msg("7: (27) r6 *= 4                       ; R6=scalar({{.*}}umin32=8,{{.*}}umax32=68,{{.*}},step=0+4)")
__msg("10: (27) r7 *= 2                      ; R7=scalar({{.*}}umin32=4,{{.*}}umax32=130,{{.*}},step=0+2)")
__msg("11: (25) if r0 > 0x2a goto pc+1")
__msg("from 11 to 13: safe")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void step_prune_hit_multiple(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r6 &= 0x0f;					\
	r6 += 2;					\
	r6 *= 4;					\
	r7 &= 0x3f;					\
	r7 += 2;					\
	r7 *= 2;					\
	if r0 > 42 goto 1f;	/* can't predict */	\
	r6 = r7;		/* step=2 explored first, step=4 explored next */ \
1:	r0 = r10;					\
	r6 = -r6;					\
	r0 += r6;					\
	*(u8 *)(r0 + 0) = 7;	/* force r6 precise */	\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Old register [0..189, step 3] should not prune cur register [0..30, step 2].
 * Bounds and tnum containment allow pruning; step divisibility must prevent it.
 */
SEC("socket")
__success __log_level(2)
__msg("6: (27) r6 *= 2                       ; R6=scalar({{.*}}smin32=0,{{.*}}umax32=30,{{.*}},step=0+2)")
__msg("8: (27) r7 *= 3                       ; R7=scalar({{.*}}smin32=0,{{.*}}umax32=189,{{.*}},step=0+3)")
__msg("9: (25) if r0 > 0x2a goto pc+1")
__msg("17: (95) exit")
__msg("from 9 to 11: R6=scalar({{.*}},step=0+2)")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void step_prune_miss_non_multiple(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r6 &= 0x0f;					\
	r6 *= 2;					\
	r7 &= 0x3f;					\
	r7 *= 3;					\
	if r0 > 42 goto 1f;	/* can't predict */	\
	r6 = r7;		/* step=3 explored first, step=2 explored next */ \
1:							\
	r2 = r10;					\
	r6 += 1;					\
	r6 = -r6;					\
	r2 += r6;					\
	*(u8 *)(r2 + 0) = 7;	/* force r6 precise */	\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Constant current register lying on the cached line: cached is {0,3,6,...}
 * (step 3, base 0), current is the constant 6. range_within() takes the
 * constant branch: imod(6, 3) == base 0, so cur is on the line and the
 * (precise) state is pruned -> "safe".
 */
SEC("socket")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("14: (27) r1 *= 3")		/* cached path: step 3 line */
__msg("16: (b7) r1 = 6")		/* current path: const 6, on the line */
__msg("17: safe")			/* pruned at join: imod(6, 3) == 0 */
__naked void step_prune_hit_const_on_line(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[step_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l_out_%=;			\
	r7 = r0;					\
	r1 = r6;					\
	r1 &= 0xff;					\
	if r6 > 0 goto l_cur_%=;			\
	r1 *= 3;			/* old: step 3 */	\
	goto l_join_%=;					\
l_cur_%=:						\
	r1 = 6;				/* cur: const on line */	\
l_join_%=:						\
	r0 = r7;					\
	r0 += r1;			/* r1 forced precise */	\
	r2 = *(u8 *)(r0 + 0);				\
l_out_%=:						\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(step_map)
	: __clobber_all);
}

/*
 * Constant current register NOT on the cached line: cached is {0,3,6,...}
 * (step 3, base 0), current is the constant 7. imod(7, 3) == 1 != base 0,
 * so range_within() fails and the join is traversed again. A power-of-two
 * step is avoided on purpose: with step 3 the tnum is loose enough to admit
 * 7, so imod() is
 * the sole check that rejects it. No failure shape is possible here: 7 is
 * within the line's bounds and tnum, and the cached line path already
 * verifies the whole outro, so an eager prune could not miss an error.
 */
SEC("socket")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("14: (27) r1 *= 3")		/* cached path: step 3 line */
__msg("16: (b7) r1 = 7")		/* current path: const 7, off the line */
/* not pruned: current continues past the join with the constant offset 7 */
__msg("19: R0=map_value({{.*}}imm=7) R1=7")
__naked void step_prune_miss_const_off_line(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[step_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l_out_%=;			\
	r7 = r0;					\
	r1 = r6;					\
	r1 &= 0xff;					\
	if r6 > 0 goto l_cur_%=;			\
	r1 *= 3;			/* old: step 3 */	\
	goto l_join_%=;					\
l_cur_%=:						\
	r1 = 7;				/* cur: const off line */	\
l_join_%=:						\
	r0 = r7;					\
	r0 += r1;			/* r1 forced precise */	\
	r2 = *(u8 *)(r0 + 0);				\
l_out_%=:						\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(step_map)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
