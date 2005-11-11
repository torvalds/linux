/*
 *  include/asm-s390/uaccess.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/uaccess.h"
 */
#ifndef __S390_UACCESS_H
#define __S390_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>
#include <linux/errno.h>

#define VERIFY_READ     0
#define VERIFY_WRITE    1


/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(a)  ((mm_segment_t) { (a) })


#define KERNEL_DS       MAKE_MM_SEG(0)
#define USER_DS         MAKE_MM_SEG(1)

#define get_ds()        (KERNEL_DS)
#define get_fs()        (current->thread.mm_segment)

#ifdef __s390x__
#define set_fs(x) \
({									\
	unsigned long __pto;						\
	current->thread.mm_segment = (x);				\
	__pto = current->thread.mm_segment.ar4 ?			\
		S390_lowcore.user_asce : S390_lowcore.kernel_asce;	\
	asm volatile ("lctlg 7,7,%0" : : "m" (__pto) );			\
})
#else
#define set_fs(x) \
({									\
	unsigned long __pto;						\
	current->thread.mm_segment = (x);				\
	__pto = current->thread.mm_segment.ar4 ?			\
		S390_lowcore.user_asce : S390_lowcore.kernel_asce;	\
	asm volatile ("lctl  7,7,%0" : : "m" (__pto) );			\
})
#endif

#define segment_eq(a,b) ((a).ar4 == (b).ar4)


#define __access_ok(addr,size) (1)

#define access_ok(type,addr,size) __access_ok(addr,size)

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

#ifndef __s390x__
#define __uaccess_fixup \
	".section .fixup,\"ax\"\n"	\
	"2: lhi    %0,%4\n"		\
	"   bras   1,3f\n"		\
	"   .long  1b\n"		\
	"3: l      1,0(1)\n"		\
	"   br     1\n"			\
	".previous\n"			\
	".section __ex_table,\"a\"\n"	\
	"   .align 4\n"			\
	"   .long  0b,2b\n"		\
	".previous"
#define __uaccess_clobber "cc", "1"
#else /* __s390x__ */
#define __uaccess_fixup \
	".section .fixup,\"ax\"\n"	\
	"2: lghi   %0,%4\n"		\
	"   jg     1b\n"		\
	".previous\n"			\
	".section __ex_table,\"a\"\n"	\
	"   .align 8\n"			\
	"   .quad  0b,2b\n"		\
	".previous"
#define __uaccess_clobber "cc"
#endif /* __s390x__ */

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
#define __put_user_asm(x, ptr, err) \
({								\
	err = 0;						\
	asm volatile(						\
		"0: mvcs  0(%1,%2),%3,%0\n"			\
		"1:\n"						\
		__uaccess_fixup					\
		: "+&d" (err)					\
		: "d" (sizeof(*(ptr))), "a" (ptr), "Q" (x),	\
		  "K" (-EFAULT)					\
		: __uaccess_clobber );				\
})
#else
#define __put_user_asm(x, ptr, err) \
({								\
	err = 0;						\
	asm volatile(						\
		"0: mvcs  0(%1,%2),0(%3),%0\n"			\
		"1:\n"						\
		__uaccess_fixup					\
		: "+&d" (err)					\
		: "d" (sizeof(*(ptr))), "a" (ptr), "a" (&(x)),	\
		  "K" (-EFAULT), "m" (x)			\
		: __uaccess_clobber );				\
})
#endif

#define __put_user(x, ptr) \
({								\
	__typeof__(*(ptr)) __x = (x);				\
	int __pu_err;						\
        __chk_user_ptr(ptr);                                    \
	switch (sizeof (*(ptr))) {				\
	case 1:							\
	case 2:							\
	case 4:							\
	case 8:							\
		__put_user_asm(__x, ptr, __pu_err);		\
		break;						\
	default:						\
		__put_user_bad();				\
		break;						\
	 }							\
	__pu_err;						\
})

#define put_user(x, ptr)					\
({								\
	might_sleep();						\
	__put_user(x, ptr);					\
})


