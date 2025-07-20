/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MINMAX_H
#define _LINUX_MINMAX_H

#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>

/*
 * min()/max()/clamp() macros must accomplish several things:
 *
 * - Avoid multiple evaluations of the arguments (so side-effects like
 *   "x++" happen only once) when non-constant.
 * - Perform signed v unsigned type-checking (to generate compile
 *   errors instead of nasty runtime surprises).
 * - Unsigned char/short are always promoted to signed int and can be
 *   compared against signed or unsigned arguments.
 * - Unsigned arguments can be compared against non-negative signed constants.
 * - Comparison of a signed argument against an unsigned constant fails
 *   even if the constant is below __INT_MAX__ and could be cast to int.
 */
#define __typecheck(x, y) \
	(!!(sizeof((typeof(x) *)1 == (typeof(y) *)1)))

/*
 * __sign_use for integer expressions:
 *   bit #0 set if ok for unsigned comparisons
 *   bit #1 set if ok for signed comparisons
 *
 * In particular, statically non-negative signed integer expressions
 * are ok for both.
 *
 * NOTE! Unsigned types smaller than 'int' are implicitly converted to 'int'
 * in expressions, and are accepted for signed conversions for now.
 * This is debatable.
 *
 * Note that 'x' is the original expression, and 'ux' is the unique variable
 * that contains the value.
 *
 * We use 'ux' for pure type checking, and 'x' for when we need to look at the
 * value (but without evaluating it for side effects!
 * Careful to only ever evaluate it with sizeof() or __builtin_constant_p() etc).
 *
 * Pointers end up being checked by the normal C type rules at the actual
 * comparison, and these expressions only need to be careful to not cause
 * warnings for pointer use.
 */
#define __sign_use(ux) (is_signed_type(typeof(ux)) ? \
	(2 + __is_nonneg(ux)) : (1 + 2 * (sizeof(ux) < 4)))

/*
 * Check whether a signed value is always non-negative.
 *
 * A cast is needed to avoid any warnings from values that aren't signed
 * integer types (in which case the result doesn't matter).
 *
 * On 64-bit any integer or pointer type can safely be cast to 'long long'.
 * But on 32-bit we need to avoid warnings about casting pointers to integers
 * of different sizes without truncating 64-bit values so 'long' or 'long long'
 * must be used depending on the size of the value.
 *
 * This does not work for 128-bit signed integers since the cast would truncate
 * them, but we do not use s128 types in the kernel (we do use 'u128',
 * but they are handled by the !is_signed_type() case).
 */
#if __SIZEOF_POINTER__ == __SIZEOF_LONG_LONG__
#define __is_nonneg(ux) statically_true((long long)(ux) >= 0)
#else
#define __is_nonneg(ux) statically_true( \
	(typeof(__builtin_choose_expr(sizeof(ux) > 4, 1LL, 1L)))(ux) >= 0)
#endif

#define __types_ok(ux, uy) \
	(__sign_use(ux) & __sign_use(uy))

#define __types_ok3(ux, uy, uz) \
	(__sign_use(ux) & __sign_use(uy) & __sign_use(uz))

#define __cmp_op_min <
#define __cmp_op_max >

#define __cmp(op, x, y)	((x) __cmp_op_##op (y) ? (x) : (y))

#define __cmp_once_unique(op, type, x, y, ux, uy) \
	({ type ux = (x); type uy = (y); __cmp(op, ux, uy); })

#define __cmp_once(op, type, x, y) \
	__cmp_once_unique(op, type, x, y, __UNIQUE_ID(x_), __UNIQUE_ID(y_))

#define __careful_cmp_once(op, x, y, ux, uy) ({		\
	__auto_type ux = (x); __auto_type uy = (y);	\
	BUILD_BUG_ON_MSG(!__types_ok(ux, uy),		\
		#op"("#x", "#y") signedness error");	\
	__cmp(op, ux, uy); })

#define __careful_cmp(op, x, y) \
	__careful_cmp_once(op, x, y, __UNIQUE_ID(x_), __UNIQUE_ID(y_))

/**
 * min - return minimum of two values of the same or compatible types
 * @x: first value
 * @y: second value
 */
#define min(x, y)	__careful_cmp(min, x, y)

/**
 * max - return maximum of two values of the same or compatible types
 * @x: first value
 * @y: second value
 */
#define max(x, y)	__careful_cmp(max, x, y)

/**
 * umin - return minimum of two non-negative values
 *   Signed types are zero extended to match a larger unsigned type.
 * @x: first value
 * @y: second value
 */
#define umin(x, y)	\
	__careful_cmp(min, (x) + 0u + 0ul + 0ull, (y) + 0u + 0ul + 0ull)

/**
 * umax - return maximum of two non-negative values
 * @x: first value
 * @y: second value
 */
#define umax(x, y)	\
	__careful_cmp(max, (x) + 0u + 0ul + 0ull, (y) + 0u + 0ul + 0ull)

#define __careful_op3(op, x, y, z, ux, uy, uz) ({			\
	__auto_type ux = (x); __auto_type uy = (y);__auto_type uz = (z);\
	BUILD_BUG_ON_MSG(!__types_ok3(ux, uy, uz),			\
		#op"3("#x", "#y", "#z") signedness error");		\
	__cmp(op, ux, __cmp(op, uy, uz)); })

/**
 * min3 - return minimum of three values
 * @x: first value
 * @y: second value
 * @z: third value
 */
#define min3(x, y, z) \
	__careful_op3(min, x, y, z, __UNIQUE_ID(x_), __UNIQUE_ID(y_), __UNIQUE_ID(z_))

/**
 * max3 - return maximum of three values
 * @x: first value
 * @y: second value
 * @z: third value
 */
