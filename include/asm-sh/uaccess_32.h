/* $Id: uaccess.h,v 1.11 2003/10/13 07:21:20 lethal Exp $
 *
 * User space memory access functions
 *
 * Copyright (C) 1999, 2002  Niibe Yutaka
 * Copyright (C) 2003  Paul Mundt
 *
 *  Based on:
 *     MIPS implementation version 1.15 by
 *              Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *     and i386 version.
 */
#ifndef __ASM_SH_UACCESS_H
#define __ASM_SH_UACCESS_H

#include <linux/errno.h>
#include <linux/sched.h>

#define VERIFY_READ    0
#define VERIFY_WRITE   1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons (Data Segment Register?), these macros are misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFFUL)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define segment_eq(a,b)	((a).seg == (b).seg)

#define get_ds()	(KERNEL_DS)

#if !defined(CONFIG_MMU)
/* NOMMU is always true */
#define __addr_ok(addr) (1)

static inline mm_segment_t get_fs(void)
{
	return USER_DS;
}

static inline void set_fs(mm_segment_t s)
{
}

/*
 * __access_ok: Check if address with size is OK or not.
 *
 * If we don't have an MMU (or if its disabled) the only thing we really have
 * to look out for is if the address resides somewhere outside of what
 * available RAM we have.
 *
 * TODO: This check could probably also stand to be restricted somewhat more..
 * though it still does the Right Thing(tm) for the time being.
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	return ((addr >= memory_start) && ((addr + size) < memory_end));
}
#else /* CONFIG_MMU */
#define __addr_ok(addr) \
	((unsigned long)(addr) < (current_thread_info()->addr_limit.seg))

#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

/*
 * __access_ok: Check if address with size is OK or not.
 *
 * Uhhuh, this needs 33-bit arithmetic. We have a carry..
 *
 * sum := addr + size;  carry? --> flag = true;
 * if (sum >= addr_limit) flag = true;
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	unsigned long flag, sum;

	__asm__("clrt\n\t"
		"addc	%3, %1\n\t"
		"movt	%0\n\t"
		"cmp/hi	%4, %1\n\t"
		"rotcl	%0"
		:"=&r" (flag), "=r" (sum)
		:"1" (addr), "r" (size),
		 "r" (current_thread_info()->addr_limit.seg)
		:"t");
	return flag == 0;
}
#endif /* CONFIG_MMU */

#define access_ok(type, addr, size)	\
	(__chk_user_ptr(addr),		\
	 __access_ok((unsigned long __force)(addr), (size)))

/*
 * Uh, these should become the main single-value transfer routines ...
 * They automatically use the right size if we just have the right
 * pointer type ...
 *
 * As SuperH uses the same address space for kernel and user data, we
 * can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)		__put_user_check((x), (ptr), sizeof(*(ptr)))
#define get_user(x,ptr)		__get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr)	__put_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __get_user(x,ptr)	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

#define __get_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		__get_user_asm(x, ptr, retval, "b");		\
		break;						\
	case 2:							\
		__get_user_asm(x, ptr, retval, "w");		\
		break;						\
	case 4:							\
		__get_user_asm(x, ptr, retval, "l");		\
		break;						\
	default:						\
		__get_user_unknown();				\
		break;						\
	}							\
} while (0)

#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err;						\
	unsigned long __gu_val;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);	\
	__chk_user_ptr(ptr);					\
	__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT;					\
	unsigned long __gu_val = 0;					\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	if (likely(access_ok(VERIFY_READ, __gu_addr, (size))))		\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_asm(x, addr, err, insn) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov." insn "	%2, %1\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov	#0, %1\n\t" \
	"mov.l	4f, %0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3, %0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	:"=&r" (err), "=&r" (x) \
	:"m" (__m(addr)), "i" (-EFAULT), "0" (err)); })

extern void __get_user_unknown(void);

#define __put_user_size(x,ptr,size,retval)		\
do {							\
	retval = 0;					\
	switch (size) {					\
	case 1:						\
		__put_user_asm(x, ptr, retval, "b");	\
		break;					\
	case 2:						\
		__put_user_asm(x, ptr, retval, "w");	\
		break;					\
	case 4:						\
		__put_user_asm(x, ptr, retval, "l");	\
		break;					\
	case 8:						\
		__put_user_u64(x, ptr, retval);		\
		break;					\
	default:					\
		__put_user_unknown();			\
	}						\
} while (0)

#define __put_user_nocheck(x,ptr,size)				\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	__chk_user_ptr(ptr);					\
	__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;						\
})

#define __put_user_check(x,ptr,size)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);		\
	if (likely(access_ok(VERIFY_WRITE, __pu_addr, size)))	\
		__put_user_size((x), __pu_addr, (size),		\
				__pu_err);			\
	__pu_err;						\
})

#define __put_user_asm(x, addr, err, insn) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov." insn "	%1, %2\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov.l	4f, %0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3, %0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	:"=&r" (err) \
	:"r" (x), "m" (__m(addr)), "i" (-EFAULT), "0" (err)	\
        :"memory"); })

#if defined(CONFIG_CPU_LITTLE_ENDIAN)
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%R1,%2\n\t" \
	"mov.l	%S1,%T2\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT), "0" (retval) \
        : "memory"); })
#else
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%S1,%2\n\t" \
	"mov.l	%R1,%T2\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT), "0" (retval) \
        : "memory"); })
#endif

extern void __put_user_unknown(void);

/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied */
__kernel_size_t __copy_user(void *to, const void *from, __kernel_size_t n);

