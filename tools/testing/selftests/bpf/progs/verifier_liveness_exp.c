// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/*
 * Exponential complexity in analyze_subprog() liveness analysis.
 *
 * analyze_subprog() recurses into each call site that passes FP-derived
 * arguments, creating a unique func_instance per (callsite, depth).
 * There is no memoization for callees reached with equivalent entry args.
 * Even if memoization were added, it can be defeated by passing a distinct
 * FP offset at each call site.  arg_track keys on (frame, off[]), so
 * r1=fp-8, r1=fp-16, ... r1=fp-400 produce 50 unique cache keys per level.
 *
 * This test chains 8 subprograms (the MAX_CALL_FRAMES limit).  Each
 * intermediate function calls the next one 50 times, each time with a
 * different FP-relative offset in r1.
 *
 * Without complexity limits in analyze_subprog() the resulting 50^7 ~ 7.8 * 10^11
 * recursive analyze_subprog() calls will cause a CPU soft lockup or OOM.
 *
 * The BPF program itself is ~1200 instructions and perfectly valid.
 */

char _license[] SEC("license") = "GPL";

/* Call fn with r1 = r10 + off (a unique FP-derived arg per call site) */
#define C(fn, off)	"r1 = r10;"		\
			"r1 += -" #off ";"	\
			"call " #fn ";"

/* 50 calls, each with a distinct FP offset: -8, -16, ... -400 */
#define CALLS_50(fn)							\
	C(fn,   8) C(fn,  16) C(fn,  24) C(fn,  32) C(fn,  40)		\
	C(fn,  48) C(fn,  56) C(fn,  64) C(fn,  72) C(fn,  80)		\
	C(fn,  88) C(fn,  96) C(fn, 104) C(fn, 112) C(fn, 120)		\
	C(fn, 128) C(fn, 136) C(fn, 144) C(fn, 152) C(fn, 160)		\
	C(fn, 168) C(fn, 176) C(fn, 184) C(fn, 192) C(fn, 200)		\
	C(fn, 208) C(fn, 216) C(fn, 224) C(fn, 232) C(fn, 240)		\
	C(fn, 248) C(fn, 256) C(fn, 264) C(fn, 272) C(fn, 280)		\
	C(fn, 288) C(fn, 296) C(fn, 304) C(fn, 312) C(fn, 320)		\
	C(fn, 328) C(fn, 336) C(fn, 344) C(fn, 352) C(fn, 360)		\
	C(fn, 368) C(fn, 376) C(fn, 384) C(fn, 392) C(fn, 400)

/* Leaf: depth 7, no further calls */
__naked __noinline __used
static unsigned long exp_sub7(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 6 -> calls exp_sub7 x50 with distinct offsets */
__naked __noinline __used
static unsigned long exp_sub6(void)
{
	asm volatile (
		CALLS_50(exp_sub7)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 5 -> calls exp_sub6 x50 */
__naked __noinline __used
static unsigned long exp_sub5(void)
{
	asm volatile (
		CALLS_50(exp_sub6)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 4 -> calls exp_sub5 x50 */
__naked __noinline __used
static unsigned long exp_sub4(void)
{
	asm volatile (
		CALLS_50(exp_sub5)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 3 -> calls exp_sub4 x50 */
__naked __noinline __used
static unsigned long exp_sub3(void)
{
	asm volatile (
		CALLS_50(exp_sub4)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 2 -> calls exp_sub3 x50 */
__naked __noinline __used
static unsigned long exp_sub2(void)
{
	asm volatile (
		CALLS_50(exp_sub3)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* depth 1 -> calls exp_sub2 x50 */
__naked __noinline __used
static unsigned long exp_sub1(void)
{
	asm volatile (
		CALLS_50(exp_sub2)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/*
 * Entry: depth 0.  Calls exp_sub1 50 times, each with a distinct
 * FP offset in r1.  Every call site produces a unique arg_track,
 * defeating any memoization keyed on entry args.
 */
SEC("?raw_tp")
__failure __log_level(2)
__msg("liveness analysis exceeded complexity limit")
__naked int liveness_exponential_complexity(void)
{
	asm volatile (
		CALLS_50(exp_sub1)
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}
