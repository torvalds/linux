/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_UACCESS_H
#define __ASM_GENERIC_UACCESS_H

/*
 * User space memory access functions, these should work
 * on any machine that has kernel and user data in the same
 * address space, e.g. all NOMMU machines.
 */
#include <linux/string.h>

#include <asm/segment.h>

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#ifndef KERNEL_DS
#define KERNEL_DS	MAKE_MM_SEG(~0UL)
#endif

#ifndef USER_DS
#define USER_DS		MAKE_MM_SEG(TASK_SIZE - 1)
#endif

#ifndef get_fs
#define get_fs()	(current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}
#endif

#ifndef segment_eq
#define segment_eq(a, b) ((a).seg == (b).seg)
#endif

#define access_ok(addr, size) __access_ok((unsigned long)(addr),(size))

/*
 * The architecture should really override this if possible, at least
 * doing a check on the get_fs()
 */
#ifndef __access_ok
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	return 1;
}
#endif

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 * This version just falls back to copy_{from,to}_user, which should
 * provide a fast-path for small values.
 */
#define __put_user(x, ptr) \
({								\
	__typeof__(*(ptr)) __x = (x);				\
	int __pu_err = -EFAULT;					\
        __chk_user_ptr(ptr);                                    \
	switch (sizeof (*(ptr))) {				\
	case 1:							\
	case 2:							\
	case 4:							\
	case 8:							\
		__pu_err = __put_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		break;						\
	default:						\
		__put_user_bad();				\
		break;						\
	 }							\
	__pu_err;						\
})

#define put_user(x, ptr)					\
({								\
	void __user *__p = (ptr);				\
	might_fault();						\
	access_ok(__p, sizeof(*ptr)) ?		\
		__put_user((x), ((__typeof__(*(ptr)) __user *)__p)) :	\
		-EFAULT;					\
})

#ifndef __put_user_fn

static inline int __put_user_fn(size_t size, void __user *ptr, void *x)
{
	return unlikely(raw_copy_to_user(ptr, x, size)) ? -EFAULT : 0;
}

#define __put_user_fn(sz, u, k)	__put_user_fn(sz, u, k)

#endif

extern int __put_user_bad(void) __attribute__((noreturn));

#define __get_user(x, ptr)					\
({								\
	int __gu_err = -EFAULT;					\
	__chk_user_ptr(ptr);					\
	switch (sizeof(*(ptr))) {				\
	case 1: {						\
		unsigned char __x = 0;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 2: {						\
		unsigned short __x = 0;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 4: {						\
		unsigned int __x = 0;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 8: {						\
		unsigned long long __x = 0;			\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	default:						\
		__get_user_bad();				\
		break;						\
	}							\
	__gu_err;						\
})

#define get_user(x, ptr)					\
({								\
	const void __user *__p = (ptr);				\
	might_fault();						\
	access_ok(__p, sizeof(*ptr)) ?		\
		__get_user((x), (__typeof__(*(ptr)) __user *)__p) :\
		((x) = (__typeof__(*(ptr)))0,-EFAULT);		\
})

#ifndef __get_user_fn
static inline int __get_user_fn(size_t size, const void __user *ptr, void *x)
{
	return unlikely(raw_copy_from_user(x, ptr, size)) ? -EFAULT : 0;
}

#define __get_user_fn(sz, u, k)	__get_user_fn(sz, u, k)

#endif

extern int __get_user_bad(void) __attribute__((noreturn));

/*
 * Copy a null terminated string from userspace.
 */
#ifndef __strncpy_from_user
static inline long
__strncpy_from_user(char *dst, const char __user *src, long count)
{
	char *tmp;
	strncpy(dst, (const char __force *)src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--)
		;
	return (tmp - dst);
}
#endif

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	if (!access_ok(src, 1))
		return -EFAULT;
	return __strncpy_from_user(dst, src, count);
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
#ifndef __strnlen_user
#define __strnlen_user(s, n) (strnlen((s), (n)) + 1)
#endif

/*
 * Unlike strnlen, strnlen_user includes the nul terminator in
 * its returned count. Callers should check for a returned value
 * greater than N as an indication the string is too long.
 */
static inline long strnlen_user(const char __user *src, long n)
{
	if (!access_ok(src, 1))
		return 0;
	return __strnlen_user(src, n);
}

/*
 * Zero Userspace
 */
#ifndef __clear_user
static inline __must_check unsigned long
__clear_user(void __user *to, unsigned long n)
{
	memset((void __force *)to, 0, n);
	return 0;
}
#endif

static inline __must_check unsigned long
clear_user(void __user *to, unsigned long n)
{
	might_fault();
	if (!access_ok(to, n))
		return n;

	return __clear_user(to, n);
}

#include <asm/extable.h>

#endif /* __ASM_GENERIC_UACCESS_H */
