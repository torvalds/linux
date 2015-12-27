#ifndef __ASM_GENERIC_UACCESS_H
#define __ASM_GENERIC_UACCESS_H

/*
 * User space memory access functions, these should work
 * on any machine that has kernel and user data in the same
 * address space, e.g. all NOMMU machines.
 */
#include <linux/sched.h>
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
#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}
#endif

#ifndef segment_eq
#define segment_eq(a, b) ((a).seg == (b).seg)
#endif

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define access_ok(type, addr, size) __access_ok((unsigned long)(addr),(size))

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
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);

/*
 * architectures with an MMU should override these two
 */
#ifndef __copy_from_user
static inline __must_check long __copy_from_user(void *to,
		const void __user * from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch(n) {
		case 1:
			*(u8 *)to = *(u8 __force *)from;
			return 0;
		case 2:
			*(u16 *)to = *(u16 __force *)from;
			return 0;
		case 4:
			*(u32 *)to = *(u32 __force *)from;
			return 0;
#ifdef CONFIG_64BIT
		case 8:
			*(u64 *)to = *(u64 __force *)from;
			return 0;
#endif
		default:
			break;
		}
	}

	memcpy(to, (const void __force *)from, n);
	return 0;
}
#endif

#ifndef __copy_to_user
static inline __must_check long __copy_to_user(void __user *to,
		const void *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch(n) {
		case 1:
			*(u8 __force *)to = *(u8 *)from;
			return 0;
		case 2:
			*(u16 __force *)to = *(u16 *)from;
			return 0;
		case 4:
			*(u32 __force *)to = *(u32 *)from;
			return 0;
#ifdef CONFIG_64BIT
		case 8:
			*(u64 __force *)to = *(u64 *)from;
			return 0;
#endif
		default:
			break;
		}
	}

	memcpy((void __force *)to, from, n);
	return 0;
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
	void *__p = (ptr);					\
	might_fault();						\
	access_ok(VERIFY_WRITE, __p, sizeof(*ptr)) ?		\
		__put_user((x), ((__typeof__(*(ptr)) *)__p)) :	\
		-EFAULT;					\
})

#ifndef __put_user_fn

static inline int __put_user_fn(size_t size, void __user *ptr, void *x)
{
	size = __copy_to_user(ptr, x, size);
	return size ? -EFAULT : size;
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
		unsigned char __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 2: {						\
		unsigned short __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 4: {						\
		unsigned int __x;				\
		__gu_err = __get_user_fn(sizeof (*(ptr)),	\
					 ptr, &__x);		\
		(x) = *(__force __typeof__(*(ptr)) *) &__x;	\
		break;						\
	};							\
	case 8: {						\
		unsigned long long __x;				\
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
	const void *__p = (ptr);				\
	might_fault();						\
	access_ok(VERIFY_READ, __p, sizeof(*ptr)) ?		\
		__get_user((x), (__typeof__(*(ptr)) *)__p) :	\
		-EFAULT;					\
})

#ifndef __get_user_fn
static inline int __get_user_fn(size_t size, const void __user *ptr, void *x)
{
	size = __copy_from_user(x, ptr, size);
	return size ? -EFAULT : size;
}

#define __get_user_fn(sz, u, k)	__get_user_fn(sz, u, k)

#endif

extern int __get_user_bad(void) __attribute__((noreturn));

#ifndef __copy_from_user_inatomic
#define __copy_from_user_inatomic __copy_from_user
#endif

#ifndef __copy_to_user_inatomic
#define __copy_to_user_inatomic __copy_to_user
#endif

static inline long copy_from_user(void *to,
		const void __user * from, unsigned long n)
{
	might_fault();
	if (access_ok(VERIFY_READ, from, n))
		return __copy_from_user(to, from, n);
	else
		return n;
}

static inline long copy_to_user(void __user *to,
		const void *from, unsigned long n)
{
	might_fault();
	if (access_ok(VERIFY_WRITE, to, n))
		return __copy_to_user(to, from, n);
	else
		return n;
}

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
	if (!access_ok(VERIFY_READ, src, 1))
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
	if (!access_ok(VERIFY_READ, src, 1))
		return 0;
	return __strnlen_user(src, n);
}

static inline long strlen_user(const char __user *src)
{
	return strnlen_user(src, 32767);
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
	if (!access_ok(VERIFY_WRITE, to, n))
		return n;

	return __clear_user(to, n);
}

#endif /* __ASM_GENERIC_UACCESS_H */
