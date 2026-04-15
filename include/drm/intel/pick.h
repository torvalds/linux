/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef _PICK_H_
#define _PICK_H_

/*
 * Given the first two numbers __a and __b of arbitrarily many evenly spaced
 * numbers, pick the 0-based __index'th value.
 *
 * Always prefer this over _PICK() if the numbers are evenly spaced.
 */
#define _PICK_EVEN(__index, __a, __b) ((__a) + (__index) * ((__b) - (__a)))

/*
 * Like _PICK_EVEN(), but supports 2 ranges of evenly spaced address offsets.
 * @__c_index corresponds to the index in which the second range starts to be
 * used. Using math interval notation, the first range is used for indexes [ 0,
 * @__c_index), while the second range is used for [ @__c_index, ... ). Example:
 *
 * #define _FOO_A			0xf000
 * #define _FOO_B			0xf004
 * #define _FOO_C			0xf008
 * #define _SUPER_FOO_A			0xa000
 * #define _SUPER_FOO_B			0xa100
 * #define FOO(x)			_MMIO(_PICK_EVEN_2RANGES(x, 3,		\
 *					      _FOO_A, _FOO_B,			\
 *					      _SUPER_FOO_A, _SUPER_FOO_B))
 *
 * This expands to:
 *	0: 0xf000,
 *	1: 0xf004,
 *	2: 0xf008,
 *	3: 0xa000,
 *	4: 0xa100,
 *	5: 0xa200,
 *	...
 */
#define _PICK_EVEN_2RANGES(__index, __c_index, __a, __b, __c, __d)		\
	(BUILD_BUG_ON_ZERO(!__is_constexpr(__c_index)) +			\
	 ((__index) < (__c_index) ? _PICK_EVEN(__index, __a, __b) :		\
				   _PICK_EVEN((__index) - (__c_index), __c, __d)))

/*
 * Given the arbitrary numbers in varargs, pick the 0-based __index'th number.
 *
 * Always prefer _PICK_EVEN() over this if the numbers are evenly spaced.
 */
#define _PICK(__index, ...) (((const u32 []){ __VA_ARGS__ })[__index])

#endif