extern int __put_user_bad(void) __attribute__((noreturn));

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
#define __get_user_asm(x, ptr, err) \
({								\
	err = 0;						\
	asm volatile (						\
		"0: mvcp  %O1(%2,%R1),0(%3),%0\n"		\
		"1:\n"						\
		__uaccess_fixup					\
		: "+&d" (err), "=Q" (x)				\
		: "d" (sizeof(*(ptr))), "a" (ptr),		\
		  "K" (-EFAULT)					\
		: __uaccess_clobber );				\
})
#else
#define __get_user_asm(x, ptr, err) \
({								\
	err = 0;						\
	asm volatile (						\
		"0: mvcp  0(%2,%5),0(%3),%0\n"			\
		"1:\n"						\
		__uaccess_fixup					\
		: "+&d" (err), "=m" (x)				\
		: "d" (sizeof(*(ptr))), "a" (ptr),		\
		  "K" (-EFAULT), "a" (&(x))			\
		: __uaccess_clobber );				\
})
#endif

#define __get_user(x, ptr)					\
({								\
	int __gu_err;						\
        __chk_user_ptr(ptr);                                    \
	switch (sizeof(*(ptr))) {				\
	case 1: {						\
		unsigned char __x;				\
		__get_user_asm(__x, ptr, __gu_err);		\
		(x) = (__typeof__(*(ptr))) __x;			\
		break;						\
	};							\
	case 2: {						\
		unsigned short __x;				\
		__get_user_asm(__x, ptr, __gu_err);		\
		(x) = (__typeof__(*(ptr))) __x;			\
		break;						\
	};							\
	case 4: {						\
		unsigned int __x;				\
		__get_user_asm(__x, ptr, __gu_err);		\
		(x) = (__typeof__(*(ptr))) __x;			\
		break;						\
	};							\
	case 8: {						\
		unsigned long long __x;				\
		__get_user_asm(__x, ptr, __gu_err);		\
		(x) = (__typeof__(*(ptr))) __x;			\
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
	might_sleep();						\
	__get_user(x, ptr);					\
})

extern int __get_user_bad(void) __attribute__((noreturn));

#define __put_user_unaligned __put_user
#define __get_user_unaligned __get_user

extern long __copy_to_user_asm(const void *from, long n, void __user *to);

/**
 * __copy_to_user: - Copy a block of data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
static inline unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_to_user_asm(from, n, to);
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from kernel space to user space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
static inline unsigned long
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_sleep();
	if (access_ok(VERIFY_WRITE, to, n))
		n = __copy_to_user(to, from, n);
	return n;
}

extern long __copy_from_user_asm(void *to, long n, const void __user *from);

/**
 * __copy_from_user: - Copy a block of data from user space, with less checking.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from user space to kernel space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 */
static inline unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_from_user_asm(to, n, from);
}

/**
 * copy_from_user: - Copy a block of data from user space.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.  This function may sleep.
 *
 * Copy data from user space to kernel space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 */
static inline unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	might_sleep();
	if (access_ok(VERIFY_READ, from, n))
		n = __copy_from_user(to, from, n);
	else
		memset(to, 0, n);
	return n;
}

extern unsigned long __copy_in_user_asm(const void __user *from, long n,
							void __user *to);

static inline unsigned long
__copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	return __copy_in_user_asm(from, n, to);
}

static inline unsigned long
copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	might_sleep();
	if (__access_ok(from,n) && __access_ok(to,n))
		n = __copy_in_user_asm(from, n, to);
	return n;
}

/*
 * Copy a null terminated string from userspace.
 */
extern long __strncpy_from_user_asm(long count, char *dst,
					const char __user *src);

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
        long res = -EFAULT;
        might_sleep();
        if (access_ok(VERIFY_READ, src, 1))
                res = __strncpy_from_user_asm(count, dst, src);
        return res;
}


extern long __strnlen_user_asm(long count, const char __user *src);

static inline unsigned long
strnlen_user(const char __user * src, unsigned long n)
{
	might_sleep();
	return __strnlen_user_asm(n, src);
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
#define strlen_user(str) strnlen_user(str, ~0UL)

/*
 * Zero Userspace
 */

extern long __clear_user_asm(void __user *to, long n);

static inline unsigned long
__clear_user(void __user *to, unsigned long n)
{
	return __clear_user_asm(to, n);
}

static inline unsigned long
clear_user(void __user *to, unsigned long n)
{
	might_sleep();
	if (access_ok(VERIFY_WRITE, to, n))
		n = __clear_user_asm(to, n);
	return n;
}

#endif /* __S390_UACCESS_H */
