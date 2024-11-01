/* SPDX-License-Identifier: GPL-2.0 OR MIT */
#ifndef __LINUX_OVERFLOW_H
#define __LINUX_OVERFLOW_H

#include <linux/compiler.h>
#include <linux/limits.h>
#include <linux/const.h>

/*
 * We need to compute the minimum and maximum values representable in a given
 * type. These macros may also be useful elsewhere. It would seem more obvious
 * to do something like:
 *
 * #define type_min(T) (T)(is_signed_type(T) ? (T)1 << (8*sizeof(T)-1) : 0)
 * #define type_max(T) (T)(is_signed_type(T) ? ((T)1 << (8*sizeof(T)-1)) - 1 : ~(T)0)
 *
 * Unfortunately, the middle expressions, strictly speaking, have
 * undefined behaviour, and at least some versions of gcc warn about
 * the type_max expression (but not if -fsanitize=undefined is in
 * effect; in that case, the warning is deferred to runtime...).
 *
 * The slightly excessive casting in type_min is to make sure the
 * macros also produce sensible values for the exotic type _Bool. [The
 * overflow checkers only almost work for _Bool, but that's
 * a-feature-not-a-bug, since people shouldn't be doing arithmetic on
 * _Bools. Besides, the gcc builtins don't allow _Bool* as third
 * argument.]
 *
 * Idea stolen from
 * https://mail-index.netbsd.org/tech-misc/2007/02/05/0000.html -
 * credit to Christian Biere.
 */
#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_min(T) ((T)((T)-type_max(T)-(T)1))

/*
 * Avoids triggering -Wtype-limits compilation warning,
 * while using unsigned data types to check a < 0.
 */
#define is_non_negative(a) ((a) > 0 || (a) == 0)
#define is_negative(a) (!(is_non_negative(a)))

/*
 * Allows for effectively applying __must_check to a macro so we can have
 * both the type-agnostic benefits of the macros while also being able to
 * enforce that the return value is, in fact, checked.
 */
static inline bool __must_check __must_check_overflow(bool overflow)
{
	return unlikely(overflow);
}

/**
 * check_add_overflow() - Calculate addition with overflow checking
 * @a: first addend
 * @b: second addend
 * @d: pointer to store sum
 *
 * Returns 0 on success.
 *
 * *@d holds the results of the attempted addition, but is not considered
 * "safe for use" on a non-zero return value, which indicates that the
 * sum has overflowed or been truncated.
 */
#define check_add_overflow(a, b, d)	\
	__must_check_overflow(__builtin_add_overflow(a, b, d))

/**
 * check_sub_overflow() - Calculate subtraction with overflow checking
 * @a: minuend; value to subtract from
 * @b: subtrahend; value to subtract from @a
 * @d: pointer to store difference
 *
 * Returns 0 on success.
 *
 * *@d holds the results of the attempted subtraction, but is not considered
 * "safe for use" on a non-zero return value, which indicates that the
 * difference has underflowed or been truncated.
 */
#define check_sub_overflow(a, b, d)	\
	__must_check_overflow(__builtin_sub_overflow(a, b, d))

/**
 * check_mul_overflow() - Calculate multiplication with overflow checking
 * @a: first factor
 * @b: second factor
 * @d: pointer to store product
 *
 * Returns 0 on success.
 *
 * *@d holds the results of the attempted multiplication, but is not
 * considered "safe for use" on a non-zero return value, which indicates
 * that the product has overflowed or been truncated.
 */
#define check_mul_overflow(a, b, d)	\
	__must_check_overflow(__builtin_mul_overflow(a, b, d))

/**
 * check_shl_overflow() - Calculate a left-shifted value and check overflow
 * @a: Value to be shifted
 * @s: How many bits left to shift
 * @d: Pointer to where to store the result
 *
 * Computes *@d = (@a << @s)
 *
 * Returns true if '*@d' cannot hold the result or when '@a << @s' doesn't
 * make sense. Example conditions:
 *
 * - '@a << @s' causes bits to be lost when stored in *@d.
 * - '@s' is garbage (e.g. negative) or so large that the result of
 *   '@a << @s' is guaranteed to be 0.
 * - '@a' is negative.
 * - '@a << @s' sets the sign bit, if any, in '*@d'.
 *
 * '*@d' will hold the results of the attempted shift, but is not
 * considered "safe for use" if true is returned.
 */
#define check_shl_overflow(a, s, d) __must_check_overflow(({		\
	typeof(a) _a = a;						\
	typeof(s) _s = s;						\
	typeof(d) _d = d;						\
	u64 _a_full = _a;						\
	unsigned int _to_shift =					\
		is_non_negative(_s) && _s < 8 * sizeof(*d) ? _s : 0;	\
	*_d = (_a_full << _to_shift);					\
	(_to_shift != _s || is_negative(*_d) || is_negative(_a) ||	\
	(*_d >> _to_shift) != _a);					\
}))

