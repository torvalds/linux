/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef _REG_BITS_H_
#define _REG_BITS_H_

#include <linux/bitfield.h>
#include <linux/bits.h>

/*
 * Wrappers over the generic fixed width BIT_U*() and GENMASK_U*()
 * implementations, for compatibility reasons with previous implementation.
 */
#define REG_GENMASK(high, low)		GENMASK_U32(high, low)
#define REG_GENMASK64(high, low)	GENMASK_U64(high, low)
#define REG_GENMASK16(high, low)	GENMASK_U16(high, low)
#define REG_GENMASK8(high, low)		GENMASK_U8(high, low)

#define REG_BIT(n)			BIT_U32(n)
#define REG_BIT64(n)			BIT_U64(n)
#define REG_BIT16(n)			BIT_U16(n)
#define REG_BIT8(n)			BIT_U8(n)

/*
 * Local integer constant expression version of is_power_of_2().
 */
#define IS_POWER_OF_2(__x)		((__x) && (((__x) & ((__x) - 1)) == 0))

/**
 * REG_FIELD_PREP8() - Prepare a u8 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP() to generate an integer constant expression, force
 * u8 and for consistency with REG_FIELD_GET8(), REG_BIT8() and REG_GENMASK8().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP8(__mask, __val)                                          \
	((u8)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +      \
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +             \
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U8_MAX) +          \
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

/**
 * REG_FIELD_PREP16() - Prepare a u16 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP16() to generate an integer constant
 * expression, force u8 and for consistency with
 * REG_FIELD_GET16(), REG_BIT16() and REG_GENMASK16().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP16(__mask, __val)                                          \
	((u16)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +      \
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +             \
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U16_MAX) +          \
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

/**
 * REG_FIELD_PREP() - Prepare a u32 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to put in the field
 *
 * Local copy of FIELD_PREP() to generate an integer constant expression, force
 * u32 and for consistency with REG_FIELD_GET(), REG_BIT() and REG_GENMASK().
 *
 * @return: @__val masked and shifted into the field defined by @__mask.
 */
#define REG_FIELD_PREP(__mask, __val)						\
	((u32)((((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) +	\
	       BUILD_BUG_ON_ZERO(!__is_constexpr(__mask)) +		\
	       BUILD_BUG_ON_ZERO((__mask) == 0 || (__mask) > U32_MAX) +		\
	       BUILD_BUG_ON_ZERO(!IS_POWER_OF_2((__mask) + (1ULL << __bf_shf(__mask)))) + \
	       BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr(__val), (~((__mask) >> __bf_shf(__mask)) & (__val)), 0))))

/**
 * REG_FIELD_GET8() - Extract a u8 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u8 and for consistency with
 * REG_FIELD_PREP(), REG_BIT() and REG_GENMASK().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET8(__mask, __val)   ((u8)FIELD_GET(__mask, __val))

/**
 * REG_FIELD_GET() - Extract a u32 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u32 and for consistency with
 * REG_FIELD_PREP(), REG_BIT() and REG_GENMASK().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET(__mask, __val)	((u32)FIELD_GET(__mask, __val))

/**
 * REG_FIELD_GET64() - Extract a u64 bitfield value
 * @__mask: shifted mask defining the field's length and position
 * @__val: value to extract the bitfield value from
 *
 * Local wrapper for FIELD_GET() to force u64 and for consistency with
 * REG_GENMASK64().
 *
 * @return: Masked and shifted value of the field defined by @__mask in @__val.
 */
#define REG_FIELD_GET64(__mask, __val)	((u64)FIELD_GET(__mask, __val))

/**
 * REG_FIELD_MAX() - produce the maximum value representable by a field
 * @__mask: shifted mask defining the field's length and position
 *
 * Local wrapper for FIELD_MAX() to return the maximum bit value that can
 * be held in the field specified by @_mask, cast to u32 for consistency
 * with other macros.
 */
#define REG_FIELD_MAX(__mask)	((u32)FIELD_MAX(__mask))

#define REG_MASKED_FIELD(mask, value) \
	(BUILD_BUG_ON_ZERO(__builtin_choose_expr(__builtin_constant_p(mask), (mask) & 0xffff0000, 0)) + \
	 BUILD_BUG_ON_ZERO(__builtin_choose_expr(__builtin_constant_p(value), (value) & 0xffff0000, 0)) + \
	 BUILD_BUG_ON_ZERO(__builtin_choose_expr(__builtin_constant_p(mask) && __builtin_constant_p(value), (value) & ~(mask), 0)) + \
	 ((mask) << 16 | (value)))

#define REG_MASKED_FIELD_ENABLE(a) \
	(__builtin_choose_expr(__builtin_constant_p(a), REG_MASKED_FIELD((a), (a)), ({ typeof(a) _a = (a); REG_MASKED_FIELD(_a, _a); })))

#define REG_MASKED_FIELD_DISABLE(a) \
	(REG_MASKED_FIELD((a), 0))

#endif