#define copy_to_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_to, __copy_size)) { \
__copy_res = __copy_user(__copy_to, (void *) (from), __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

#define copy_from_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
void *__copy_from = (void *) (from); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_from, __copy_size)) { \
__copy_res = __copy_user(__copy_to, __copy_from, __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

static __always_inline unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_user(to, (__force void *)from, n);
}

static __always_inline unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user((__force void *)to, from, n);
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/*
 * Clear the area and return remaining number of bytes
 * (on failure.  Usually it's 0.)
 */
extern __kernel_size_t __clear_user(void *addr, __kernel_size_t size);

#define clear_user(addr,n) ({ \
void * __cl_addr = (addr); \
unsigned long __cl_size = (n); \
if (__cl_size && __access_ok(((unsigned long)(__cl_addr)), __cl_size)) \
__cl_size = __clear_user(__cl_addr, __cl_size); \
__cl_size; })

static __inline__ int
__strncpy_from_user(unsigned long __dest, unsigned long __user __src, int __count)
{
	__kernel_size_t res;
	unsigned long __dummy, _d, _s, _c;

	__asm__ __volatile__(
		"9:\n"
		"mov.b	@%2+, %1\n\t"
		"cmp/eq	#0, %1\n\t"
		"bt/s	2f\n"
		"1:\n"
		"mov.b	%1, @%3\n\t"
		"dt	%4\n\t"
		"bf/s	9b\n\t"
		" add	#1, %3\n\t"
		"2:\n\t"
		"sub	%4, %0\n"
		"3:\n"
		".section .fixup,\"ax\"\n"
		"4:\n\t"
		"mov.l	5f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	%9, %0\n\t"
		".balign 4\n"
		"5:	.long 3b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 9b,4b\n"
		".previous"
		: "=r" (res), "=&z" (__dummy), "=r" (_s), "=r" (_d), "=r"(_c)
		: "0" (__count), "2" (__src), "3" (__dest), "4" (__count),
		  "i" (-EFAULT)
		: "memory", "t");

	return res;
}

/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
#define strncpy_from_user(dest,src,count) ({ \
unsigned long __sfu_src = (unsigned long) (src); \
int __sfu_count = (int) (count); \
long __sfu_res = -EFAULT; \
if(__access_ok(__sfu_src, __sfu_count)) { \
__sfu_res = __strncpy_from_user((unsigned long) (dest), __sfu_src, __sfu_count); \
} __sfu_res; })

/*
 * Return the size of a string (including the ending 0 even when we have
 * exceeded the maximum string length).
 */
static __inline__ long __strnlen_user(const char __user *__s, long __n)
{
	unsigned long res;
	unsigned long __dummy;

	__asm__ __volatile__(
		"1:\t"
		"mov.b	@(%0,%3), %1\n\t"
		"cmp/eq	%4, %0\n\t"
		"bt/s	2f\n\t"
		" add	#1, %0\n\t"
		"tst	%1, %1\n\t"
		"bf	1b\n\t"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:\n\t"
		"mov.l	4f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	#0, %0\n"
		".balign 4\n"
		"4:	.long 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 1b,3b\n"
		".previous"
		: "=z" (res), "=&r" (__dummy)
		: "0" (0), "r" (__s), "r" (__n)
		: "t");
	return res;
}

/**
 * strnlen_user: - Get the size of a string in user space.
 * @s: The string to measure.
 * @n: The maximum valid length
 *
 * Context: User context only.  This function may sleep.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
static __inline__ long strnlen_user(const char __user *s, long n)
{
	if (!__addr_ok(s))
		return 0;
	else
		return __strnlen_user(s, n);
}

/**
 * strlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 *
 * Context: User context only.  This function may sleep.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 *
 * If there is a limit on the length of a valid string, you may wish to
 * consider using strnlen_user() instead.
 */
#define strlen_user(str)	strnlen_user(str, ~0UL >> 1)

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

extern int fixup_exception(struct pt_regs *regs);

#endif /* __ASM_SH_UACCESS_H */
