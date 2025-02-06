/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2023 Google LLC.
 */

#ifndef __UNROLL_H
#define __UNROLL_H

#include <linux/args.h>

#ifdef CONFIG_CC_IS_CLANG
#define __pick_unrolled(x, y)		_Pragma(#x)
#elif CONFIG_GCC_VERSION >= 80000
#define __pick_unrolled(x, y)		_Pragma(#y)
#else
#define __pick_unrolled(x, y)		/* not supported */
#endif

/**
 * unrolled - loop attributes to ask the compiler to unroll it
 *
 * Usage:
 *
 * #define BATCH 8
 *
 *	unrolled_count(BATCH)
 *	for (u32 i = 0; i < BATCH; i++)
 *		// loop body without cross-iteration dependencies
 *
 * This is only a hint and the compiler is free to disable unrolling if it
 * thinks the count is suboptimal and may hurt performance and/or hugely
 * increase object code size.
 * Not having any cross-iteration dependencies (i.e. when iter x + 1 depends
 * on what iter x will do with variables) is not a strict requirement, but
 * provides best performance and object code size.
 * Available only on Clang and GCC 8.x onwards.
 */

/* Ask the compiler to pick an optimal unroll count, Clang only */
#define unrolled							\
	__pick_unrolled(clang loop unroll(enable), /* nothing */)

/* Unroll each @n iterations of the loop */
#define unrolled_count(n)						\
	__pick_unrolled(clang loop unroll_count(n), GCC unroll n)

/* Unroll the whole loop */
#define unrolled_full							\
	__pick_unrolled(clang loop unroll(full), GCC unroll 65534)

/* Never unroll the loop */
#define unrolled_none							\
	__pick_unrolled(clang loop unroll(disable), GCC unroll 1)

#define UNROLL(N, MACRO, args...) CONCATENATE(__UNROLL_, N)(MACRO, args)

#define __UNROLL_0(MACRO, args...)
#define __UNROLL_1(MACRO, args...)  __UNROLL_0(MACRO, args)  MACRO(0, args)
#define __UNROLL_2(MACRO, args...)  __UNROLL_1(MACRO, args)  MACRO(1, args)
#define __UNROLL_3(MACRO, args...)  __UNROLL_2(MACRO, args)  MACRO(2, args)
#define __UNROLL_4(MACRO, args...)  __UNROLL_3(MACRO, args)  MACRO(3, args)
#define __UNROLL_5(MACRO, args...)  __UNROLL_4(MACRO, args)  MACRO(4, args)
#define __UNROLL_6(MACRO, args...)  __UNROLL_5(MACRO, args)  MACRO(5, args)
#define __UNROLL_7(MACRO, args...)  __UNROLL_6(MACRO, args)  MACRO(6, args)
#define __UNROLL_8(MACRO, args...)  __UNROLL_7(MACRO, args)  MACRO(7, args)
#define __UNROLL_9(MACRO, args...)  __UNROLL_8(MACRO, args)  MACRO(8, args)
#define __UNROLL_10(MACRO, args...) __UNROLL_9(MACRO, args)  MACRO(9, args)
#define __UNROLL_11(MACRO, args...) __UNROLL_10(MACRO, args) MACRO(10, args)
#define __UNROLL_12(MACRO, args...) __UNROLL_11(MACRO, args) MACRO(11, args)
#define __UNROLL_13(MACRO, args...) __UNROLL_12(MACRO, args) MACRO(12, args)
#define __UNROLL_14(MACRO, args...) __UNROLL_13(MACRO, args) MACRO(13, args)
#define __UNROLL_15(MACRO, args...) __UNROLL_14(MACRO, args) MACRO(14, args)
#define __UNROLL_16(MACRO, args...) __UNROLL_15(MACRO, args) MACRO(15, args)
#define __UNROLL_17(MACRO, args...) __UNROLL_16(MACRO, args) MACRO(16, args)
#define __UNROLL_18(MACRO, args...) __UNROLL_17(MACRO, args) MACRO(17, args)
#define __UNROLL_19(MACRO, args...) __UNROLL_18(MACRO, args) MACRO(18, args)
#define __UNROLL_20(MACRO, args...) __UNROLL_19(MACRO, args) MACRO(19, args)

#endif /* __UNROLL_H */
