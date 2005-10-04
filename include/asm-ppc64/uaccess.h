#ifndef _PPC64_UACCESS_H
#define _PPC64_UACCESS_H

/* 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/errno.h>
#include <asm/processor.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)  ((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(0UL)
#define USER_DS		MAKE_MM_SEG(0xf000000000000000UL)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->thread.fs)
#define set_fs(val)	(current->thread.fs = (val))

#define segment_eq(a,b)	((a).seg == (b).seg)

/*
 * Use the alpha trick for checking ranges:
 *
 * Is a address valid? This does a straightforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 *
 * We dont have to check for high bits in (addr+size) because the first
 * two checks force the maximum result to be below the start of the
 * kernel region.
 */
#define __access_ok(addr,size,segment) \
	(((segment).seg & (addr | size )) == 0)

#define access_ok(type,addr,size) \
	__access_ok(((__force unsigned long)(addr)),(size),get_fs())

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
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 *
 * As we use the same address space for kernel and user data on the
 * PowerPC, we can just do these as direct assignments.  (Of course, the
 * exception handling means that it's no longer "just"...)
 */
#define get_user(x,ptr) \
  __get_user_check((x),(ptr),sizeof(*(ptr)))
#define put_user(x,ptr) \
  __put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user(x,ptr) \
  __get_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __put_user(x,ptr) \
  __put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user_unaligned __get_user
#define __put_user_unaligned __put_user

extern long __put_user_bad(void);

#define __put_user_nocheck(x,ptr,size)				\
({								\
	long __pu_err;						\
	might_sleep();						\
	__chk_user_ptr(ptr);					\
	__put_user_size((x),(ptr),(size),__pu_err,-EFAULT);	\
	__pu_err;						\
})

#define __put_user_check(x,ptr,size)					\
({									\
	long __pu_err = -EFAULT;					\
	void __user *__pu_addr = (ptr);					\
	might_sleep();							\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))			\
		__put_user_size((x),__pu_addr,(size),__pu_err,-EFAULT);	\
	__pu_err;							\
})

#define __put_user_size(x,ptr,size,retval,errret)			\
do {									\
	retval = 0;							\
	switch (size) {							\
	  case 1: __put_user_asm(x,ptr,retval,"stb",errret); break;	\
	  case 2: __put_user_asm(x,ptr,retval,"sth",errret); break;	\
	  case 4: __put_user_asm(x,ptr,retval,"stw",errret); break;	\
	  case 8: __put_user_asm(x,ptr,retval,"std",errret); break; 	\
	  default: __put_user_bad();					\
	}								\
} while (0)

/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 */
#define __put_user_asm(x, addr, err, op, errret)		\
	__asm__ __volatile__(					\
		"1:	"op" %1,0(%2)  	# put_user\n" 	 	\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	li %0,%3\n"				\
		"	b 2b\n"					\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 3\n"				\
		"	.llong 1b,3b\n"				\
		".previous"					\
		: "=r"(err)					\
		: "r"(x), "b"(addr), "i"(errret), "0"(err))


#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err;						\
	unsigned long __gu_val;					\
	might_sleep();						\
	__get_user_size(__gu_val,(ptr),(size),__gu_err,-EFAULT);\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT;					\
	unsigned long __gu_val = 0;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	might_sleep();							\
	if (access_ok(VERIFY_READ,__gu_addr,size))			\
		__get_user_size(__gu_val,__gu_addr,(size),__gu_err,-EFAULT);\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval,errret)			\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __get_user_asm(x,ptr,retval,"lbz",errret); break;	\
	  case 2: __get_user_asm(x,ptr,retval,"lhz",errret); break;	\
	  case 4: __get_user_asm(x,ptr,retval,"lwz",errret); break;	\
	  case 8: __get_user_asm(x,ptr,retval,"ld",errret);  break;	\
	  default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, op, errret)	\
	__asm__ __volatile__(				\
		"1:	"op" %1,0(%2)	# get_user\n"  	\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %0,%3\n"			\
		"	li %1,0\n"			\
		"	b 2b\n"				\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align 3\n"			\
		"	.llong 1b,3b\n"			\
		".previous"				\
		: "=r"(err), "=r"(x)			\
		: "b"(addr), "i"(errret), "0"(err))

/* more complex routines */

extern unsigned long __copy_tofrom_user(void __user *to, const void __user *from,
					unsigned long size);

static inline unsigned long
__copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		unsigned long ret;

		switch (n) {
		case 1:
			__get_user_size(*(u8 *)to, from, 1, ret, 1);
			return ret;
		case 2:
			__get_user_size(*(u16 *)to, from, 2, ret, 2);
			return ret;
		case 4:
			__get_user_size(*(u32 *)to, from, 4, ret, 4);
			return ret;
		case 8:
			__get_user_size(*(u64 *)to, from, 8, ret, 8);
			return ret;
		}
	}
	return __copy_tofrom_user((__force void __user *) to, from, n);
}

static inline unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	might_sleep();
	return __copy_from_user_inatomic(to, from, n);
}

static inline unsigned long
__copy_to_user_inatomic(void __user *to, const void *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		unsigned long ret;

		switch (n) {
		case 1:
			__put_user_size(*(u8 *)from, (u8 __user *)to, 1, ret, 1);
			return ret;
		case 2:
			__put_user_size(*(u16 *)from, (u16 __user *)to, 2, ret, 2);
			return ret;
		case 4:
			__put_user_size(*(u32 *)from, (u32 __user *)to, 4, ret, 4);
			return ret;
		case 8:
			__put_user_size(*(u64 *)from, (u64 __user *)to, 8, ret, 8);
			return ret;
		}
	}
	return __copy_tofrom_user(to, (__force const void __user *) from, n);
}

static inline unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_sleep();
	return __copy_to_user_inatomic(to, from, n);
}

#define __copy_in_user(to, from, size) \
	__copy_tofrom_user((to), (from), (size))

extern unsigned long copy_from_user(void *to, const void __user *from,
				    unsigned long n);
extern unsigned long copy_to_user(void __user *to, const void *from,
				  unsigned long n);
extern unsigned long copy_in_user(void __user *to, const void __user *from,
				  unsigned long n);

extern unsigned long __clear_user(void __user *addr, unsigned long size);

static inline unsigned long
clear_user(void __user *addr, unsigned long size)
{
	might_sleep();
	if (likely(access_ok(VERIFY_WRITE, addr, size)))
		size = __clear_user(addr, size);
	return size;
}

extern int __strncpy_from_user(char *dst, const char __user *src, long count);

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	might_sleep();
	if (likely(access_ok(VERIFY_READ, src, 1)))
		return __strncpy_from_user(dst, src, count);
	return -EFAULT;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
extern int __strnlen_user(const char __user *str, long len);

/*
 * Returns the length of the string at str (including the null byte),
 * or 0 if we hit a page we can't access,
 * or something > len if we didn't find a null byte.
 */
static inline int strnlen_user(const char __user *str, long len)
{
	might_sleep();
	if (likely(access_ok(VERIFY_READ, str, 1)))
		return __strnlen_user(str, len);
	return 0;
}

#define strlen_user(str)	strnlen_user((str), 0x7ffffffe)

#endif  /* __ASSEMBLY__ */

#endif	/* _PPC64_UACCESS_H */
