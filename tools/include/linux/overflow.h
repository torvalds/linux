/* SPDX-License-Identifier: GPL-2.0 OR MIT */
#ifndef __LINUX_OVERFLOW_H
#define __LINUX_OVERFLOW_H

#include <linux/compiler.h>

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
#define is_signed_type(type)       (((type)(-1)) < (type)1)
#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_min(T) ((T)((T)-type_max(T)-(T)1))

/*
 * For simplicity and code hygiene, the fallback code below insists on
 * a, b and *d having the same type (similar to the min() and max()
 * macros), whereas gcc's type-generic overflow checkers accept
 * different types. Hence we don't just make check_add_overflow an
 * alias for __builtin_add_overflow, but add type checks similar to
 * below.
 */
#define check_add_overflow(a, b, d) ({		\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_add_overflow(__a, __b, __d);	\
})

#define check_sub_overflow(a, b, d) ({		\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_sub_overflow(__a, __b, __d);	\
})

#define check_mul_overflow(a, b, d) ({		\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_mul_overflow(__a, __b, __d);	\
})

/**
 * array_size() - Calculate size of 2-dimensional array.
 *
 * @a: dimension one
 * @b: dimension two
 *
 * Calculates size of 2-dimensional array: @a * @b.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
static inline __must_check size_t array_size(size_t a, size_t b)
{
	size_t bytes;

	if (check_mul_overflow(a, b, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * array3_size() - Calculate size of 3-dimensional array.
 *
 * @a: dimension one
 * @b: dimension two
 * @c: dimension three
 *
 * Calculates size of 3-dimensional array: @a * @b * @c.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
static inline __must_check size_t array3_size(size_t a, size_t b, size_t c)
{
	size_t bytes;

	if (check_mul_overflow(a, b, &bytes))
		return SIZE_MAX;
	if (check_mul_overflow(bytes, c, &bytes))
		return SIZE_MAX;

	return bytes;
}

static inline __must_check size_t __ab_c_size(size_t n, size_t size, size_t c)
{
	size_t bytes;

	if (check_mul_overflow(n, size, &bytes))
		return SIZE_MAX;
	if (check_add_overflow(bytes, c, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * struct_size() - Calculate size of structure with trailing array.
 * @p: Pointer to the structure.
 * @member: Name of the array member.
 * @n: Number of elements in the array.
 *
 * Calculates size of memory needed for structure @p followed by an
 * array of @n @member elements.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define struct_size(p, member, n)					\
	__ab_c_size(n,							\
		    sizeof(*(p)->member) + __must_be_array((p)->member),\
		    sizeof(*(p)))

#endif /* __LINUX_OVERFLOW_H */