#define max3(x, y, z) \
	__careful_op3(max, x, y, z, __UNIQUE_ID(x_), __UNIQUE_ID(y_), __UNIQUE_ID(z_))

/**
 * min_t - return minimum of two values, using the specified type
 * @type: data type to use
 * @x: first value
 * @y: second value
 */
#define min_t(type, x, y) __cmp_once(min, type, x, y)

/**
 * max_t - return maximum of two values, using the specified type
 * @type: data type to use
 * @x: first value
 * @y: second value
 */
#define max_t(type, x, y) __cmp_once(max, type, x, y)

/**
 * min_not_zero - return the minimum that is _not_ zero, unless both are zero
 * @x: value1
 * @y: value2
 */
#define min_not_zero(x, y) ({			\
	typeof(x) __x = (x);			\
	typeof(y) __y = (y);			\
	__x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })

#define __clamp(val, lo, hi)	\
	((val) >= (hi) ? (hi) : ((val) <= (lo) ? (lo) : (val)))

#define __clamp_once(type, val, lo, hi, uval, ulo, uhi) ({			\
	type uval = (val);							\
	type ulo = (lo);							\
	type uhi = (hi);							\
	BUILD_BUG_ON_MSG(statically_true(ulo > uhi),				\
		"clamp() low limit " #lo " greater than high limit " #hi);	\
	BUILD_BUG_ON_MSG(!__types_ok3(uval, ulo, uhi),				\
		"clamp("#val", "#lo", "#hi") signedness error");		\
	__clamp(uval, ulo, uhi); })

#define __careful_clamp(type, val, lo, hi) \
	__clamp_once(type, val, lo, hi, __UNIQUE_ID(v_), __UNIQUE_ID(l_), __UNIQUE_ID(h_))

/**
 * clamp - return a value clamped to a given range with typechecking
 * @val: current value
 * @lo: lowest allowable value
 * @hi: highest allowable value
 *
 * This macro checks @val/@lo/@hi to make sure they have compatible
 * signedness.
 */
#define clamp(val, lo, hi) __careful_clamp(__auto_type, val, lo, hi)

/**
 * clamp_t - return a value clamped to a given range using a given type
 * @type: the type of variable to use
 * @val: current value
 * @lo: minimum allowable value
 * @hi: maximum allowable value
 *
 * This macro does no typechecking and uses temporary variables of type
 * @type to make all the comparisons.
 */
#define clamp_t(type, val, lo, hi) __careful_clamp(type, val, lo, hi)

/**
 * clamp_val - return a value clamped to a given range using val's type
 * @val: current value
 * @lo: minimum allowable value
 * @hi: maximum allowable value
 *
 * This macro does no typechecking and uses temporary variables of whatever
 * type the input argument @val is.  This is useful when @val is an unsigned
 * type and @lo and @hi are literals that will otherwise be assigned a signed
 * integer type.
 */
#define clamp_val(val, lo, hi) __careful_clamp(typeof(val), val, lo, hi)

/*
 * Do not check the array parameter using __must_be_array().
 * In the following legit use-case where the "array" passed is a simple pointer,
 * __must_be_array() will return a failure.
 * --- 8< ---
 * int *buff
 * ...
 * min = min_array(buff, nb_items);
 * --- 8< ---
 *
 * The first typeof(&(array)[0]) is needed in order to support arrays of both
 * 'int *buff' and 'int buff[N]' types.
 *
 * The array can be an array of const items.
 * typeof() keeps the const qualifier. Use __unqual_scalar_typeof() in order
 * to discard the const qualifier for the __element variable.
 */
#define __minmax_array(op, array, len) ({				\
	typeof(&(array)[0]) __array = (array);				\
	typeof(len) __len = (len);					\
	__unqual_scalar_typeof(__array[0]) __element = __array[--__len];\
	while (__len--)							\
		__element = op(__element, __array[__len]);		\
	__element; })

/**
 * min_array - return minimum of values present in an array
 * @array: array
 * @len: array length
 *
 * Note that @len must not be zero (empty array).
 */
#define min_array(array, len) __minmax_array(min, array, len)

/**
 * max_array - return maximum of values present in an array
 * @array: array
 * @len: array length
 *
 * Note that @len must not be zero (empty array).
 */
#define max_array(array, len) __minmax_array(max, array, len)

static inline bool in_range64(u64 val, u64 start, u64 len)
{
	return (val - start) < len;
}

static inline bool in_range32(u32 val, u32 start, u32 len)
{
	return (val - start) < len;
}

/**
 * in_range - Determine if a value lies within a range.
 * @val: Value to test.
 * @start: First value in range.
 * @len: Number of values in range.
 *
 * This is more efficient than "if (start <= val && val < (start + len))".
 * It also gives a different answer if @start + @len overflows the size of
 * the type by a sufficient amount to encompass @val.  Decide for yourself
 * which behaviour you want, or prove that start + len never overflow.
 * Do not blindly replace one form with the other.
 */
#define in_range(val, start, len)					\
	((sizeof(start) | sizeof(len) | sizeof(val)) <= sizeof(u32) ?	\
		in_range32(val, start, len) : in_range64(val, start, len))

/**
 * swap - swap values of @a and @b
 * @a: first value
 * @b: second value
 */
#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/*
 * Use these carefully: no type checking, and uses the arguments
 * multiple times. Use for obvious constants only.
 */
#define MIN(a, b) __cmp(min, a, b)
#define MAX(a, b) __cmp(max, a, b)
#define MIN_T(type, a, b) __cmp(min, (type)(a), (type)(b))
#define MAX_T(type, a, b) __cmp(max, (type)(a), (type)(b))

#endif	/* _LINUX_MINMAX_H */