#define __overflows_type_constexpr(x, T) (			\
	is_unsigned_type(typeof(x)) ?				\
		(x) > type_max(typeof(T)) :			\
	is_unsigned_type(typeof(T)) ?				\
		(x) < 0 || (x) > type_max(typeof(T)) :		\
	(x) < type_min(typeof(T)) || (x) > type_max(typeof(T)))

#define __overflows_type(x, T)		({	\
	typeof(T) v = 0;			\
	check_add_overflow((x), v, &v);		\
})

/**
 * overflows_type - helper for checking the overflows between value, variables,
 *		    or data type
 *
 * @n: source constant value or variable to be checked
 * @T: destination variable or data type proposed to store @x
 *
 * Compares the @x expression for whether or not it can safely fit in
 * the storage of the type in @T. @x and @T can have different types.
 * If @x is a constant expression, this will also resolve to a constant
 * expression.
 *
 * Returns: true if overflow can occur, false otherwise.
 */
#define overflows_type(n, T)					\
	__builtin_choose_expr(__is_constexpr(n),		\
			      __overflows_type_constexpr(n, T),	\
			      __overflows_type(n, T))

/**
 * castable_to_type - like __same_type(), but also allows for casted literals
 *
 * @n: variable or constant value
 * @T: variable or data type
 *
 * Unlike the __same_type() macro, this allows a constant value as the
 * first argument. If this value would not overflow into an assignment
 * of the second argument's type, it returns true. Otherwise, this falls
 * back to __same_type().
 */
#define castable_to_type(n, T)						\
	__builtin_choose_expr(__is_constexpr(n),			\
			      !__overflows_type_constexpr(n, T),	\
			      __same_type(n, T))

/**
 * size_mul() - Calculate size_t multiplication with saturation at SIZE_MAX
 * @factor1: first factor
 * @factor2: second factor
 *
 * Returns: calculate @factor1 * @factor2, both promoted to size_t,
 * with any overflow causing the return value to be SIZE_MAX. The
 * lvalue must be size_t to avoid implicit type conversion.
 */
static inline size_t __must_check size_mul(size_t factor1, size_t factor2)
{
	size_t bytes;

	if (check_mul_overflow(factor1, factor2, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * size_add() - Calculate size_t addition with saturation at SIZE_MAX
 * @addend1: first addend
 * @addend2: second addend
 *
 * Returns: calculate @addend1 + @addend2, both promoted to size_t,
 * with any overflow causing the return value to be SIZE_MAX. The
 * lvalue must be size_t to avoid implicit type conversion.
 */
static inline size_t __must_check size_add(size_t addend1, size_t addend2)
{
	size_t bytes;

	if (check_add_overflow(addend1, addend2, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * size_sub() - Calculate size_t subtraction with saturation at SIZE_MAX
 * @minuend: value to subtract from
 * @subtrahend: value to subtract from @minuend
 *
 * Returns: calculate @minuend - @subtrahend, both promoted to size_t,
 * with any overflow causing the return value to be SIZE_MAX. For
 * composition with the size_add() and size_mul() helpers, neither
 * argument may be SIZE_MAX (or the result with be forced to SIZE_MAX).
 * The lvalue must be size_t to avoid implicit type conversion.
 */
static inline size_t __must_check size_sub(size_t minuend, size_t subtrahend)
{
	size_t bytes;

	if (minuend == SIZE_MAX || subtrahend == SIZE_MAX ||
	    check_sub_overflow(minuend, subtrahend, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * array_size() - Calculate size of 2-dimensional array.
 * @a: dimension one
 * @b: dimension two
 *
 * Calculates size of 2-dimensional array: @a * @b.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
#define array_size(a, b)	size_mul(a, b)

/**
 * array3_size() - Calculate size of 3-dimensional array.
 * @a: dimension one
 * @b: dimension two
 * @c: dimension three
 *
 * Calculates size of 3-dimensional array: @a * @b * @c.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
#define array3_size(a, b, c)	size_mul(size_mul(a, b), c)

/**
 * flex_array_size() - Calculate size of a flexible array member
 *                     within an enclosing structure.
 * @p: Pointer to the structure.
 * @member: Name of the flexible array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of a flexible array of @count number of @member
 * elements, at the end of structure @p.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define flex_array_size(p, member, count)				\
	__builtin_choose_expr(__is_constexpr(count),			\
		(count) * sizeof(*(p)->member) + __must_be_array((p)->member),	\
		size_mul(count, sizeof(*(p)->member) + __must_be_array((p)->member)))

/**
 * struct_size() - Calculate size of structure with trailing flexible array.
 * @p: Pointer to the structure.
 * @member: Name of the array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of memory needed for structure @p followed by an
 * array of @count number of @member elements.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define struct_size(p, member, count)					\
	__builtin_choose_expr(__is_constexpr(count),			\
		sizeof(*(p)) + flex_array_size(p, member, count),	\
		size_add(sizeof(*(p)), flex_array_size(p, member, count)))

#endif /* __LINUX_OVERFLOW_H */
