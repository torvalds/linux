// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <stdbool.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"
#include "bpf_kfuncs.h"

#define COND_TEST(name, _initial, _step, _bound, msg, body)                     \
SEC("xdp")                                                                      \
__success                                                                       \
__log_level(2)                                                                  \
__flag(BPF_F_TEST_STATE_FREQ)                                                   \
__msg("loop header at {{[0-9]+}}, " msg)                                        \
__naked void name(void)                                                         \
{                                                                               \
	asm volatile (                                                          \
		"r6 = %[initial] ll;"                                           \
		"r7 = %[bound] ll;"                                             \
		"r9 = 0;"                                                       \
		body                                                            \
		"2: r0 = 0;"                                                    \
		"exit;"                                                         \
		:                                                               \
		: __imm_const(initial, _initial),                               \
		  __imm_const(step, _step),                                     \
		  __imm_const(bound, _bound)                                    \
		: __clobber_all);                                               \
}

/* Optional assembly runs before the tested condition, preserving its latch. */
#define PRE__COND_TEST(name, op, initial, step, bound, msg, ...)                 \
	COND_TEST(name, initial, step, bound, msg,                              \
		"1: r8 = %[step] ll;" __VA_ARGS__                               \
		"if r6 " op " r7 goto 2f;"                                      \
		"r6 += r8;"                                                     \
		"goto 1b;")

#define POST_COND_TEST(name, op, initial, step, bound, msg, ...)                \
	COND_TEST(name, initial, step, bound, msg,                              \
		"1: r8 = %[step] ll;" __VA_ARGS__                               \
		"r6 += r8;"                                                     \
		"if r6 " op " r7 goto 1b;")

/*
 * Bound fallback verification of otherwise infinite or extremely long loops.
 * JSET is not a linear condition, SCEV ignores it and the tested condition
 * alone decides the iteration count.
 */
#define LIMIT_ITERATIONS "r9 += 1; if r9 & 8 goto 2f;"

#define PRE__COND_TEST2(name, op, initial, step, bound, msg)                     \
	PRE__COND_TEST(name, op, initial, step, bound, msg, LIMIT_ITERATIONS)

#define POST_COND_TEST32(name, op, initial, step, bound, msg)                    \
	COND_TEST(name, initial, step, bound, msg,                              \
		"1: r8 = %[step] ll;" LIMIT_ITERATIONS                           \
		"w6 += w8;"                                                     \
		"if w6 " op " w7 goto 1b;")

#define ITER_U64_MAX (~0ULL)
#define ITER_S64_MAX ((1ULL << 63) - 1)
#define ITER_S64_MIN (1ULL << 63)
#define ITER_U32_MAX ((1ULL << 32) - 1)

struct map_val {
	char foo[1024];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct map_val);
} map SEC(".maps");

typeof(map) other_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, __u64);
} small_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_ARRAY);
		__uint(map_flags, BPF_F_INNER_MAP);
		__uint(max_entries, 1);
		__type(key, int);
		__type(value, struct map_val);
	});
} outer_map SEC(".maps");

struct bpf_iter__bpf_map_elem {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
	void *key;
	void *value;
};

