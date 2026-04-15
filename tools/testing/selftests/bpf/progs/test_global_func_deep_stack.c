// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/*
 * Macro tricks to tersely define for long non-recursive call chains. Add
 * computation to the functions prevent tail recursion from reducing the
 * stack size to 0.
 */

#define CAT(a, b) a ## b
#define XCAT(a, b) CAT(a, b)

#define F_0 \
__attribute__((noinline))         \
int f0(unsigned long a)           \
{                                 \
	volatile long b = a + 16; \
	if (a == 0)               \
		return 0;         \
	return b;                 \
}

#define FN(n, prev) \
__attribute__((noinline))                        \
int XCAT(f, n)(unsigned long a)                  \
{                                                \
	volatile long b = XCAT(f, prev)(a - 1);  \
	if (!b)                                  \
		return 0;                        \
	return b + 1;                            \
}

/* Call chain 33 levels deep. */
#define F_1 F_0         FN(1, 0)
#define F_2 F_1         FN(2, 1)
#define F_3 F_2         FN(3, 2)
#define F_4 F_3         FN(4, 3)
#define F_5 F_4         FN(5, 4)
#define F_6 F_5         FN(6, 5)
#define F_7 F_6         FN(7, 6)
#define F_8 F_7         FN(8, 7)
#define F_9 F_8         FN(9, 8)
#define F_10 F_9        FN(10, 9)
#define F_11 F_10       FN(11, 10)
#define F_12 F_11       FN(12, 11)
#define F_13 F_12       FN(13, 12)
#define F_14 F_13       FN(14, 13)
#define F_15 F_14       FN(15, 14)
#define F_16 F_15       FN(16, 15)
#define F_17 F_16       FN(17, 16)
#define F_18 F_17       FN(18, 17)
#define F_19 F_18       FN(19, 18)
#define F_20 F_19       FN(20, 19)
#define F_21 F_20       FN(21, 20)
#define F_22 F_21       FN(22, 21)
#define F_23 F_22       FN(23, 22)
#define F_24 F_23       FN(24, 23)
#define F_25 F_24       FN(25, 24)
#define F_26 F_25       FN(26, 25)
#define F_27 F_26       FN(27, 26)
#define F_28 F_27       FN(28, 27)
#define F_29 F_28       FN(29, 28)
#define F_30 F_29       FN(30, 29)
#define F_31 F_30       FN(31, 30)
#define F_32 F_31       FN(32, 31)

#define CAT2(a, b) a ## b
#define XCAT2(a, b) CAT2(a, b)

#define F(n) XCAT2(F_, n)

F(32)

/* Ensure that even 32 levels deep, the function verifies. */
SEC("syscall")
__success
int global_func_deep_stack_success(struct __sk_buff *skb)
{
	return f31(55);
}

/*
 * Check we actually honor stack limits (33 * 16 = 528 > 512 = MAX_STACK_DEPTH).
 * The stack depth is 16 because the verifier calls round_up_stack_depth() on
 * the size.
 */
SEC("syscall")
__failure __msg("combined stack size of 34 calls")
int global_func_deep_stack_fail(struct __sk_buff *skb)
{
	return f32(123);
}
