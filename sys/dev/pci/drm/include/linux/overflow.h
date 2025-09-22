/* Public domain. */

#ifndef _LINUX_OVERFLOW_H
#define _LINUX_OVERFLOW_H

#define array_size(x, y)	((x) * (y))

#define struct_size(p, member, n) \
	(sizeof(*(p)) + ((n) * (sizeof(*(p)->member))))

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 5)
#define check_add_overflow(x, y, sum)	__builtin_add_overflow(x, y, sum)
#define check_sub_overflow(x, y, z)	__builtin_sub_overflow(x, y, z)
#define check_mul_overflow(x, y, z)	__builtin_mul_overflow(x, y, z)
#else
#define check_mul_overflow(x, y, z) ({		\
	*(z) = (x) * (y);			\
	0;					\
})
#endif

#endif