SEC("xdp")
__success
__log_level(2)
__msg_next("scev at header 1:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__naked void simple_loop1(void)
{
	asm volatile ("					\
	r0 = 0;						\
loop_%=:						\
	if r0 == 10 goto exit_%=;			\
	r0 += 1;					\
	goto loop_%=;					\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("8: (7b) *(u64 *)(r1 +0) = r0     ; *fp-8 (+ *fp-8 1) -> ?")
__naked void indirect_write_invalidates_scev(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64 *)(r10 - 8) = r0;				\
loop_%=:						\
	r0 = *(u64 *)(r10 - 8);				\
	if r0 == 10 goto exit_%=;			\
	r0 += 1;					\
	*(u64 *)(r10 - 8) = r0;				\
	r1 = r10;					\
	r1 += -8;					\
	*(u64 *)(r1 + 0) = r0;				\
	goto loop_%=;					\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Even a no-op global callee can be freplaced with one that writes to *p. */
__noinline int global_noop_counter(__u64 *p)
{
	return 0;
}

SEC("xdp")
__success
__log_level(2)
__msg("8: (85) call pc+{{[0-9]+}} {{.*}}; *fp-8 (+ *fp-8 1) -> ?")
__naked void global_call_invalidates_scev(void)
{
	asm volatile (
	"r0 = 0;"
	"*(u64 *)(r10 - 8) = r0;"
	"r6 = 0;"
"1:"
	"r0 = *(u64 *)(r10 - 8);"
	"r0 += 1;"
	"*(u64 *)(r10 - 8) = r0;"
	"r1 = r10;"
	"r1 += -8;"
	"call global_noop_counter;"
	"r6 += 1;"
	"if r6 < 3 goto 1b;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg_next("scev at header 2:")
__msg_next("  *fp-8=(+ *fp-8 1) / (linear *fp-8 1)")
__msg_next(" scev at latch 3:")
__msg_next("  r0=*fp-8 / (linear *fp-8 1)")
__msg_next("  *fp-8=*fp-8 / (linear *fp-8 1)")
__naked void simple_loop2(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64 *)(r10 - 8) = r0;				\
loop_%=:						\
	r0 = *(u64 *)(r10 - 8);				\
	if r0 == 10 goto exit_%=;			\
	r0 = *(u64 *)(r10 - 8);				\
	r0 += 1;					\
	*(u64 *)(r10 - 8) = r0;				\
	goto loop_%=;					\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg_next("scev at header 2:")
__msg_next("  r1=(+ r1 1) / (linear r1 1)")
__naked void meet_agrees(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0;						\
1:							\
	if r1 == 2 goto 3f;				\
	if r0 == 7 goto 2f;				\
	r1 += 1;					\
	goto 1b;					\
2:							\
	r1 += 1;					\
	goto 1b;					\
3:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__failure
__log_level(2)
__msg_next("scev at header 1:")
__msg_next("  r6=(any r6 (+ r6 1)) / ?")
__naked void meet_disagrees(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:							\
	if r6 == 2 goto 3f;				\
	call %[bpf_get_prandom_u32];			\
	if r0 == 7 goto 1b;				\
	r6 += 1;					\
	goto 1b;					\
3:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Both entry state values for r1 should be accounted for */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev at header 2:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg_next("  r1=42 / (any r1 42)")
__msg_next(" scev at latch 2:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg_next("  r1=42 / (any r1 42)")
__msg("loop header at 2, widening r0 to 0..10 step 1")
__msg_next("loop header at 2, widening r1 to 7..42 step 1")
__naked void entry_state_join(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = 7;						\
1:	if r0 == 10 goto 2f;	/* r1 in [7,42] */	\
	r0 += 1; 					\
	r1 = 42;					\
	goto 1b;					\
2:	r0 = r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__log_level(2)
__msg_next("scev at header 1:")
__msg_next("  r6=(bswap32 (bswap32 (zext32 (- (- (zext32 (+ (>>...) 1))))))) / ?")
__naked void expr_chain(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:							\
	if r6 == 2 goto 2f;				\
	r6 += 1;					\
	r6 <<= 32;					\
	r6 >>= 32;					\
	w6 += 1;					\
	r6 = -r6;					\
	w6 = -w6;					\
	r6 = bswap32 r6;				\
	r6 = bswap32 r6;				\
	goto 1b;					\
2:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

#define ALU_OP(insn) "r1 = r6; " insn "; r0 += r1;"

SEC("xdp")
__success
__log_level(2)
__msg("r1 += 2 {{.*}}; r1 r6 -> (+ r6 2)")
__msg("r1 += r7 {{.*}}; r1 r6 -> (+ r6 r7)")
__msg("w1 += 2 {{.*}}; r1 r6 -> (zext32 (+ r6 2))")
__msg("w1 += w7 {{.*}}; r1 r6 -> (zext32 (+ r6 r7))")
__msg("r1 -= 2 {{.*}}; r1 r6 -> (- r6 2)")
__msg("r1 -= r7 {{.*}}; r1 r6 -> (- r6 r7)")
__msg("w1 -= 2 {{.*}}; r1 r6 -> (zext32 (- r6 2))")
__msg("w1 -= w7 {{.*}}; r1 r6 -> (zext32 (- r6 r7))")
__msg("r1 *= 2 {{.*}}; r1 r6 -> (* r6 2)")
__msg("r1 *= r7 {{.*}}; r1 r6 -> (* r6 r7)")
__msg("w1 *= 2 {{.*}}; r1 r6 -> (zext32 (* r6 2))")
__msg("w1 *= w7 {{.*}}; r1 r6 -> (zext32 (* r6 r7))")
__msg("r1 /= 2 {{.*}}; r1 r6 -> (/ r6 2)")
__msg("r1 /= r7 {{.*}}; r1 r6 -> (/ r6 r7)")
__msg("w1 /= 2 {{.*}}; r1 r6 -> (zext32 (/ r6 2))")
__msg("w1 /= w7 {{.*}}; r1 r6 -> (zext32 (/ r6 r7))")
__msg("r1 s/= 2 {{.*}}; r1 r6 -> (s/ r6 2)")
__msg("r1 s/= r7 {{.*}}; r1 r6 -> (s/ r6 r7)")
__msg("w1 s/= 2 {{.*}}; r1 r6 -> (zext32 (s/ r6 2))")
__msg("w1 s/= w7 {{.*}}; r1 r6 -> (zext32 (s/ r6 r7))")
__msg("r1 %= 2 {{.*}}; r1 r6 -> (% r6 2)")
__msg("r1 %= r7 {{.*}}; r1 r6 -> (% r6 r7)")
__msg("w1 %= 2 {{.*}}; r1 r6 -> (zext32 (% r6 2))")
__msg("w1 %= w7 {{.*}}; r1 r6 -> (zext32 (% r6 r7))")
__msg("r1 s%= 2 {{.*}}; r1 r6 -> (s% r6 2)")
__msg("r1 s%= r7 {{.*}}; r1 r6 -> (s% r6 r7)")
__msg("w1 s%= 2 {{.*}}; r1 r6 -> (zext32 (s% r6 2))")
__msg("w1 s%= w7 {{.*}}; r1 r6 -> (zext32 (s% r6 r7))")
__msg("r1 |= 2 {{.*}}; r1 r6 -> (| r6 2)")
__msg("r1 |= r7 {{.*}}; r1 r6 -> (| r6 r7)")
__msg("w1 |= 2 {{.*}}; r1 r6 -> (zext32 (| r6 2))")
__msg("w1 |= w7 {{.*}}; r1 r6 -> (zext32 (| r6 r7))")
__msg("r1 &= 2 {{.*}}; r1 r6 -> (& r6 2)")
__msg("r1 &= r7 {{.*}}; r1 r6 -> (& r6 r7)")
__msg("w1 &= 2 {{.*}}; r1 r6 -> (zext32 (& r6 2))")
__msg("w1 &= w7 {{.*}}; r1 r6 -> (zext32 (& r6 r7))")
__msg("r1 ^= 2 {{.*}}; r1 r6 -> (^ r6 2)")
__msg("r1 ^= r7 {{.*}}; r1 r6 -> (^ r6 r7)")
__msg("w1 ^= 2 {{.*}}; r1 r6 -> (zext32 (^ r6 2))")
__msg("w1 ^= w7 {{.*}}; r1 r6 -> (zext32 (^ r6 r7))")
__msg("r1 <<= 2 {{.*}}; r1 r6 -> (<< r6 2)")
__msg("r1 <<= r7 {{.*}}; r1 r6 -> (<< r6 r7)")
__msg("w1 <<= 2 {{.*}}; r1 r6 -> (zext32 (<< r6 2))")
__msg("w1 <<= w7 {{.*}}; r1 r6 -> (zext32 (<< r6 r7))")
__msg("r1 >>= 2 {{.*}}; r1 r6 -> (>> r6 2)")
__msg("r1 >>= r7 {{.*}}; r1 r6 -> (>> r6 r7)")
__msg("w1 >>= 2 {{.*}}; r1 r6 -> (zext32 (>> r6 2))")
__msg("w1 >>= w7 {{.*}}; r1 r6 -> (zext32 (>> r6 r7))")
__msg("r1 s>>= 2 {{.*}}; r1 r6 -> (s>> r6 2)")
__msg("r1 s>>= r7 {{.*}}; r1 r6 -> (s>> r6 r7)")
__msg("w1 s>>= 2 {{.*}}; r1 r6 -> (zext32 (s>> r6 2))")
__msg("w1 s>>= w7 {{.*}}; r1 r6 -> (zext32 (s>> r6 r7))")
__msg("r1 = -r1 {{.*}}; r1 r6 -> (- r6)")
__msg("w1 = -w1 {{.*}}; r1 r6 -> (zext32 (- r6))")
__msg("r1 = 42 {{.*}}; r1 r6 -> 42")
__msg("w1 = 42 {{.*}}; r1 r6 -> (zext32 42)")
__msg("r1 = r7 {{.*}}; r1 r6 -> r7")
__msg("w1 = w7 {{.*}}; r1 r6 -> (zext32 r7)")
__msg("r1 = (s8)r6 {{.*}}; r1 r6 -> (sext8 r6)")
__msg("r1 = (s16)r6 {{.*}}; r1 r6 -> (sext16 r6)")
__msg("r1 = (s32)r6 {{.*}}; r1 r6 -> (sext32 r6)")
__msg("w1 = (s8)w6 {{.*}}; r1 r6 -> (zext32 (sext8 r6))")
__msg("w1 = (s16)w6 {{.*}}; r1 r6 -> (zext32 (sext16 r6))")
__msg("r1 = bswap16 r1 {{.*}}; r1 r6 -> (bswap16 r6)")
__msg("r1 = bswap32 r1 {{.*}}; r1 r6 -> (bswap32 r6)")
__msg("r1 = bswap64 r1 {{.*}}; r1 r6 -> (bswap64 r6)")
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
__msg("r1 = le16 r1 {{.*}}; r1 r6 -> (zext16 r6)")
__msg("r1 = le32 r1 {{.*}}; r1 r6 -> (zext32 r6)")
__msg("r1 += 1 {{.*}}; r1 r6 -> (+ r6 1)")
__msg("r1 = be16 r1 {{.*}}; r1 r6 -> (bswap16 r6)")
__msg("r1 = be32 r1 {{.*}}; r1 r6 -> (bswap32 r6)")
__msg("r1 = be64 r1 {{.*}}; r1 r6 -> (bswap64 r6)")
#else
__msg("r1 = le16 r1 {{.*}}; r1 r6 -> (bswap16 r6)")
__msg("r1 = le32 r1 {{.*}}; r1 r6 -> (bswap32 r6)")
__msg("r1 = le64 r1 {{.*}}; r1 r6 -> (bswap64 r6)")
__msg("r1 = be16 r1 {{.*}}; r1 r6 -> (zext16 r6)")
__msg("r1 = be32 r1 {{.*}}; r1 r6 -> (zext32 r6)")
__msg("r1 += 1 {{.*}}; r1 r6 -> (+ r6 1)")
#endif
__naked void alu_ops_exprs(void)
{
	asm volatile (
	"r6 = 7;"
	"r7 = 2;"
	"r8 = 0;"
"1:"
	"r0 = 0;"
	ALU_OP("r1 += 2")
	ALU_OP("r1 += r7")
	ALU_OP("w1 += 2")
	ALU_OP("w1 += w7")
	ALU_OP("r1 -= 2")
	ALU_OP("r1 -= r7")
	ALU_OP("w1 -= 2")
	ALU_OP("w1 -= w7")
	ALU_OP("r1 *= 2")
	ALU_OP("r1 *= r7")
	ALU_OP("w1 *= 2")
	ALU_OP("w1 *= w7")
	ALU_OP("r1 /= 2")
	ALU_OP("r1 /= r7")
	ALU_OP("w1 /= 2")
	ALU_OP("w1 /= w7")
	ALU_OP("r1 s/= 2")
	ALU_OP("r1 s/= r7")
	ALU_OP("w1 s/= 2")
	ALU_OP("w1 s/= w7")
	ALU_OP("r1 %%= 2")
	ALU_OP("r1 %%= r7")
	ALU_OP("w1 %%= 2")
	ALU_OP("w1 %%= w7")
	ALU_OP("r1 s%%= 2")
	ALU_OP("r1 s%%= r7")
	ALU_OP("w1 s%%= 2")
	ALU_OP("w1 s%%= w7")
	ALU_OP("r1 |= 2")
	ALU_OP("r1 |= r7")
	ALU_OP("w1 |= 2")
	ALU_OP("w1 |= w7")
	ALU_OP("r1 &= 2")
	ALU_OP("r1 &= r7")
	ALU_OP("w1 &= 2")
	ALU_OP("w1 &= w7")
	ALU_OP("r1 ^= 2")
	ALU_OP("r1 ^= r7")
	ALU_OP("w1 ^= 2")
	ALU_OP("w1 ^= w7")
	ALU_OP("r1 <<= 2")
	ALU_OP("r1 <<= r7")
	ALU_OP("w1 <<= 2")
	ALU_OP("w1 <<= w7")
	ALU_OP("r1 >>= 2")
	ALU_OP("r1 >>= r7")
	ALU_OP("w1 >>= 2")
	ALU_OP("w1 >>= w7")
	ALU_OP("r1 s>>= 2")
	ALU_OP("r1 s>>= r7")
	ALU_OP("w1 s>>= 2")
	ALU_OP("w1 s>>= w7")
	ALU_OP("r1 = -r1")
	ALU_OP("w1 = -w1")
	ALU_OP("r1 = 42")
	ALU_OP("w1 = 42")
	ALU_OP("r1 = r7")
	ALU_OP("w1 = w7")
	ALU_OP("r1 = (s8)r6")
	ALU_OP("r1 = (s16)r6")
	ALU_OP("r1 = (s32)r6")
	ALU_OP("w1 = (s8)w6")
	ALU_OP("w1 = (s16)w6")
	ALU_OP("r1 = bswap16 r1")
	ALU_OP("r1 = bswap32 r1")
	ALU_OP("r1 = bswap64 r1")
	ALU_OP("r1 = le16 r1")
	ALU_OP("r1 = le32 r1")
	/*
	 * Expressions unchanged by transfer() do not appear in the log.
	 * Add r1 += 1 to touch the expression and make it appear in the log.
	 */
	ALU_OP("r1 = le64 r1; r1 += 1")
	ALU_OP("r1 = be16 r1")
	ALU_OP("r1 = be32 r1")
	ALU_OP("r1 = be64 r1; r1 += 1")
	"r8 += 1;"
	"if r8 < 2 goto 1b;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

#undef ALU_OP

/* Reset operands and consume results to keep transfer changes live. */
#define ST_OP(stmt)							\
	"r0 = 0; r1 = 1; r2 = 2;"					\
	"*(u64 *)(r10 - 8) = -8;"					\
	stmt ";"							\
	"r0 += r1; r0 += r2;"						\
	"r3 = *(u64 *)(r10 - 8); r0 += r3;"

SEC("xdp")
__success
__log_level(2)
__msg("*(u64 *)(r10 -8) = r1 {{.*}}; *fp-8 -8 -> 1")
__msg("*(u32 *)(r10 -8) = r1 {{.*}}; *fp-8 -8 -> (spill32 1)")
__msg("*(u16 *)(r10 -8) = r1 {{.*}}; *fp-8 -8 -> (spill16 1)")
__msg("*(u8 *)(r10 -8) = r1 {{.*}}; *fp-8 -8 -> (spill8 1)")
__msg("r1 = *(u64 *)(r10 -8) {{.*}}; r1 1 -> -8")
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
__msg("r1 = *(u32 *)(r10 -8) {{.*}}; r1 1 -> (zext32 -8)")
__msg("r1 = *(u16 *)(r10 -8) {{.*}}; r1 1 -> (zext16 -8)")
__msg("r1 = *(u8 *)(r10 -8) {{.*}}; r1 1 -> (zext8 -8)")
__msg("r1 = *(s32 *)(r10 -8) {{.*}}; r1 1 -> (sext32 -8)")
__msg("r1 = *(s16 *)(r10 -8) {{.*}}; r1 1 -> (sext16 -8)")
__msg("r1 = *(s8 *)(r10 -8) {{.*}}; r1 1 -> (sext8 -8)")
__msg("r1 = load_acquire((u8 *)(r10 -8)) {{.*}}; r1 1 -> (zext8 -8)")
__msg("r1 = load_acquire((u16 *)(r10 -8)) {{.*}}; r1 1 -> (zext16 -8)")
__msg("r1 = load_acquire((u32 *)(r10 -8)) {{.*}}; r1 1 -> (zext32 -8)")
#else
__msg("r1 = *(u32 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = *(u16 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = *(u8 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = *(s32 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = *(s16 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = *(s8 *)(r10 -8) {{.*}}; r1 1 -> _")
__msg("r1 = load_acquire((u8 *)(r10 -8)) {{.*}}; r1 1 -> _")
__msg("r1 = load_acquire((u16 *)(r10 -8)) {{.*}}; r1 1 -> _")
__msg("r1 = load_acquire((u32 *)(r10 -8)) {{.*}}; r1 1 -> _")
#endif
__msg("r2 = load_acquire((u64 *)(r10 -8)) {{.*}}; r2 2 -> -8")
__msg("store_release((u8 *)(r10 -8), r1) {{.*}}; *fp-8 -8 -> (spill8 1)")
__msg("store_release((u16 *)(r10 -8), r1) {{.*}}; *fp-8 -8 -> (spill16 1)")
__msg("store_release((u32 *)(r10 -8), r1) {{.*}}; *fp-8 -8 -> (spill32 1)")
__msg("store_release((u64 *)(r10 -8), r1) {{.*}}; *fp-8 -8 -> 1")
__msg("lock *(u64 *)(r10 -8) += r1 {{.*}}; *fp-8 -8 -> _")
__msg("r1 = atomic64_fetch_add((u64 *)(r10 -8), r1) {{.*}}; r1 1 -> _, *fp-8 -8 -> _")
__msg("r1 = atomic_fetch_add((u32 *)(r10 -8), r1) {{.*}}; r1 1 -> _, *fp-8 -8 -> _")
__msg("r1 = atomic64_xchg((u64 *)(r10 -8), r1) {{.*}}; r1 1 -> _, *fp-8 -8 -> _")
__msg("r0 = atomic64_cmpxchg((u64 *)(r10 -8), r0, r2) {{.*}}; r0 0 -> _, *fp-8 -8 -> _")
__msg("*(u64 *)(r10 -8) = 42 {{.*}}; *fp-8 -8 -> 42")
__msg("*(u8 *)(r10 -7) = r0 {{.*}}; *fp-8 -8 -> _")
__naked void store_load_exprs(void)
{
	asm volatile (
	"r7 = 0;"
"1:"
	ST_OP("*(u64 *)(r10 - 8) = r1")
	ST_OP("*(u32 *)(r10 - 8) = r1")
	ST_OP("*(u16 *)(r10 - 8) = r1")
	ST_OP("*(u8 *)(r10 - 8) = r1")

	ST_OP("r1 = *(u64 *)(r10 - 8)")
	ST_OP("r1 = *(u32 *)(r10 - 8)")
	ST_OP("r1 = *(u16 *)(r10 - 8)")
	ST_OP("r1 = *(u8 *)(r10 - 8)")
	ST_OP("r1 = *(s32 *)(r10 - 8)")
	ST_OP("r1 = *(s16 *)(r10 - 8)")
	ST_OP("r1 = *(s8 *)(r10 - 8)")

	ST_OP(".8byte %[load_acquire8]")
	ST_OP(".8byte %[load_acquire16]")
	ST_OP(".8byte %[load_acquire32]")
	ST_OP(".8byte %[load_acquire64]")
	ST_OP(".8byte %[store_release8]")
	ST_OP(".8byte %[store_release16]")
	ST_OP(".8byte %[store_release32]")
	ST_OP(".8byte %[store_release64]")

	ST_OP(".8byte %[atomic_add64]")
	ST_OP(".8byte %[atomic_fetch_add64]")
	ST_OP(".8byte %[atomic_fetch_add32]")
	ST_OP(".8byte %[atomic_xchg64]")
	ST_OP(".8byte %[atomic_cmpxchg64]")

	ST_OP("*(u64 *)(r10 - 8) = 42")
	ST_OP("*(u8 *)(r10 - 7) = r0")

	"r7 += 1;"
	"if r7 < 2 goto 1b;"
	"r0 = 0;"
	"exit;"
	:
	: __imm_insn(atomic_add64,       BPF_ATOMIC_OP(BPF_DW, BPF_ADD,       BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(atomic_xchg64,      BPF_ATOMIC_OP(BPF_DW, BPF_XCHG,      BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(load_acquire8,      BPF_ATOMIC_OP(BPF_B,  BPF_LOAD_ACQ,  BPF_REG_1,  BPF_REG_10, -8)),
	  __imm_insn(load_acquire16,     BPF_ATOMIC_OP(BPF_H,  BPF_LOAD_ACQ,  BPF_REG_1,  BPF_REG_10, -8)),
	  __imm_insn(load_acquire32,     BPF_ATOMIC_OP(BPF_W,  BPF_LOAD_ACQ,  BPF_REG_1,  BPF_REG_10, -8)),
	  __imm_insn(load_acquire64,     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ,  BPF_REG_2,  BPF_REG_10, -8)),
	  __imm_insn(store_release8,     BPF_ATOMIC_OP(BPF_B,  BPF_STORE_REL, BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(store_release16,    BPF_ATOMIC_OP(BPF_H,  BPF_STORE_REL, BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(store_release32,    BPF_ATOMIC_OP(BPF_W,  BPF_STORE_REL, BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(store_release64,    BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_1,  -8)),
	  __imm_insn(atomic_cmpxchg64,   BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG,   BPF_REG_10, BPF_REG_2,  -8)),
	  __imm_insn(atomic_fetch_add32, BPF_ATOMIC_OP(BPF_W,  BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8)),
	  __imm_insn(atomic_fetch_add64, BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

#undef ST_OP

SEC("xdp")
__success
__log_level(2)
__msg_next("scev at header 1:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next("  r7=?")
__msg_next(" scev at latch 1:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next("  r7=?")
__msg_next("scev at header 4:")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg_next(" scev at latch 4:")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__naked void nested_loop1(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:							\
	if r6 == 2 goto 2f;				\
	r6 += 1;					\
	r7 = 0;						\
3:							\
	if r7 == 2 goto 4f;				\
	r7 += 1;					\
	goto 3b;					\
4:							\
	goto 1b;					\
2:							\
	r0 = r7;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__success
__msg("loop at 1")
__msg_next("  backedge from 4{{$}}")
__msg_next("  exit from 1 to 7")
__msg_next("loop at 4, nested in 1")
__msg_next("  backedge from 6, latch at 4")
__msg_next("  exit from 4 to 1")
__msg("scev at header 1:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next("  r7=?")
__msg_next("scev at header 4:")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg_next(" scev at latch 4:")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg("loop header at 1, latch not identified")
__msg("loop header at 4, widening r7 to 0..2 step 1")
__log_level(2)
__naked void nested_loop_hdr_backedge1(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:							\
	if r6 == 2 goto 3f;				\
	r6 += 1;					\
	r7 = 0;						\
2:							\
	  if r7 == 2 goto 1b;				\
	  r7 += 1;					\
	  goto 2b;					\
3:							\
	r0 = r7;					\
	exit;						\
"	::: __clobber_all);
}

/*
 * Outer loop {2, 3, 4, 5} contains inner loop {3, 4, 5}.
 * The outer loop's only backedge is 4 -> 2, and its latch is also at 4,
 * inside the inner loop. Outer loop's latch index is not computed in
 * such a case, as there is no mechanism to build SCEVs for such latches
 * at the moment.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop at 2{{$}}")
__msg_next("  backedge from 4{{$}}")
__msg_next("  exit from 5 to 6")
__msg_next("loop at 3, nested in 2")
__msg_next("  backedge from 5, latch at 5")
__msg_next("  exit from 4 to 2")
__msg_next("  exit from 5 to 6")
__msg("scev at header 2:")
__msg_next("  r6=?")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg_next("scev at header 3:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next(" scev at latch 5:")
__msg_next("  r6=(+ r6 1) / (linear (+ r6 1) 1)")
__msg("loop header at 2, latch not identified")
__msg("loop header at 3, header_count is [0..2] ")
__naked void nested_loop_hdr_backedge2(void)
{
	asm volatile ("					\
	r6 = 0;						\
	r7 = 0;						\
1:	r7 += 1;					\
2:	r6 += 1;					\
	if r7 == 2 goto 1b;				\
	if r6 < 2 goto 2b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * Logical nest: for (r6 = 0; r6 < 3; r6++)
 *                   for (r7 = 0; r7 < 4; r7++) r8++;
 * Both backedges target header 3, so the CFG has one loop. r6 advances only
 * on the outer backedge and r7 resets there; neither has a linear SCEV.
 * r8 advances on both backedges and retains its linear SCEV.
 */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop at 3{{$}}")
__msg("scev at header 3:")
__msg_next("  r6=(any r6 (+ r6 1)) / ?")
__msg_next("  r7=(any (+ r7 1) 0) / ?")
__msg_next("  r8=(+ r8 1) / (linear r8 1)")
__msg("loop header at 3, unsupported loop: multiple backedges")
__naked void nested_loops_shared_header(void)
{
	asm volatile ("					\
	r6 = 0;						\
	r7 = 0;						\
	r8 = 0;						\
1:	r8 += 1;					\
	r7 += 1;					\
	if r7 < 4 goto 1b;				\
	r7 = 0;						\
	r6 += 1;					\
	if r6 < 3 goto 1b;				\
	r0 = r8;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__success
__log_level(2)
__msg("loop header at 1, header_count is 10")
__msg("loop header at 5, header_count is 5")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void nested_loops_outer_latch_true_branch_to_inner(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;		/* outer loop */	\
	r1 = 0;						\
	if r0 < 10 goto 2f;	/* outer latch true branch jumps into inner loop */ \
	goto 3f;					\
2:	  r1 += 1;		/* inner loop */	\
	  if r1 < 5 goto 2b;				\
	goto 1b;					\
3:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * Exact boundary hits for every continuation opcode.
 *
 * Example: pre_lt
 *
 *    u64 r6 = 1;
 *    while (r6 < 7) {               // r6 ∈ [1,3,5,7]
 *        r6 += 2;                   // r6 ∈ [3,5,7]
 *    }
 */
/*              name               op              initial          step             bound  expected message */
PRE__COND_TEST (pre_lt,            ">=",                 1,            2,                7, "header_count is 4 ")
POST_COND_TEST (post_lt,           "<",                  1,            2,                7, "header_count is 3 ")
PRE__COND_TEST (pre_le,            ">",                  1,            2,                7, "header_count is 5 ")
POST_COND_TEST (post_le,           "<=",                 1,            2,                7, "header_count is 4 ")
PRE__COND_TEST (pre_gt,            "<=",                 9,           -2,                3, "header_count is 4 ")
POST_COND_TEST (post_gt,           ">",                  9,           -2,                3, "header_count is 3 ")
PRE__COND_TEST (pre_ge,            "<",                  9,           -2,                3, "header_count is 5 ")
POST_COND_TEST (post_ge,           ">=",                 9,           -2,                3, "header_count is 4 ")
PRE__COND_TEST (pre_slt,           "s>=",               -5,            2,                1, "header_count is 4 ")
POST_COND_TEST (post_slt,          "s<",                -5,            2,                1, "header_count is 3 ")
PRE__COND_TEST (pre_sle,           "s>",                -5,            2,                1, "header_count is 5 ")
POST_COND_TEST (post_sle,          "s<=",               -5,            2,                1, "header_count is 4 ")
PRE__COND_TEST (pre_sgt,           "s<=",                5,           -2,               -1, "header_count is 4 ")
POST_COND_TEST (post_sgt,          "s>",                 5,           -2,               -1, "header_count is 3 ")
PRE__COND_TEST (pre_sge,           "s<",                 5,           -2,               -1, "header_count is 5 ")
POST_COND_TEST (post_sge,          "s>=",                5,           -2,               -1, "header_count is 4 ")
PRE__COND_TEST (pre_ne,            "==",                 1,            2,                7, "header_count is 4 ")
POST_COND_TEST (post_ne,           "!=",                 1,            2,                7, "header_count is 3 ")
PRE__COND_TEST (pre_ne_down,       "==",                 9,           -2,                3, "header_count is 4 ")
POST_COND_TEST (post_ne_down,      "!=",                 9,           -2,                3, "header_count is 3 ")

/*
 * Bounds between two consecutive counter values.
 *
 * Example: pre_lt_round
 *
 *    u64 r6 = 1;
 *    while (r6 < 8) {               // r6 ∈ [1,3,5,7,9]
 *        r6 += 2;                   // r6 ∈ [3,5,7,9]
 *    }
 */
PRE__COND_TEST (pre_lt_round,      ">=",                 1,            2,                8, "header_count is 5 ")
PRE__COND_TEST (pre_le_round,      ">",                  1,            2,                8, "header_count is 5 ")
PRE__COND_TEST (pre_gt_round,      "<=",                 9,           -2,                2, "header_count is 5 ")
PRE__COND_TEST (pre_ge_round,      "<",                  9,           -2,                2, "header_count is 5 ")
PRE__COND_TEST (pre_slt_round,     "s>=",               -5,            2,                2, "header_count is 5 ")
PRE__COND_TEST (pre_sle_round,     "s>",                -5,            2,                2, "header_count is 5 ")
PRE__COND_TEST (pre_sgt_round,     "s<=",                5,           -2,               -2, "header_count is 5 ")
PRE__COND_TEST (pre_sge_round,     "s<",                 5,           -2,               -2, "header_count is 5 ")

/*
 * Equality on the first non-strict comparison still takes a backedge.
 *
 * Example: pre_le_eq
 *
 *    u64 r6 = 7;
 *    while (r6 <= 7) {              // r6 ∈ [7,9]
 *        r6 += 2;                   // r6 ∈ [9]
 *    }
 */
PRE__COND_TEST (pre_le_eq,         ">",                  7,            2,                7, "header_count is 2 ")
PRE__COND_TEST (pre_ge_eq,         "<",                  3,           -2,                3, "header_count is 2 ")
PRE__COND_TEST (pre_sle_eq,        "s>",                 1,            2,                1, "header_count is 2 ")
PRE__COND_TEST (pre_sge_eq,        "s<",                -1,           -2,               -1, "header_count is 2 ")

/*
 * Unsigned order across the sign bit.
 *
 * Example: pre_lt_sign
 *
 *    u64 H = 1ULL << 63, r6 = H - 2;
 *    while (r6 < H + 1) {           // r6 ∈ [H-2,H-1,H,H+1]
 *        r6 += 1;                   // r6 ∈ [H-1,H,H+1]
 *    }
 */
PRE__COND_TEST (pre_lt_sign,       ">=",  ITER_S64_MAX - 1,            1, ITER_S64_MIN + 1, "header_count is 4 ")
PRE__COND_TEST (pre_gt_sign,       "<=",  ITER_S64_MIN + 1,           -1, ITER_S64_MAX - 1, "header_count is 4 ")

/*
 * The first latch comparison is false.
 *
 * Example: pre_lt_false
 *
 *    u64 r6 = 7;
 *    while (r6 < 7) {               // r6 ∈ [7]
 *        r6 += 2;                   // unreachable
 *    }
 */
PRE__COND_TEST (pre_lt_false,      ">=",                 7,            2,                7, "can't compute iterations count")
PRE__COND_TEST (pre_le_false,      ">",                  8,            2,                7, "can't compute iterations count")
PRE__COND_TEST (pre_gt_false,      "<=",                 3,           -2,                3, "can't compute iterations count")
PRE__COND_TEST (pre_ge_false,      "<",                  2,           -2,                3, "can't compute iterations count")
PRE__COND_TEST (pre_slt_false,     "s>=",                1,            2,                1, "can't compute iterations count")
PRE__COND_TEST (pre_sle_false,     "s>",                 2,            2,                1, "can't compute iterations count")
PRE__COND_TEST (pre_sgt_false,     "s<=",               -1,           -2,               -1, "can't compute iterations count")
PRE__COND_TEST (pre_sge_false,     "s<",                -2,           -2,               -1, "can't compute iterations count")
PRE__COND_TEST (pre_ne_false,      "==",                 7,            2,                7, "can't compute iterations count")

/*
 * The counter moves away from the exit; bound fallback verification.
 *
 * Example: pre_lt_dir
 *
 *    u64 r6 = 5, r9 = 0;
 *    while (++r9 <= 8 && r6 < 7) {  // r6 ∈ [5,3,1,U64_MAX]
 *        r6 -= 2;                   // r6 ∈ [3,1,U64_MAX]
 *    }
 */
PRE__COND_TEST2(pre_lt_dir,        ">=",                 5,           -2,                7, "can't compute iterations count")
PRE__COND_TEST2(pre_le_dir,        ">",                  5,           -2,                7, "can't compute iterations count")
PRE__COND_TEST2(pre_gt_dir,        "<=",                 5,            2,                3, "can't compute iterations count")
PRE__COND_TEST2(pre_ge_dir,        "<",                  5,            2,                3, "can't compute iterations count")
PRE__COND_TEST2(pre_slt_dir,       "s>=",               -1,           -2,                1, "can't compute iterations count")
PRE__COND_TEST2(pre_sle_dir,       "s>",                -1,           -2,                1, "can't compute iterations count")
PRE__COND_TEST2(pre_sgt_dir,       "s<=",                1,            2,               -1, "can't compute iterations count")
PRE__COND_TEST2(pre_sge_dir,       "s<",                 1,            2,               -1, "can't compute iterations count")

/*
 * The predicted exit would wrap the counter.
 *
 * Example: pre_lt_wrap
 *
 *    u64 M = U64_MAX, r6 = M - 1, r9 = 0;
 *    while (++r9 <= 8 && r6 < M) {  // r6 ∈ [M-1,0,2,...,14]
 *        r6 += 2;                   // r6 ∈ [0,2,...,14]
 *    }
 */
PRE__COND_TEST2(pre_lt_wrap,       ">=",  ITER_U64_MAX - 1,            2,     ITER_U64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_le_wrap,       ">",       ITER_U64_MAX,            1,     ITER_U64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_gt_wrap,       "<=",                 1,           -2,                0, "can't compute iterations count")
PRE__COND_TEST2(pre_ge_wrap,       "<",                  0,           -1,                0, "can't compute iterations count")
PRE__COND_TEST2(pre_slt_wrap,      "s>=", ITER_S64_MAX - 1,            2,     ITER_S64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_sle_wrap,      "s>",      ITER_S64_MAX,            1,     ITER_S64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_sgt_wrap,      "s<=", ITER_S64_MIN + 1,           -2,     ITER_S64_MIN, "can't compute iterations count")
PRE__COND_TEST2(pre_sge_wrap,      "s<",      ITER_S64_MIN,           -1,     ITER_S64_MIN, "can't compute iterations count")

/*
 * The non-strict count itself would overflow u64.
 *
 * Example: pre_le_count_ovf
 *
 *    u64 r6 = 0, r9 = 0;
 *    while (++r9 <= 8 && r6 <= U64_MAX) {  // r6 ∈ [0..8]
 *        r6 += 1;                         // r6 ∈ [1..8]
 *    }
 */
PRE__COND_TEST2(pre_le_count_ovf,  ">",                  0,            1,     ITER_U64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_ge_count_ovf,  "<",       ITER_U64_MAX,           -1,                0, "can't compute iterations count")
PRE__COND_TEST2(pre_sle_count_ovf, "s>",      ITER_S64_MIN,            1,     ITER_S64_MAX, "can't compute iterations count")
PRE__COND_TEST2(pre_sge_count_ovf, "s<",      ITER_S64_MAX,           -1,     ITER_S64_MIN, "can't compute iterations count")

/*
 * JNE divisibility and equality across unsigned wraparound.
 *
 * Example: pre_ne_rem
 *
 *    u64 r6 = 1, r9 = 0;
 *    while (++r9 <= 8 && r6 != 8) { // r6 ∈ [1,3,...,17]
 *        r6 += 2;                   // r6 ∈ [3,5,...,17]
 *    }
 */
PRE__COND_TEST2(pre_ne_rem,        "==",                 1,            2,                8, "can't compute iterations count")
PRE__COND_TEST2(pre_ne_rem_down,   "==",                 9,           -2,                2, "can't compute iterations count")
PRE__COND_TEST (pre_ne_wrap_up,    "==",  ITER_U64_MAX - 1,            2,                0, "header_count is 2 ")
PRE__COND_TEST (pre_ne_wrap_down,  "==",                 1,           -2,     ITER_U64_MAX, "header_count is 2 ")

/*
 * Largest supported header count, sentinel, and u32 overflow.
 *
 * Example: pre_lt_max_hdrs
 *
 *    u64 r6 = 0, r9 = 0;
 *    while (++r9 <= 8 && r6 < U32_MAX - 2) { // r6 ∈ [0..8]
 *        r6 += 1;                            // r6 ∈ [1..8]
 *    }
 */
PRE__COND_TEST2(pre_lt_max_hdrs,   ">=",                 0,            1, ITER_U32_MAX - 2, "header_count is [0..4294967294] ")
PRE__COND_TEST2(pre_lt_sentinel,   ">=",                 0,            1, ITER_U32_MAX - 1, "can't compute iterations count")
PRE__COND_TEST2(pre_lt_hdr_ovf,    ">=",                 0,            1,     ITER_U32_MAX, "can't compute iterations count")

/*
 * The extreme signed step values remain valid unsigned magnitudes.
 *
 * Example: pre_lt_step_max
 *
 *    u64 r6 = 0;
 *    while (r6 < S64_MAX) {         // r6 ∈ [0,S64_MAX]
 *        r6 += S64_MAX;             // r6 ∈ [S64_MAX]
 *    }
 */
PRE__COND_TEST (pre_lt_step_max,   ">=",                 0, ITER_S64_MAX,     ITER_S64_MAX, "header_count is 2 ")
PRE__COND_TEST (pre_gt_step_min,   "<=",      ITER_S64_MIN, ITER_S64_MIN,                0, "header_count is 2 ")
PRE__COND_TEST (pre_ne_step_min,   "==",                 0, ITER_S64_MIN,     ITER_S64_MIN, "header_count is 2 ")

/*
 * A zero step cannot establish a finite iteration count.
 *
 * Example: pre_lt_zero_step
 *
 *    u64 r6 = 1, r9 = 0;
 *    while (++r9 <= 8 && r6 < 7) {  // r6 ∈ [1], nine header visits
 *        r6 += 0;                   // r6 ∈ [1]
 *    }
 */
PRE__COND_TEST2(pre_lt_zero_step,  ">=",                 1,            0,                7, "can't compute iterations count")

/*
 * Signed JMP32 comparisons sign-extend the initial value, bound and step.
 * Encode negative operands as positive u32 constants so the conversion matters.
 * post32_sgt also checks that an exit exactly at S32_MIN is accepted.
 */
/*              name               op                 initial  step             bound  expected message */
POST_COND_TEST32(post32_slt,       "s<",                  -7u,    2,              -1u, "header_count is [0..3] ")
POST_COND_TEST32(post32_sle,       "s<=",                 -7u,    2,              -1u, "header_count is [0..4] ")
POST_COND_TEST32(post32_sgt,       "s>", (__u32)INT32_MIN + 6,  -2u, (__u32)INT32_MIN, "header_count is [0..3] ")
POST_COND_TEST32(post32_sge,       "s>=",                 -1u,  -2u,              -7u, "header_count is [0..4] ")

/*
 * When computing 32-bit iteration bounds, scev.c:compute_max_iters() does
 * the computation in the 64-bit domain first. For unsigned JMP32 comparisons,
 * it zero-extends the initial value and bound, but sign-extends the step.
 * Hence pick negative 64-bit initial values and bounds, and encode negative
 * steps as positive u32 constants, so both conversions affect the computation.
 * post32_lt also checks that an exit exactly at U32_MAX is accepted.
 */
POST_COND_TEST32(post32_lt,        "<",                    -7,    2,               -1, "header_count is [0..3] ")
POST_COND_TEST32(post32_le,        "<=",                   -9,    2,               -3, "header_count is [0..4] ")
POST_COND_TEST32(post32_gt,        ">",                    -1,  -2u,               -7, "header_count is [0..3] ")
POST_COND_TEST32(post32_ge,        ">=",                   -1,  -2u,               -7, "header_count is [0..4] ")
POST_COND_TEST32(post32_ne,        "!=",                   -1,  -2u,               -7, "header_count is [0..3] ")

/* An exit exactly at S32_MAX is accepted as well. */
POST_COND_TEST32(post32_slt_max,   "s<",        INT32_MAX - 6,    2,        INT32_MAX, "header_count is [0..3] ")

/*
 * The first iteration fits in 32 bits, but the predicted exit overshoots the bound
 * by one and wraps.
 */
POST_COND_TEST32(post32_slt_wrap,  "s<",        INT32_MAX - 3,    2,        INT32_MAX, "can't compute iterations count")
POST_COND_TEST32(post32_sgt_wrap,  "s>", (__u32)INT32_MIN + 3,  -2u, (__u32)INT32_MIN, "can't compute iterations count")
POST_COND_TEST32(post32_lt_wrap,   "<",                    -4,    2,               -1, "can't compute iterations count")

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 10 ")
__msg("loop header at 1, widening r0 to 0..9 step 1")
__msg("processed 5 insns")
__naked void post_cond_jlt(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 < 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Explore with r7=10 first, r7=4096 next.
 * Should trigger invalid stack read if r7 is marked precise.
 *
 *   r0 = 0;
 *   r6 = 0;
 *   r7 = bpf_get_prandom_u32() == 42 ? 10 : 4096;
 *   do {
 *           r0++;
 *           r6++;
 *           r3 = *(fp - r0);
 *   } while (r6 < r7);
 *   return r0;
 */
SEC("xdp")
__failure
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 6, header_count is 10 ")
__msg("mark_precise: frame0: parent state regs=r6,r7 stack=:")
__msg("loop header at 6, widening r0 to 0..9 step 1")
__msg("loop header at 6, header_count is 4096 ")
__msg("loop header at 6, widening r0 to 0..4095 step 1")
__msg("invalid variable-offset read from stack R2")
__naked void mark_bound_precise(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r7 = 4096;					\
	if r0 != 42 goto 1f;				\
	r7 = 10;					\
1:	r0 = 0;						\
	r6 = 0;						\
2:	r0 += 1;					\
	r6 += 1;					\
	r2 = r0;					\
	r2 = -r2;					\
	r2 += r10;					\
	r3 = *(u8 *)(r2 + 0);				\
	if r6 < r7 goto 2b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Explore with r6=4086 first, r6=0 next.
 * Should trigger invalid stack read if r6 is marked precise.
 *
 *   r0 = 0;
 *   r6 = bpf_get_prandom_u32() == 42 ? 4086 : 0;
 *   do {
 *           r0++;
 *           r6++;
 *           r3 = *(fp - r0);
 *   } while (r6 < 4096);
 *   return r0;
 */
SEC("xdp")
__failure
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 5, header_count is 10 ")
__msg("mark_precise: frame0: parent state regs=r6 stack=:")
__msg("loop header at 5, widening r0 to 0..9 step 1")
__msg("loop header at 5, header_count is 4096 ")
__msg("loop header at 5, widening r0 to 0..4095 step 1")
__msg("invalid variable-offset read from stack R2")
__naked void mark_base_precise(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = 0;						\
	if r0 != 42 goto 1f;				\
	r6 = 4086;					\
1:	r0 = 0;						\
2:	r0 += 1;					\
	r6 += 1;					\
	r2 = r0;					\
	r2 = -r2;					\
	r2 += r10;					\
	r3 = *(u8 *)(r2 + 0);				\
	if r6 < 4096 goto 2b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Spill size should be taken into account when evaluating bound scev.
 *
 *   r0 = 256;
 *   *(u8 *)(r10 - 8) = r0;
 *   r0 = 0;
 *   do {
 *           r0++;
 *           r1 = *(u64 *)(r10 - 8);
 *           if (r0 & 0x200) break; // hidden from scev, limit main pass iterations.
 *   } while (r0 < r1);
 *   return r0;
 */
SEC("xdp")
__success
__log_level(2)
__msg("scev at header 3:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg("loop header at 3, can't compute iterations count")
__naked void eval_expr_small_spill_bound(void)
{
	asm volatile ("					\
	r0 = 256;					\
	*(u8 *)(r10 - 8) = r0;				\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = *(u64 *)(r10 - 8);				\
	if r0 & 0x200 goto 2f;				\
	if r0 < r1 goto 1b;				\
2:	exit;						\
"	::: __clobber_all);
}

/* Even when the saved value fits, the upper bytes of the slot are unknown. */
SEC("xdp")
__success
__log_level(2)
__msg("scev at header 3:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg("loop header at 3, can't compute iterations count")
__naked void eval_expr_small_spill_bound_fits(void)
{
	asm volatile ("					\
	r0 = 10;					\
	*(u8 *)(r10 - 8) = r0;				\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = *(u64 *)(r10 - 8);				\
	if r0 & 0x80 goto 2f;				\
	if r0 < r1 goto 1b;				\
2:	exit;						\
"	::: __clobber_all);
}

/*
 *   r0 = 255;
 *   *(u8 *)(r10 - 8) = r0;		// narrow spill
 *   do {
 *           r0 = *(u64 *)(r10 - 8);	// wide load, widening should bail out here.
 *           r0++;
 *           *(u64 *)(r10 - 8) = r0;
 *   } while (r0 < 265);
 *   return r0;
 */
SEC("xdp")
__success
__log_level(2)
__msg("scev at header 2:")
__msg_next("  *fp-8=(+ *fp-8 1) / (linear *fp-8 1)")
__msg("loop header at 2, can't compute iterations count")
__not_msg("widening")
__naked void no_widen_small_spill_counter(void)
{
	asm volatile ("					\
	r0 = 255;					\
	*(u8 *)(r10 - 8) = r0;				\
1:	r0 = *(u64 *)(r10 - 8);				\
	r0 += 1;					\
	*(u64 *)(r10 - 8) = r0;				\
	if r0 < 265 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* Full-width counterpart: the counter can be widened from 5 to 14. */
SEC("xdp")
__success
__log_level(2)
__msg("scev at header 2:")
__msg_next("  *fp-8=(+ *fp-8 1) / (linear *fp-8 1)")
__msg("loop header at 2, widening *fp-8 to 5..14 step 1")
__naked void widen_full_spill_counter(void)
{
	asm volatile ("					\
	r0 = 5;						\
	*(u64 *)(r10 - 8) = r0;				\
1:	r0 = *(u64 *)(r10 - 8);				\
	r0 += 1;					\
	*(u64 *)(r10 - 8) = r0;				\
	if r0 < 15 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("scev at header 2:")
__msg_next("  *fp-8=(+ *fp-8 1) / (linear *fp-8 1)")
__msg("loop header at 2, widening *fp-8 to 5..14 step 1")
__naked void widen_acquire_release_counter(void)
{
	asm volatile (
	"r0 = 5;"
	"*(u64 *)(r10 - 8) = r0;"
"1:"
	".8byte %[load_acquire];"
	"r0 += 1;"
	".8byte %[store_release];"
	"if r0 < 15 goto 1b;"
	"exit;"
	:
	: __imm_insn(load_acquire, BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_10, -8)),
	  __imm_insn(store_release, BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_0, -8))
	: __clobber_all);
}

/*
 * r6 scev is (any r6 *fp-8). The byte store leaves memory at zero,
 * but the spill metadata retains 256. Widen r6 to an unknown scalar
 * instead of [1,256], which would exclude the zero loaded from memory.
 *
 *   r0 = 0;
 *   *(u64 *)(r10 - 8) = r0;
 *   r0 = 256;
 *   *(u8 *)(r10 - 8) = r0;		// narrow spill
 *   r6 = 1;
 *   r7 = 0;
 *   do {
 *           r0 = r6;
 *           r6 = *(u64 *)(r10 - 8);
 *           r7++;
 *   } while (r7 < 2);
 *   return r0;
 */
SEC("xdp")
__success __retval(0)
__log_level(2)
__msg("scev at header 6:")
__msg_next("  r6=*fp-8 / (any r6 *fp-8)")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg("loop header at 6, widening r6 to 0x8000000000000000..0x7fffffffffffffff step 1")
__naked void widen_any_small_spill_leaf(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64 *)(r10 - 8) = r0;				\
	r0 = 256;					\
	*(u8 *)(r10 - 8) = r0;				\
	r6 = 1;						\
	r7 = 0;						\
1:	r0 = r6;					\
	r6 = *(u64 *)(r10 - 8);				\
	r7 += 1;					\
	if r7 < 2 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Widening (any *fp-8 7) must allocate the slot before writing its state.
 * The unallocated entry value makes the union an unknown scalar.
 *
 *   r6 = 0;
 *   do {
 *           if (r6 == 1)
 *                   *(u64 *)(r10 - 8) = 7;
 *           r0 = *(u64 *)(r10 - 8);
 *           r6++;
 *   } while (r6 < 3);
 *   return r0;
 */
SEC("xdp")
__success __retval(7)
__log_level(2)
__msg("scev at header 1:")
__msg_next("r6=(+ r6 1) / (linear r6 1)")
__msg_next("*fp-8=(any *fp-8 7) / (any *fp-8 7)")
__msg("loop header at 1, widening r6 to 0..2 step 1")
__msg("2: (7a) *(u64 *)(r10 -8) = 7          ; R10=fp0 fp-8=7 loop_stack=1")
__naked void widen_any_unallocated_slot(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:	if r6 != 1 goto 2f;				\
	*(u64 *)(r10 - 8) = 7;				\
2:	r0 = *(u64 *)(r10 - 8);				\
	r6 += 1;					\
	if r6 < 3 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* A spilled zero entry contributes zero to the union. */
SEC("xdp")
__success __retval(7)
__log_level(2)
__msg("loop header at 2, widening *fp-8 to 0..7 step 1")
__naked void widen_any_zero_spill(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r6 = 0;						\
1:	if r6 != 1 goto 2f;				\
	*(u64 *)(r10 - 8) = 7;				\
2:	r0 = *(u64 *)(r10 - 8);				\
	r6 += 1;					\
	if r6 < 3 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* A byte store inside the slot leaves STACK_MISC and an unknown scalar value. */
SEC("xdp")
__success __retval(7)
__log_level(2)
__msg("scev at header 2:")
__msg_next("r6=(+ r6 1) / (linear r6 1)")
__msg_next("*fp-8=(any *fp-8 7) / (any *fp-8 7)")
__msg("loop header at 2, widening r6 to 0..2 step 1")
__msg("3: (79) r1 = *(u64 *)(r10 -8)         ; R1=scalar() R10=fp0 fp-8=m???????")
__naked void widen_any_misc_slot(void)
{
	asm volatile ("					\
	*(u8 *)(r10 - 1) = 1;				\
	r6 = 0;						\
1:	if r6 != 1 goto 2f;				\
	r1 = *(u64 *)(r10 - 8);				\
	*(u64 *)(r10 - 8) = 7;				\
2:	r0 = *(u64 *)(r10 - 8);				\
	r6 += 1;					\
	if r6 < 3 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

__naked __noinline __used static int probe_read_stack(void)
{
	asm volatile (
	"r2 = 8;"
	"r3 = 0;"
	"call %[bpf_probe_read_kernel];"
	"exit;"
	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

/* The unknown SCEV of an unbound STACK_MISC slot must not prevent widening. */
SEC("tracepoint")
__success
__log_level(2)
__msg("scev at header 4:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next("  *fp-8=?")
__msg("loop header at 4, widening r6 to 0..9 step 1")
__naked void widen_subprog_probe_read(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call probe_read_stack;"
	"r6 = 0;"
"1:"
	"r0 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"call probe_read_stack;"
	"r6 += 1;"
	"if r6 < 10 goto 1b;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* An induction variable whose range overflows s64 is widened to the full range. */
SEC("socket")
__success
__log_level(2)
__msg("loop header at 4, header_count is 10")
__msg("loop header at 4, widening r1 to 0..9 step 1")
__msg("loop header at 4, widening r6 to 0x8000000000000000..0x7fffffffffffffff step 1")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void widen_overflowing_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 |= 1;	/* not an unknown scalar, those need no widening */	\
	r1 = 0;						\
1:	r6 += 24;					\
	r1 += 1;					\
	if r1 < 10 goto 1b;				\
	r0 = r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * The bound slot is unallocated at the first loop header. SCEV must
 * treat it as an unknown scalar. JSET limits main-pass iterations.
 *
 *   r0 = 0;
 *   do {
 *           r0++;
 *           r1 = *(u64 *)(r10 - 8);
 *           if (r0 & 0x80) break;
 *   } while (r0 < r1);
 *   return 0;
 */
SEC("xdp")
__success __retval(0)
__log_level(2)
__msg("scev at header 1:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg("loop header at 1, can't compute iterations count")
__not_msg("widening")
__naked void eval_expr_unallocated_stack_bound(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	r1 = *(u64 *)(r10 - 8);				\
	if r0 & 0x80 goto 2f;				\
	if r0 < r1 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 3, header_count is 8")
__msg("loop header at 3, widening r0 to 2..9 step 1")
__msg("3: R0=scalar(smin=umin=smin32=umin32=2,smax=umax=smax32=umax32=9,{{.*}})")
__msg("processed 6 insns")
__naked void post_cond_jlt_with_base(void)
{
	asm volatile ("					\
	r7 = 10 ll;	/* ldimm64 for a twist */	\
	r0 = 2;						\
1:	r0 += 1;					\
	if r0 < r7 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 4 ")
__naked void latch_base_differs_from_step(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r1 = r0;					\
	r1 += 1;					\
	if r1 >= 10 goto 2f;				\
	r0 += 4;					\
	goto 1b;					\
2:	r0 = r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 11")
__msg("loop header at 1, widening r0 to 0..10 step 1")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=10,{{.*}})")
__msg("processed 5 insns")
__naked void post_cond_jle(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 <= 10 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 10")
__msg("loop header at 1, widening r0 to 0..9 step 1")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=9,{{.*}})")
__msg("processed 6 insns")
__naked void post_cond_jge(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 >= 10 goto 2f;				\
	goto 1b;					\
2:	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 11")
__msg("loop header at 1, widening r0 to 0..10 step 1")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=10,{{.*}})")
__msg("processed 6 insns")
__naked void pre_cond_jge(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	if r0 >= 10 goto 2f;				\
	r0 += 1;					\
	goto 1b;					\
2:	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg_next("scev at header 1:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg_next(" scev at latch 2:")
__msg_next("  r0=(+ r0 1) / (linear (+ r0 1) 1)")
__msg("loop header at 1, widening r0")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,{{.*}})")
__msg("1: (07) r0 += 1                       ; R0=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=3,{{.*}})")
__msg("2: (55) if r0 != 0x3 goto pc-2")
__msg("3: (95) exit")
__msg("loop header at 1, clamping r0")
__msg("from 2 to 1: safe")
__msg("processed 5 insns")
__naked void post_cond_jne(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	r0 += 1;					\
	if r0 != 3 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg_next("scev at header 1:")
__msg_next("  r0=(+ r0 -1) / (linear r0 -1)")
__msg_next(" scev at latch 2:")
__msg_next("  r0=(+ r0 -1) / (linear (+ r0 -1) -1)")
__msg("loop header at 1, header_count is 3 ")
__msg("loop header at 1, widening r0 to 1..3 step 1")
__msg("1: R0=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=3,var_off=(0x0; 0x3)) loop_stack=1")
__msg("loop header at 1, clamping r0 to 1..3 step 1")
__msg("processed 5 insns")
__naked void post_cond_jne_neg_step(void)
{
	asm volatile ("					\
	r0 = 3;						\
1:	r0 += -1;					\
	if r0 != 0 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * r1 and r3 are widened independently, so the r3 == 5 path reaches the back
 * edge with r3 past the last value it takes at the header.
 */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, header_count is 5 ")
__msg("loop header at 2, widening r1 to 0..4 step 1")
__msg("loop header at 2, widening r3 to 0..4 step 1")
__msg("3: (55) if r3 != 0x5 goto pc+1 {{.*}}; R3=5")
__msg("loop header at 2, clamping r3 to 0..4 step 1")
__msg("from 6 to 2: safe")
__naked void clamp_dependent_overshoot(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r3 = 0;						\
1:	r3 += 1;					\
	if r3 != 5 goto 2f;				\
	r2 = 0;						\
2:	r1 += 1;					\
	if r1 != 5 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Same with a 32-bit dependent register, SCEV (zext32 (linear r3 1)) */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, header_count is 5 ")
__msg("loop header at 2, widening r1 to 0..4 step 1")
__msg("loop header at 2, widening r3 to 0..4 step 1")
__msg("3: (56) if w3 != 0x5 goto pc+1 {{.*}}; R3=5")
__msg("loop header at 2, clamping r3 to 0..4 step 1")
__msg("from 6 to 2: safe")
__naked void clamp_dependent_overshoot32(void)
{
	asm volatile ("					\
	r1 = 0;						\
	w3 = 0;						\
1:	w3 += 1;					\
	if w3 != 5 goto 2f;				\
	r2 = 0;						\
2:	r1 += 1;					\
	if r1 != 5 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg_next("scev at header 1:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg_next(" scev at latch 1:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg("loop header at 1, widening r0")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=3,{{.*}})")
__msg("1: (15) if r0 == 0x3 goto pc+2        ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,{{.*}})")
__msg("2: (07) r0 += 1                       ; R0=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=3,{{.*}})")
__msg("3: (05) goto pc-3")
__msg("loop header at 1, clamping r0")
__msg("1: safe")
__msg("from 1 to 4: R0=3")
__msg("4: R0=3")
__msg("4: (95) exit")
__msg("processed 6 insns")
__naked void pre_cond_je1(void)
{
	asm volatile ("					\
	r0 = 0;						\
1:	if r0 == 3 goto 2f;				\
	r0 += 1;					\
	goto 1b;					\
2:	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg_next("scev at header 1:")
__msg_next("  r0=(zext32 (+ r0 1)) / (zext32 (linear r0 1))")
__msg_next(" scev at latch 2:")
__msg_next("  r0=(zext32 (+ r0 1)) / (zext32 (linear (+ r0 1) 1))")
__msg("loop header at 1, widening r0")
__msg("1: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,{{.*}})")
__msg("1: (04) w0 += 1                       ; R0=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=3,{{.*}})")
__msg("2: (56) if w0 != 0x3 goto pc-2")
__msg("3: (95) exit")
__msg("loop header at 1, clamping r0")
__msg("from 2 to 1: safe")
__msg("processed 5 insns")
__naked void post_cond_jne32(void)
{
	asm volatile ("					\
	w0 = 0;						\
1:	w0 += 1;					\
	if w0 != 3 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* w0 ∈ {0xffffffff, 2, 5}, w0 % 3 ∈ {0, 2, 2} ⇒ step 3 can't be preserved */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev at header 1:")
__msg_next("  r0=(zext32 (+ r0 3)) / (zext32 (linear r0 3))")
__msg_next(" scev at latch 2:")
__msg_next("  r0=(zext32 (+ r0 3)) / (zext32 (linear (+ r0 3) 3))")
__msg("entering loop 1")
__msg_next("loop header at 1, header_count is 2")
__msg("loop header at 1, widening r0 to 0..0xffffffff (s32 -1..2) step 1")
__msg("1: R0=scalar(smin=0,smax=umax=0xffffffff,smin32=-1,smax32=2,{{.*}})")
__naked void post_cond_jne32_overflow(void)
{
	asm volatile ("					\
	w0 = 0xffffffff;				\
1:	w0 += 3;					\
	if w0 != 5 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * w0 ∈ {0, 3, ..., 0xffffffff, 2, 5}, w0 % 3 ∈ {0, 0, ..., 0, 2, 2}
 * ⇒ step 3 can't be preserved.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, header_count is 1431655767")
__msg("loop header at 2, widening r0 to 0..0xffffffff step 1")
__naked void post_cond_jne32_full_range(void)
{
	asm volatile ("					\
	w0 = 0;						\
	w1 = 0;						\
1:	w0 += 3;					\
	w1 += 1;					\
	if w1 != 1431655767 goto 1b;			\
	exit;						\
"	::: __clobber_all);
}

/* r0 ∈ {-6, 0xfffffff7, 0xfffffff4}, r0 % 3 ∈ {0, 1, 1} ⇒ step 3 can't be preserved. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 2")
__msg("loop header at 1, widening r0 to -6..0xfffffffa (s32 -9..-6) step 1")
__naked void post_cond_jne32_base_mismatch(void)
{
	asm volatile ("					\
	r0 = -6;					\
1:	w0 += -3;					\
	if w0 != -12 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * r0 ∈ {0, 0xffffffff, 0xfffffffe},
 * ⇒ the 64-bit header bounds are [0, U32_MAX], 32-bit header bounds are [-1, 0].
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 2")
__msg("loop header at 1, widening r0 to 0..0xffffffff (s32 -1..0) step 1")
__naked void post_cond_jne32_neg_step(void)
{
	asm volatile ("					\
	w0 = 0;						\
1:	w0 += -1;					\
	if w0 != -2 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/*
 * r0 ∈ {S64_MIN + 1, 3, 5}, r0 % 2 ∈ {1, 1, 1}
 * ⇒ step 2 and base 1 can be preserved.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, header_count is 2")
__msg("loop header at 2, widening r0 to 0x8000000000000000..0x7fffffffffffffff (s32 1..3) step 2")
__msg("2: R0=scalar({{.*}}step=1+2)")
__naked void post_cond_jne32_signed_boundary(void)
{
	asm volatile ("					\
	r0 = %[initial] ll;				\
1:	w0 += 2;					\
	if w0 != 5 goto 1b;				\
	exit;						\
"	:
	: __imm_const(initial, ITER_S64_MIN + 1)
	: __clobber_all);
}

/* The pre-condition also visits the header with the terminating value 3. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 4")
__msg("loop header at 1, widening r0 to 0..3 step 1")
__msg("loop header at 1, clamping r0")
__msg("4: R0=3")
__naked void pre_cond_je32(void)
{
	asm volatile ("					\
	w0 = 0;						\
1:	if w0 == 3 goto 2f;				\
	w0 += 1;					\
	goto 1b;					\
2:	exit;						\
"	::: __clobber_all);
}

/* ALU32 keeps r0 in [0, U32_MAX], so the 64-bit comparison never becomes false. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 4, can't compute iterations count")
__naked void post_cond_alu32_jmp64(void)
{
	asm volatile ("					\
	w0 = 0;						\
	r1 = 0x100000003 ll;				\
	r9 = 0;						\
1:	"
	LIMIT_ITERATIONS
	"w0 += 1;					\
	if r0 != r1 goto 1b;				\
2:	exit;						\
"	::: __clobber_all);
}

/* The signed 32-bit comparison is always true, even when r0 exceeds INT32_MAX. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, can't compute iterations count")
__naked void post_cond_alu64_jmp32(void)
{
	asm volatile ("					\
	r0 = %[initial];				\
	r9 = 0;						\
1:	"
	LIMIT_ITERATIONS
	"r0 += 1;					\
	if w0 s<= %[bound] goto 1b;			\
2:	exit;						\
"	:
	: __imm_const(initial, INT32_MAX - 1),
	  __imm_const(bound, INT32_MAX)
	: __clobber_all);
}

/* ALU32 values above INT32_MAX remain positive in a signed 64-bit comparison. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 3, header_count is 3")
__msg("loop header at 3, widening r0 to 0xfffffffc..0xfffffffe (s32 -4..-2) step 1")
__naked void post_cond_alu32_jmp64_bounded(void)
{
	asm volatile ("					\
	w0 = -4;					\
	r1 = 0xffffffff ll;				\
1:	w0 += 1;					\
	if r0 s< r1 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

/* The negative 64-bit counter and its exit value fit in the signed 32-bit range. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is 3")
__msg("loop header at 1, widening r0 to -6..-2 step 2")
__naked void post_cond_alu64_jmp32_bounded(void)
{
	asm volatile ("					\
	r0 = -6;					\
1:	r0 += 2;					\
	if w0 s< 0 goto 1b;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 1, header_count is [0..3] ")
__naked void one_backedge_two_exits(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:	call %[bpf_get_prandom_u32];			\
	if r0 == 0 goto 2f;				\
	r6 += 1;					\
	if r6 != 3 goto 1b;				\
2:	r0 = r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg_next("scev at header 11:")
__msg_next("  r0=(+ r0 1) / (linear r0 1)")
__msg_next("  r1=(+ r1 2) / (linear r1 2)")
__msg_next(" scev at latch 16:")
__msg_next("  r0=(+ r0 1) / (linear (+ r0 1) 1)")
__msg_next("  r1=(+ r1 2) / (linear (+ r1 2) 2)")
__msg("loop header at 11, widening r0")
__msg("loop header at 11, widening r1")
__msg("11: R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=7,var_off=(0x0; 0x7)) R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=14,var_off=(0x0; 0xe),step=0+2)")
/* loop exit */
__msg("16: (a5) if r0 < 0x8 goto pc-6")
__msg("exiting loop 11")
__msg("17: (95) exit")
/* second iteration */
__msg("loop header at 11, clamping r0")
__msg("loop header at 11, clamping r1")
/* iteration convergence */
__msg("from 16 to 11: safe")
__not_msg("{{^}}11:")
/* map lookup error path */
__msg("from 7 to 17: safe")
__naked void correlated_regs(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r0 = 0;						\
	r1 = 0;						\
1:	r2 = r6;					\
	r2 += r1;					\
	*(u8 *)(r2 + 0) = 1;				\
	r0 += 1;					\
	r1 += 2;					\
	if r0 < 8 goto 1b;				\
2:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

/*
 * k = 0
 * for (i = 0; i < 4; i++):
 *   for (j = 0; j < 4; j++):
 *     k += 1
 *     k <<= 1   // make SCEV construction not possible
 *     k >>= 1
 * map[k] = 1    // make k precise
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("19: (72) *(u8 *)(r4 +0) = 1           ; R4=map_value(id={{.*}},map=map,ks=4,vs=1024,imm=16)")
__not_msg("19: ")
__msg("processed 106 insns")
__naked void nested_loops_precise_var1(void)
{
	asm volatile ("					\
	*(u64*)(r10 - 8) = 0;				\
	r1 = %[map] ll;					\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 3f;				\
	r1 = 0;						\
	r3 = 0;						\
	/* outer loop */				\
1:	r2 = 0;						\
	/* inner loop */				\
2:	r2 += 1;					\
	r3 += 1;					\
	r3 <<= 1;					\
	r3 >>= 1;					\
	if r2 < 4 goto 2b;				\
	r1 += 1;					\
	if r1 < 4 goto 1b;				\
	r4 = r0;					\
	r4 += r3;					\
	*(u8 *)(r4 + 0) = 1;				\
	r0 = 0;						\
3:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("loop header at 2, header_count is 100 ")
__msg("loop header at 4, header_count is [0..100] ")
__msg("loop header at 6, header_count is [0..100] ")
__msg("processed 20 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void nested_loop_with_two_exits(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = 0;						\
1:	r6 += 1;					\
	r7 = 0;						\
2:	r7 += 1;					\
	r8 = 0;						\
3:	r8 += 1;					\
	call %[bpf_get_prandom_u32];			\
	if r0 == 42 goto +1;				\
	goto 4f;					\
	if r8 < 100 goto 3b;				\
	if r7 < 100 goto 2b;				\
4:	if r6 < 100 goto 1b;				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__naked void exit_loop_into_loop_header(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 0;						\
loop_a_%=:						\
	r1 += 1;					\
	if r1 < 10 goto loop_a_%=;			\
loop_b_%=:						\
	r2 += 1;					\
	if r2 < 10 goto loop_b_%=;			\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * This exercises verifier.c:loop_stack_{pop,push}() implementation,
 * at 'goto d' loops 'b' and 'a' have to be popped from stack,
 * while loops 'c' and 'd' have to be pushed to stack.
 *
 *   loop a:                  // header 5
 *     loop b:                // header 6
 *       if (rand) goto d;    // 8 -> 12, side entry into inner loop d
 *       ...
 *   loop c:                  // header 11
 *     loop d:                // header 12
 *       ...
 */
SEC("xdp")
__log_level(2)
__msg("loop at 5")
__msg("loop at 6, nested in 5")
__msg("loop at 11, irreducible")
__msg("loop at 12, nested in 11")
/* entry via if r0 == 5 goto d_%= false branch */
__msg("loop header at 12, header_count is 3 ")
__msg("loop header at 12, widening r9 to 0..2 step 1")
__msg("12: R8=1 R9=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3)) loop_stack=11,12")
/* entry via if r0 == 5 goto d_%= true branch */
__msg("loop header at 12, header_count is 3 ")
__msg("loop header at 12, widening r9 to 0..2 step 1")
__msg("from 8 to 12: R8=0 R9=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3)) R10=fp0 loop_stack=11,12")
__naked void enter_nested_loop_from_side(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = 0;						\
	r7 = 0;						\
	r8 = 0;						\
	r9 = 0;						\
a_%=:	r6 += 1;					\
b_%=:	r7 += 1;					\
	call %[bpf_get_prandom_u32];			\
	if r0 == 5 goto d_%=;				\
	if r7 < 3 goto b_%=;				\
	if r6 < 3 goto a_%=;				\
c_%=:	r8 += 1;					\
d_%=:	r9 += 1;					\
	if r9 < 3 goto d_%=;				\
	if r8 < 3 goto c_%=;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Loop A (counter in r6) contains irreducible loop B (header 5),
 * which contains C (counter in r9).
 * Entering B at 'body' saves and restores r6, making it appear invariant.
 * Entering B at 'alternate' skips the save and modifies r6 instead.
 * Hence A must not infer a SCEV expression for r6.
 * SCEV expression for r9 in C should still be computed.
 *
 *  0: r6 = 0;
 *     do {                              // A
 *  1:     r6++;
 *  2:     r0 = bpf_get_prandom_u32();
 *  3:     r7 = 0;
 *  4:     if (r0 > 5) goto alternate;
 *  5: B:  r8 = r6;                      // B
 *  6:     goto body;
 *  7: alternate:
 *         r8 = r6;
 *  8:     r8++;
 *  9: body:
 *         r6 = r8;
 * 10:     r9 = 0;
 *         do {                          // C
 * 11:         r9++;
 * 12:     } while (r9 < 3);
 * 13:     r7++;
 * 14:     if (r7 < 4) goto B;
 * 15: } while (r6 < 4);
 * 16: r0 = 0;
 * 17: return r0;
 */
SEC("xdp")
__success
__log_level(2)
__msg("loop at 1{{$}}")
__msg("loop at 5, nested in 1, irreducible")
__msg("loop at 11, nested in 5")
__msg("scev at header 1:")
__msg_next("  r6=?")
__msg("scev at header 11:")
__msg_next("  r9=(+ r9 1) / (linear r9 1)")
__msg("loop header at 11, widening r9 to 0..2 step 1")
__not_msg("loop header at 1, widening r6")
__naked void nested_irreducible_loop(void)
{
	asm volatile ("					\
	r6 = 0;						\
a_%=:	r6 += 1;					\
	call %[bpf_get_prandom_u32];			\
	r7 = 0;						\
	if r0 > 5 goto alternate_%=;			\
b_%=:	r8 = r6;					\
	goto body_%=;					\
alternate_%=:						\
	r8 = r6;					\
	r8 += 1;					\
body_%=:						\
	r6 = r8;					\
	r9 = 0;						\
c_%=:	r9 += 1;					\
	if r9 < 3 goto c_%=;				\
	r7 += 1;					\
	if r7 < 4 goto b_%=;				\
	if r6 < 4 goto a_%=;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Loop A (counter in r6) contains irreducible loop B.
 * Loop B has an exit edge from A. This is a corner case
 * observed during testing: insn_aux_data[l1].need_scev == true,
 * but SCEV environment is not constructed.
 *
 *	  r6 = 0;
 *	  do {					// A
 *	     r6++;
 *	     r0 = bpf_get_prandom_u32();
 *	     r7 = 0;
 *	     if (r0 > 5) goto alternate;
 *	     do {				// B
 *	       r7++;
 * alt:	       r0 = bpf_get_prandom_u32();
 * l1:	       if (r0 == 42)
 *		 goto exit;
 *	       r7++;
 *	     } while (r7 < 4);
 *	  } while (r6 < 4);
 *	  r0 = 0;
 * exit:  return r0;
 */
SEC("xdp")
__success
__log_level(2)
__naked void nested_irreducible_loop2(void)
{
	asm volatile ("					\
	r6 = 0;						\
a_%=:	r6 += 1;					\
	call %[bpf_get_prandom_u32];			\
	r7 = 0;						\
	if r0 > 5 goto alternate_%=;			\
b_%=:	r7 += 1;					\
alternate_%=:						\
	call %[bpf_get_prandom_u32];			\
	if r0 == 42 goto exit_%=;			\
	r7 += 1;					\
	if r7 < 4 goto b_%=;				\
	if r6 < 4 goto a_%=;				\
	r0 = 0;						\
exit_%=:exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Induction variable seeded from a non-constant value. r7 enters the loop as a
 * range aligned to 2 (prandom & 0x6 -> {0,2,4,6}) and is incremented by a
 * non-power-of-2 slope of 6. Since the entry value is not a single point, only
 * the power-of-two alignment shared by the entry value and the slope can be
 * guaranteed, so the widened step is 2.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 4, widening r7 to 0..18 step 2")
__msg("R7=scalar(smin=smin32=0,smax=umax=smax32=umax32=18,var_off=(0x0; 0x1e),step=0+2)")
__naked void widen_nonconst_base(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r7 = r0;					\
	r7 &= 0x6;					\
	r6 = 0;						\
1:	r7 += 6;					\
	r6 += 1;					\
	if r6 < 3 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * After excluding zero from {0,4,8,12}, the interval starts at 1 while the
 * values remain multiples of 4. Widening over three iterations must preserve
 * base 0 and include 20.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 5, header_count is 3 ")
__msg("5: R7=scalar({{.*}}smax=umax=smax32=umax32=20,{{.*}}step=0+4)")
__naked void widen_nonconst_base_refined(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r7 = r0;					\
	r7 &= 0xc;					\
	if r7 < 1 goto 2f;				\
	r8 = 0;						\
1:	r7 += 4;					\
	r8 += 1;					\
	if r8 < 3 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Two nested loops with (1) exiting directly to (2):
 *
 *   for (r6 = 0; r6 < 4; r6++) {
 *     r7 = 0;
 *     for (; r7 <  3; r7++) {}   // (1)
 *     for (; r7 != 0; r7--) {}   // (2)
 *   }
 */
SEC("xdp")
__success
__log_level(2)
__msg("loop header at 1, widening r6 to 0..3 step 1")
__msg("loop header at 2, widening r7 to 0..2 step 1")
__msg("exiting loop 2")
__msg("entering loop 4")
__msg("loop header at 4, widening r7 to 1..3 step 1")
__naked void sibling_inner_loops(void)
{
	asm volatile ("					\
	r6 = 0;						\
1:	r7 = 0;						\
2:	r7 += 1;					\
	if r7 < 3 goto 2b;				\
3:	r7 += -1;					\
	if r7 != 0 goto 3b;				\
	r6 += 1;					\
	if r6 < 4 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__success
__log_level(2)
__msg("loop header at 0, can't compute iterations count")
__naked void uninit_slot_counter(void)
{
	asm volatile ("					\
1:	r0 = *(u64 *)(r10 - 8);				\
	r0 += 1;					\
	*(u64 *)(r10 - 8) = r0;				\
	if r0 < 10 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * The assignment happens only on the second iteration:
 *
 *   r7 = 5;
 *   for (r6 = 0; r6 < 3; r6++)
 *           if (r6 == 1)
 *                   r7 = 10;
 *   if (r7 != 10)
 *           invalid_stack_read();
 *
 * R7 is always 10 at the real exit. Widening loses the correlation between
 * R6 and R7, leaving R7 in [5, 10] both at the loop header and after the loop.
 * The verifier therefore rejects the possible invalid stack read.
 */
SEC("xdp")
__failure
__log_level(2)
__msg("2: {{.*}}R7=scalar(smin=umin=smin32=umin32=5,smax=umax=smax32=umax32=10,var_off=(0x0; 0xf))")
__msg("7: R7=scalar(smin=umin=smin32=umin32=5,smax=umax=smax32=umax32=10,var_off=(0x0; 0xf))")
__msg("invalid read from stack R10 off=0 size=8")
__naked void conditional_assignment_on_second_iteration(void)
{
	asm volatile ("					\
	r7 = 5;						\
	r6 = 0;						\
1:	if r6 >= 3 goto 3f;				\
	if r6 != 1 goto 2f;				\
	r7 = 10;					\
2:	r6 += 1;					\
	goto 1b;					\
3:	if r7 == 10 goto 4f;				\
	r0 = *(u64 *)(r10 + 0);				\
4:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * The latch bound has header SCEV (any r7 100), so it is not invariant:
 *
 *   u64 i = 0, n = 3;
 *   for (;;) {
 *           ++i;
 *           if (i >= n)
 *                   break;
 *           if (i == 1)
 *                   n = 100;
 *   }
 *
 * The ANY expression must also appear at the latch, rather than a bare r7
 * that would incorrectly be treated as a loop-invariant bound.
 */
SEC("xdp")
__success
__log_level(2)
__msg("scev at header 2:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next("  r7=(any r7 100) / (any r7 100)")
__msg_next(" scev at latch 3:")
__msg_next("  r6=(+ r6 1) / (linear (+ r6 1) 1)")
__msg_next("  r7=r7 / (any r7 100)")
__naked void no_widen_changing_latch_bound(void)
{
	asm volatile ("					\
	r6 = 0;						\
	r7 = 3;						\
loop_%=:						\
	r6 += 1;					\
	if r6 >= r7 goto exit_%=;			\
	if r6 != 1 goto next_%=;			\
	r7 = 100;					\
next_%=:						\
	goto loop_%=;					\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * Join two stack pointer offsets via (any r7 r8), with r8 loop-invariant.
 * The joined range [-16, -8] still points to initialized stack memory,
 * so dereferencing r7 after the loop is safe.
 */
SEC("xdp")
__success __retval(0)
__log_level(2)
__msg("loop header at 7, widening r7 to -16..-8 step 1")
__msg("7: {{.*}}R7=fp(smin=smin32=-16,smax=smax32=-8,")
__msg("12: R7=fp(smin=smin32=-16,smax=smax32=-8,")
__naked void conditional_stack_pointer_assignment(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 16) = 0;				\
	*(u64 *)(r10 - 8) = 0;				\
	r7 = r10;					\
	r7 += -16;					\
	r8 = r10;					\
	r8 += -8;					\
	r6 = 0;						\
1:	if r6 >= 3 goto 3f;				\
	if r6 != 1 goto 2f;				\
	r7 = r8;					\
2:	r6 += 1;					\
	goto 1b;					\
3:	r0 = *(u64 *)(r7 + 0);				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Both choices are valid, but combining their offsets while retaining
 * small_map's metadata would invent small_map + 32, outside its value.
 *
 *   r6 = lookup(small_map, 0);
 *   r7 = lookup(map, 0) + 32;
 *   for (r8 = 0; r8 < 3; r8++) {
 *           if (r8 == 1)
 *                   r6 = r7;
 *           r0 = *(u32 *)(r6 + 0);
 *   }
 */
SEC("xdp")
__success __retval(0)
__log_level(2)
__msg("loop header at {{[0-9]+}}, can't widen r6, expr is")
__not_msg("widening")
__naked void no_widen_any_map_origin(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[small_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 3f;				\
	r6 = r0;					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 3f;				\
	r7 = r0;					\
	r7 += 32;					\
	r8 = 0;						\
1:	if r8 != 1 goto 2f;				\
	r6 = r7;					\
2:	r0 = *(u32 *)(r6 + 0);				\
	r8 += 1;					\
	if r8 < 3 goto 1b;				\
3:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(small_map), __imm_addr(map)
	: __clobber_all);
}

__naked __noinline __used static int any_stack_origin_callee(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r6 = r1;					\
	r7 = r10;					\
	r7 += -8;					\
	r8 = 0;						\
1:	if r8 != 1 goto 2f;				\
	r6 = r7;					\
2:	r8 += 1;					\
	if r8 < 3 goto 1b;				\
	r0 = *(u64 *)(r6 + 0);				\
	exit;						\
"	::: __clobber_all);
}

/*
 * The callee chooses between caller fp-16 and its own fp-8:
 *
 *   r6 = r1;
 *   r7 = fp - 8;
 *   for (r8 = 0; r8 < 3; r8++)
 *           if (r8 == 1)
 *                   r6 = r7;
 *   return *(u64 *)r6;
 *
 * Both locations hold zero. Their offsets belong to different frames
 * and cannot be combined into one stack pointer.
 */
SEC("xdp")
__success __retval(0)
__log_level(2)
__msg("loop header at {{[0-9]+}}, can't widen r6, expr is")
__not_msg("widening")
__naked void no_widen_any_stack_origin(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 16) = 0;				\
	r1 = r10;					\
	r1 += -16;					\
	call any_stack_origin_callee;			\
	exit;						\
"	::: __clobber_all);
}

__naked __noinline __used static int scrub_slot_callee(void)
{
	asm volatile ("					\
	*(u32 *)(r1 + 4) = 1;				\
	call %[bpf_get_prandom_u32];			\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * The callee turns the caller's fp-8 into misc, so the states at the call site
 * differ between the first and the second iteration, while the states inside
 * the callee are identical. Verification fails with an infinite loop error if
 * states.c:current_loop() does not search upper frames for active loops.
 */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 3, widening r6 to 0..9 step 1")
__msg("loop header at 3, clamping r6 to 0..9 step 1")
__msg("12: safe")
__naked void clamp_callee_state_pruning(void)
{
	asm volatile ("					\
	w1 = 1;						\
	*(u32 *)(r10 - 8) = r1;				\
	r6 = 0;						\
1:	r1 = r10;					\
	r1 += -8;					\
	call scrub_slot_callee;				\
	r6 += 1;					\
	if r6 < 10 goto 1b;				\
	r0 = *(u32 *)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

/*
 * The inner loop turns fp-8 from a spill into misc. On the next outer
 * iteration, the states inside the inner loop match its first invocation.
 */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 3, widening r6 to 0..9 step 1")
__msg("loop header at 4, widening r7 to 0..2 step 1")
__msg("loop header at 3, clamping r6 to 0..9 step 1")
__naked void clamp_nested_loop_state_pruning(void)
{
	asm volatile ("					\
	w1 = 1;						\
	*(u32 *)(r10 - 8) = r1;				\
	r6 = 0;						\
1:	r7 = 0;						\
2:	*(u32 *)(r10 - 4) = 1;				\
	r7 += 1;					\
	if r7 < 3 goto 2b;				\
	r6 += 1;					\
	if r6 < 10 goto 1b;				\
	r0 = *(u32 *)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

/*
 * Same as above, but the inner loop is not recognized by SCEV and is unrolled
 * with a precise r7. The states at its header still match between the outer
 * iterations and must not be reported as an infinite loop: the path between
 * them crosses the back edge of the terminating outer loop.
 */
SEC("socket")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 3, widening r6 to 0..9 step 1")
__msg("loop header at 5, can't compute iterations count")
__msg("loop header at 3, clamping r6 to 0..9 step 1")
__naked void clamp_nested_unrolled_loop_state_pruning(void)
{
	asm volatile ("					\
	w1 = 1;						\
	*(u32 *)(r10 - 8) = r1;				\
	r6 = 0;						\
1:	*(u32 *)(r10 - 4) = 1;				\
	r7 = 1;						\
2:	r7 *= 2;					\
	if r7 < 4 goto 2b;				\
	r6 += 1;					\
	if r6 < 10 goto 1b;				\
	r0 = *(u32 *)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

/*
 * r7 starts as 1 + 6*k and invariant r8 as 1 + 9*k (0 <= k <= 3).
 * The (any r7 r8) union must preserve base 1 and step gcd(6, 9) = 3.
 */
SEC("xdp")
__success __retval(1)
__log_level(2)
__msg("r7 += 1 {{.*}}step=1+6)")
__msg("r8 += 1 {{.*}}step=1+9)")
__msg("loop header at 9, widening r7 to 1..28 step 3")
__msg("9: {{.*}}R7=scalar({{.*}},step=1+3)")
__msg("14: R7=scalar({{.*}},step=1+3)")
__naked void conditional_scalar_assignment_gcd(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 3;					\
	r7 = r0;					\
	r7 *= 6;					\
	r7 += 1;					\
	r8 = r0;					\
	r8 *= 9;					\
	r8 += 1;					\
	r6 = 0;						\
1:	if r6 >= 3 goto 3f;				\
	if r6 != 1 goto 2f;				\
	r7 = r8;					\
2:	r6 += 1;					\
	goto 1b;					\
3:	r0 = r7;					\
	r0 %%= 3;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * r7 = ctx->data;
 * r8 = r7
 * for (r6 = 0; r6 < 10 && random() != 42; r7++, r6++);
 * if (r7 >= ctx->data_end)
 *   return;
 * *(r8 + 4);  // At this point the loop executed unknown number of times.
 *	       // Hence, r7 range gives no information about r8.
 */
SEC("tc")
__failure
__msg("R8 min value is outside of the allowed memory range")
__naked void break_pkt_pointers_id(void)
{
	asm volatile ("					\
	r7 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r8 = r7;					\
	r9 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 = 0;						\
1:	call %[bpf_get_prandom_u32];			\
	if r0 == 42 goto 3f;				\
	r6 += 1;					\
	r7 += 1;					\
	if r6 < 10 goto 1b;				\
3:	if r7 >= r9 goto 2f;				\
	r0 = *(u8*)(r8 + 4);				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * A loop induction variable used to compute the base address of a store to the
 * stack must not be widened: the spill offset would become varying, which the
 * verifier does not track.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, can't widen r2, expr is (+ r2 8), requires exact stack-offset tracking")
__naked void no_widen_stack_spill(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r2 = 0;						\
1:	r3 = r10;					\
	r3 += -64;					\
	r3 += r2;					\
	*(u64 *)(r3 + 0) = r0;				\
	r0 += 1;					\
	r2 += 8;					\
	if r0 < 4 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}
/*
 * Same hazard across a loop nest: the outer induction variable r2 addresses a
 * stack store performed inside the inner loop. The dependency is pulled up from
 * the inner loop, so the outer loop must not widen r2.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 2, can't widen r2, expr is (+ r2 8), requires exact stack-offset tracking")
__msg("loop header at 3, widening r1 to 0..1 step 1")
__naked void no_widen_stack_spill_nested(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r2 = 0;						\
1:	r1 = 0;						\
2:	r3 = r10;					\
	r3 += -64;					\
	r3 += r2;					\
	*(u64 *)(r3 + 0) = r1;				\
	r1 += 1;					\
	if r1 < 2 goto 2b;				\
	r0 += 1;					\
	r2 += 8;					\
	if r0 < 4 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * The inner loop restores r6 on its backedge but exits with r6 = -1000 on break.
 * Outer loop must account for the exit value instead of deriving r6 = r6_entry + n.
 * On the first outer backedge r6 is -999, so the next map access is invalid.
 *
 *   u8 *r7 = map_value;
 *   s64 r6 = 0;
 *   do {
 *   1:       r9 = 0;
 *           r0 = r7[r6];
 *           while (true) {
 *   2:              r8 = r6;
 *                   r6 = -1000;
 *                   if (++r9 > 5)
 *                           break;
 *                   r6 = r8;
 *   3:      }
 *           r6++;
 *   } while (r6 < 3);
 */
SEC("xdp")
__failure
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev at header 9:")
__msg_next("  r6=(+ ? 1) / ?")
__msg_next(" scev at latch 20:")
__msg_next("  r6=(+ ? 1) / (+ ? 1)")
__msg_next("scev at header 13:")
__msg_next("  r9=(+ r9 1) / (linear r9 1)")
__msg_next(" scev at latch 16:")
__msg_next("  r6=-1000 / -1000")
__msg_next("  r8=r6 / r6")
__msg_next("  r9=(+ r9 1) / (linear (+ r9 1) 1)")
__msg("R6=-999")
__msg("R1 min value is negative")
__naked void nested_loop_exit_clobbers_reg(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"r2 = r10;"
	"r2 += -8;"
	"r1 = %[map] ll;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto 4f;"
	"r7 = r0;"
	"r6 = 0;"
"1:"
	"r9 = 0;"
	"r1 = r7;"
	"r1 += r6;"
	"r0 = *(u8 *)(r1 + 0);"
"2:"
	"r8 = r6;"
	"r6 = -1000;"
	"r9 += 1;"
	"if r9 > 5 goto 3f;"
	"r6 = r8;"
	"goto 2b;"
"3:"
	"r6 += 1;"
	"if r6 s< 3 goto 1b;"
"4:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

/*
 * The inner loop exits both nested loops with r6 unchanged, or exits only itself with r6 = -1000.
 * The middle loop repairs the latter exit with a stack fill. It therefore preserves r6,
 * so the outermost loop must retain r6_entry + n.
 *
 *   r6 = 0;
 *   do {
 *           r7 = 0;
 *           do {
 *                   spill = r6;
 *                   r9 = 0;
 *                   do {
 *                           r8 = r6;
 *                           if (random() & 1)
 *                                   goto next;
 *                           r6 = -1000;
 *                           if (++r9 >= 2)
 *                                   break;
 *                           r6 = r8;
 *                   } while (true);
 *                   r6 = spill;
 *           } while (++r7 < 2);
 *   next:
 *           r6++;
 *   } while (r6 < 3);
 */
SEC("xdp")
__success __retval(3)
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("r6 = *(u64 *)(r10 -8) {{.*}}; r6 ? -> r6")
__msg("scev at header 1:")
__msg_next("  r6=(+ r6 1) / (linear r6 1)")
__msg_next(" scev at latch 16:")
__msg_next("  r6=(+ r6 1) / (linear (+ r6 1) 1)")
__msg("scev at header 2:")
__msg_next("  r7=(+ r7 1) / (linear r7 1)")
__msg("scev at header 4:")
__msg_next("  r9=(+ r9 1) / (linear r9 1)")
__msg(" scev at latch 9:")
__msg_next("  r8=r6 / r6")
__msg_next("  r9=(+ r9 1) / (linear (+ r9 1) 1)")
__naked void nested_loop_exit_preserves_reg(void)
{
	asm volatile (
	"r6 = 0;"
"1:"
	"r7 = 0;"
"2:"
	"*(u64 *)(r10 - 8) = r6;"
	"r9 = 0;"
"3:"
	"r8 = r6;"
	"call %[bpf_get_prandom_u32];"
	"if r0 & 1 goto 5f;"
	"r6 = -1000;"
	"r9 += 1;"
	"if r9 >= 2 goto 4f;"
	"r6 = r8;"
	"goto 3b;"
"4:"
	"r6 = *(u64 *)(r10 - 8);"
	"r7 += 1;"
	"if r7 < 2 goto 2b;"
"5:"
	"r6 += 1;"
	"if r6 < 3 goto 1b;"
	"r0 = r6;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Complement of no_widen_stack_spill: a sub-register (1-byte) store to the stack
 * lands as STACK_MISC and carries no tracked value, so the induction variable
 * addressing it (r2) is still widened.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("widening r2 to 0..24 step 8")
__naked void widen_byte_stack_store(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r2 = 0;						\
1:	r3 = r10;					\
	r3 += -64;					\
	r3 += r2;					\
	*(u8 *)(r3 + 0) = r0;				\
	r0 += 1;					\
	r2 += 8;					\
	if r0 < 4 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * A fill (BPF_LDX) at a varying stack offset loses precision just like a spill,
 * so the induction variable computing the load base (r2) must not be widened.
 * The slots are initialized up front so the fill itself is a valid read.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 6, can't widen r2, expr is (+ r2 8), requires exact stack-offset tracking")
__naked void no_widen_stack_fill(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64 *)(r10 - 64) = r0;			\
	*(u64 *)(r10 - 56) = r0;			\
	*(u64 *)(r10 - 48) = r0;			\
	*(u64 *)(r10 - 40) = r0;			\
	r2 = 0;						\
1:	r3 = r10;					\
	r3 += -64;					\
	r3 += r2;					\
	r4 = *(u64 *)(r3 + 0);				\
	r0 += 1;					\
	r2 += 8;					\
	if r0 < 4 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * struct xdp_md *slots[2] = { ctx, ctx };
 *
 * r7 = 0;
 * for (r8 = 0; r8 < 2; r8++) {
 *         value = slots[r7]->data;
 *         if (i == 0)
 *                 r7 = 1;
 * }
 *
 * SCEV expression for r7 is (any r7 8) and r7 is used to
 * address the stack memory. Avoid widening it, otherwise
 * verifier won't know the type of the slots[r7] expression.
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev at header 4:")
__msg_next("r7=(any r7 8) / (any r7 8)")
__msg("loop header at 4, can't widen r7, expr is {{.*}}, requires exact stack-offset tracking")
__naked void no_widen_stack_fill_any(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 16) = r1;			\
	*(u64 *)(r10 - 8) = r1;				\
	r7 = 0;						\
	r8 = 0;						\
1:	r2 = r10;					\
	r2 += -16;					\
	r2 += r7;					\
	r3 = *(u64 *)(r2 + 0);	/* slots[r7] */		\
	r0 = *(u32 *)(r3 + 0);	/* slots[r7]->data */	\
	if r8 != 0 goto 2f;				\
	r7 = 8;			/* conditionally update r7 */ \
2:	r8 += 1;					\
	if r8 < 2 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/*
 * A dynptr/iter/irq/res_spin_lock call initializes a stack object through a
 * pointer argument, which acts like a spill base: the induction variable
 * computing that argument's varying stack offset must not be widened, otherwise
 * the slot can't be resolved. Here each iteration constructs an xdp dynptr at
 * &dptrs[i].
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("can't widen {{.*}}, requires exact stack-offset tracking")
#ifndef __clang__
/* The issue is unrelated to SCEV */
__skip("GCC emits a stack-pointer loop condition the verifier cannot resolve")
#endif
int no_widen_dynptr_kfunc_arg(struct xdp_md *ctx)
{
	struct bpf_dynptr dptrs[4];
	int i;

#ifdef __clang__
#pragma clang loop unroll(disable)
#else
#pragma GCC unroll 0
#endif
	for (i = 0; i < 4; i++)
		bpf_dynptr_from_xdp(ctx, 0, &dptrs[i]);

	return 0;
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("header_count is 4 ")
__msg("widening r6 to -16..-13 step 1")
__naked void ptr_stack(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -16;					\
	r7 = r6;					\
	r7 += 4;					\
1:	r6 += 1;					\
	if r6 < r7 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

__naked __noinline __used static int ptr_stack_callee(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -16;					\
	r7 = r1;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 4f;	/* bound main pass iteration, but hide it from SCEV */	\
	if r6 < r7 goto 1b;	/* r6 and r7 are from different frames, can't be widened */	\
4:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* The end pointer belongs to the caller's frame. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (fp), r7 (fp)")
__not_msg("widening r6")
__naked void ptr_stack_other_frame(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -12;					\
	call ptr_stack_callee;				\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("scev: incompatible latch operands r6 (map_value), r7 (map_value)")
__msg("loop header at 11, can't compute iterations count at 14")
__naked void ptr_map_value_jmp32(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r7 = r6;					\
	r7 += 4;					\
	r9 = 0;						\
1:"	LIMIT_ITERATIONS				\
"	r6 += 1;					\
	if w6 < w7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map)
	: __clobber_all);
}

SEC("xdp")
__failure
__log_level(2)
__msg("loop header at 11, can't compute iterations count")
__msg("R6 32-bit pointer arithmetic prohibited")
__naked void ptr_map_value_alu32_latch(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r7 = r6;					\
	r7 += 4;					\
	r9 = 0;						\
1:"	LIMIT_ITERATIONS				\
"	w6 += 1;					\
	if r6 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("loop header at 11, can't compute iterations count")
__naked void ptr_map_value_alu32_latch2(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r7 = r6;					\
	r7 += 4;					\
	r9 = 0;						\
1:"	LIMIT_ITERATIONS				\
"	r6 += 1;					\
	w8 = w6;					\
	if r8 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__msg("loop header at 8, can't widen r0, expr is (zext32 (+ r0 1))")
__naked void ptr_map_value_alu32_non_latch(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r1 = 0;						\
1:	r1 += 1;					\
	w0 = w0;					\
	w0 += 1;					\
	if r1 < 4 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map)
	: __clobber_all);
}

/* Metadata and packet data have different origins. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (pkt_meta), r7 (pkt)")
__not_msg("widening r6")
__naked void ptr_packet_meta_other_origin(void)
{
	asm volatile ("					\
	r6 = *(u32 *)(r1 + %[data_meta]);		\
	r7 = *(u32 *)(r1 + %[data]);			\
	r7 += 4;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 4f;				\
	if r6 < r7 goto 1b;				\
4:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(data, offsetof(struct xdp_md, data)),
	  __imm_const(data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

/* Two lookups in a 1-element array count as the same memory origin. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("header_count is 4 ")
__msg("widening r6 to 0..3 step 1")
__naked void ptr_map_value_two_lookups(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[small_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[small_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r7 = r0;					\
	r7 += 4;					\
1:	r6 += 1;					\
	if r6 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(small_map)
	: __clobber_all);
}

/*
 * For inner array maps with BPF_F_INNER_MAP flag the actual array size
 * might differ from metadata in map declaration. Hence, r6 and r7 below
 * cannot be directly compared / iterations count can't be inferred.
 *
 *   r8 = bpf_map_lookup_elem(outer_map, ...); // inner map
 *   r6 = bpf_map_lookup_elem(r8, ...);
 *   r7 = bpf_map_lookup_elem(r8, ...);
 *   r7 += 4;
 *   do { r6++; ... } while (r6 < r7);
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (map_value), r7 (map_value)")
__not_msg("widening r6")
__naked void ptr_map_value_two_lookups_inner_map(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[outer_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r8 = r0;					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = r8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r1 = r8;					\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r7 = r0;					\
	r7 += 4;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 2f;	/* main pass iteration guard */ \
	if r6 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(outer_map)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("header_count is 4 ")
__msg("widening r6 to 0..3 step 1")
__naked void ptr_map_value(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r7 = r6;					\
	r7 += 4;					\
1:	r6 += 1;					\
	if r6 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map)
	: __clobber_all);
}

/* Equal offsets into different maps do not establish a common origin. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (map_value), r7 (map_value)")
__not_msg("widening r6")
__naked void ptr_map_value_other_map(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r6 = r0;					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[other_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 2f;				\
	r7 = r0;					\
	r7 += 4;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 4f;				\
	if r6 < r7 goto 1b;				\
4:							\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem), __imm_addr(map), __imm_addr(other_map)
	: __clobber_all);
}

SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("header_count is 4 ")
__msg("widening r6 to 0..3 step 1")
__naked void ptr_mem(void)
{
	asm volatile ("					\
	r1 = %[ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	if r0 == 0 goto 2f;				\
	r8 = r0;					\
	r6 = r0;					\
	r7 = r6;					\
	r7 += 4;					\
1:	r6 += 1;					\
	if r6 < r7 goto 1b;				\
	r1 = r8;					\
	r2 = 0;						\
	call %[bpf_ringbuf_discard];			\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ringbuf_reserve), __imm(bpf_ringbuf_discard), __imm_addr(ringbuf)
	: __clobber_all);
}

/* Separate reservations have different pointer IDs. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (ringbuf_mem), r7 (ringbuf_mem)")
__not_msg("widening r6")
__naked void ptr_mem_other_reservation(void)
{
	asm volatile ("					\
	r1 = %[ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	if r0 == 0 goto 3f;				\
	r8 = r0;					\
	r6 = r0;					\
	r1 = %[ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	if r0 == 0 goto 2f;				\
	r9 = r0;					\
	r7 = r0;					\
	r7 += 4;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 4f;				\
	if r6 < r7 goto 1b;				\
4:	r1 = r9;					\
	r2 = 0;						\
	call %[bpf_ringbuf_discard];			\
2:	r1 = r8;					\
	r2 = 0;						\
	call %[bpf_ringbuf_discard];			\
3:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ringbuf_reserve), __imm(bpf_ringbuf_discard), __imm_addr(ringbuf)
	: __clobber_all);
}

SEC("iter/bpf_map_elem")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("header_count is 4 ")
__msg("widening r6 to 0..3 step 1")
__naked void ptr_buf(void)
{
	asm volatile ("					\
	r6 = *(u64 *)(r1 + %[value]);			\
	if r6 == 0 goto 2f;				\
	r7 = r6;					\
	r7 += 4;					\
1:	r6 += 1;					\
	if r6 < r7 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(value, offsetof(struct bpf_iter__bpf_map_elem, value))
	: __clobber_all);
}

/* Separate context loads receive different pointer IDs. */
SEC("iter/bpf_map_elem")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r6 (buf), r7 (buf)")
__not_msg("widening r6")
__naked void ptr_buf_other_load(void)
{
	asm volatile ("					\
	r6 = *(u64 *)(r1 + %[value]);			\
	if r6 == 0 goto 2f;				\
	r7 = *(u64 *)(r1 + %[value]);			\
	if r7 == 0 goto 2f;				\
	r7 += 4;					\
	r0 = 0;						\
1:	r6 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto 4f;				\
	if r6 < r7 goto 1b;				\
4:							\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(value, offsetof(struct bpf_iter__bpf_map_elem, value))
	: __clobber_all);
}

/* A pointer-versus-scalar latch must not establish an iteration count. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("scev: incompatible latch operands r1 (map_value), scalar 4")
__not_msg("widening r1")
__naked void ptr_vs_scalar_no_widen(void)
{
	asm volatile ("					\
	*(u64 *)(r10 - 8) = 0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto out_%=;				\
	r1 = r0;					\
	r0 = 0;						\
loop_%=:						\
	r1 += 1;					\
	r0 += 1;					\
	if r0 & 8 goto out_%=;				\
	if r1 < 4 goto loop_%=;				\
out_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

/* A nullable pointer must not be widened before its null check. */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop header at 9, can't widen r1, expr is (+ r1 8)")
__naked void no_widen_maybe_null(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64 *)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map] ll;					\
	call %[bpf_map_lookup_elem];			\
	r1 = r0;					\
	r6 = 0;						\
loop_%=:						\
	if r1 == 0 goto exit_%=;			\
	r1 += 8;					\
	r6 += 1;					\
	if r6 < 3 goto loop_%=;				\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

/*
 * A terminating inner loop does not prove termination of the outer loop.
 *
 *    u64 r7 = 0;
 *    while (bpf_get_prandom_u32() != 42) {
 *        r7 = (r7 + 1) & 0xf;
 *        u64 r8 = 0;
 *        do {
 *            r8++;
 *        } while (r8 < 3);
 *    }
 *    return 0;
 */
SEC("xdp")
__failure
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("The sequence of 8193 jumps is too complex.")
__naked void nested_terminating_prune(void)
{
	asm volatile ("					\
	r7 = 0;						\
outer_%=:						\
	r7 += 1;					\
	r7 &= 0xf;					\
	call %[bpf_get_prandom_u32];			\
	if r0 == 42 goto out_%=;			\
	r8 = 0;						\
inner_%=:						\
	r8 += 1;					\
	if r8 < 3 goto inner_%=;			\
	goto outer_%=;					\
out_%=:							\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Returning directly from the inner loop to the outer header changes the
 * active stack from [1, 4] to [1].
 */
SEC("xdp")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("loop at 1{{$}}")
__msg("loop at 4, nested in 1")
__msg("entering loop 1")
__msg("entering loop 4")
__msg("exiting loop 4")
__not_msg("entering loop 1")
__msg("from 4 to 1:")
__msg("loop_stack=1{{$}}")
__naked void nested_loop_backedge_to_outer_header(void)
{
	asm volatile ("					\
	r6 = 0;						\
outer_%=:						\
	if r6 == 3 goto exit_%=;			\
	r6 += 1;					\
	r7 = 0;						\
inner_%=:						\
	if r7 == 2 goto outer_%=;			\
	r7 += 1;					\
	goto inner_%=;					\
exit_%=:						\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Induction variable as the src operand of the latch compare. */
SEC("socket")
__success
__log_level(2)
__msg("loop header at 2, header_count is 10")
__msg("loop header at 2, widening r1 to 0..9 step 1")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void post_cond_ivar_src(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 10;					\
1:	r1 += 1;					\
	if r2 != r1 goto 1b;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Early exit dominates the latch and gives a tighter bound. */
SEC("socket")
__success
__log_level(2)
__msg("  backedge from 4, latch at 4, dominating exits at 2")
__msg("loop header at 2, header_count is [0..15]")
__msg("loop header at 2, widening r1 to 0..14 step 1")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void dominating_exit_bounds_count(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 14;					\
1:	if r2 == r1 goto 2f;				\
	r1 += 1;					\
	if r1 != 63 goto 1b;				\
2:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
