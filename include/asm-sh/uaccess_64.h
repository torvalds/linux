#ifndef __ASM_SH_UACCESS_64_H
#define __ASM_SH_UACCESS_64_H

/*
 * include/asm-sh/uaccess_64.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * User space memory access functions
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 *  Based on:
 *     MIPS implementation version 1.15 by
 *              Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *     and i386 version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
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

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(0x80000000)

#define get_ds()	(KERNEL_DS)
#define get_fs()        (current_thread_info()->addr_limit)
#define set_fs(x)       (current_thread_info()->addr_limit=(x))

#define segment_eq(a,b)	((a).seg == (b).seg)

#define __addr_ok(addr) ((unsigned long)(addr) < (current_thread_info()->addr_limit.seg))

/*
 * Uhhuh, this needs 33-bit arithmetic. We have a carry..
 *
 * sum := addr + size;  carry? --> flag = true;
 * if (sum >= addr_limit) flag = true;
 */
#define __range_ok(addr,size) (((unsigned long) (addr) + (size) < (current_thread_info()->addr_limit.seg)) ? 0 : 1)

#define access_ok(type,addr,size) (__range_ok(addr,size) == 0)
#define __access_ok(addr,size) (__range_ok(addr,size) == 0)

/*
 * Uh, these should become the main single-value transfer routines ...
 * They automatically use the right size if we just have the right
 * pointer type ...
 *
 * As MIPS uses the same address space for kernel and user data, we
 * can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	__put_user_check((x),(ptr),sizeof(*(ptr)))
#define get_user(x,ptr) __get_user_check((x),(ptr),sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr) __put_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)))

/*
 * The "xxx_ret" versions return constant specified in third argument, if
 * something bad happens. These macros can be optimized for the
 * case of just returning from the function xxx_ret is used.
 */

#define put_user_ret(x,ptr,ret) ({ \
if (put_user(x,ptr)) return ret; })

#define get_user_ret(x,ptr,ret) ({ \
if (get_user(x,ptr)) return ret; })

#define __put_user_ret(x,ptr,ret) ({ \
if (__put_user(x,ptr)) return ret; })

#define __get_user_ret(x,ptr,ret) ({ \
if (__get_user(x,ptr)) return ret; })

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __get_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		retval = __get_user_asm_b(x, ptr);		\
		break;						\
	case 2:							\
		retval = __get_user_asm_w(x, ptr);		\
		break;						\
	case 4:							\
		retval = __get_user_asm_l(x, ptr);		\
		break;						\
	case 8:							\
		retval = __get_user_asm_q(x, ptr);		\
		break;						\
	default:						\
		__get_user_unknown();				\
		break;						\
	}							\
} while (0)

#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err, __gu_val;				\
	__get_user_size((void *)&__gu_val, (long)(ptr),		\
			(size), __gu_err);			\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)				\
({								\
	long __gu_addr = (long)(ptr);				\
	long __gu_err = -EFAULT, __gu_val;			\
	if (__access_ok(__gu_addr, (size)))			\
		__get_user_size((void *)&__gu_val, __gu_addr,	\
				(size), __gu_err);		\
	(x) = (__typeof__(*(ptr))) __gu_val;			\
	__gu_err;						\
})

extern long __get_user_asm_b(void *, long);
extern long __get_user_asm_w(void *, long);
extern long __get_user_asm_l(void *, long);
extern long __get_user_asm_q(void *, long);
extern void __get_user_unknown(void);

#define __put_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		retval = __put_user_asm_b(x, ptr);		\
		break;						\
	case 2:							\
		retval = __put_user_asm_w(x, ptr);		\
		break;						\
	case 4:							\
		retval = __put_user_asm_l(x, ptr);		\
		break;						\
	case 8:							\
		retval = __put_user_asm_q(x, ptr);		\
		break;						\
	default:						\
		__put_user_unknown();				\
	}							\
} while (0)

#define __put_user_nocheck(x,ptr,size)				\
({								\
	long __pu_err;						\
	__typeof__(*(ptr)) __pu_val = (x);			\
	__put_user_size((void *)&__pu_val, (long)(ptr), (size), __pu_err); \
	__pu_err;						\
})

#define __put_user_check(x,ptr,size)				\
({								\
	long __pu_err = -EFAULT;				\
	long __pu_addr = (long)(ptr);				\
	__typeof__(*(ptr)) __pu_val = (x);			\
								\
	if (__access_ok(__pu_addr, (size)))			\
		__put_user_size((void *)&__pu_val, __pu_addr, (size), __pu_err);\
	__pu_err;						\
})

extern long __put_user_asm_b(void *, long);
extern long __put_user_asm_w(void *, long);
extern long __put_user_asm_l(void *, long);
extern long __put_user_asm_q(void *, long);
extern void __put_user_unknown(void);


/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied */
/* XXX: should be such that: 4byte and the rest. */
extern __kernel_size_t __copy_user(void *__to, const void *__from, __kernel_size_t __n);

#define copy_to_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_to, __copy_size)) { \
__copy_res = __copy_user(__copy_to, (void *) (from), __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

#define copy_to_user_ret(to,from,n,retval) ({ \
if (copy_to_user(to,from,n)) \
	return retval; \
})

#define __copy_to_user(to,from,n)		\
	__copy_user((void *)(to),		\
		    (void *)(from), n)

#define __copy_to_user_ret(to,from,n,retval) ({ \
if (__copy_to_user(to,from,n)) \
	return retval; \
})

#define copy_from_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
void *__copy_from = (void *) (from); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_from, __copy_size)) { \
__copy_res = __copy_user(__copy_to, __copy_from, __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

#define copy_from_user_ret(to,from,n,retval) ({ \
if (copy_from_user(to,from,n)) \
	return retval; \
})

#define __copy_from_user(to,from,n)		\
	__copy_user((void *)(to),		\
		    (void *)(from), n)

#define __copy_from_user_ret(to,from,n,retval) ({ \
if (__copy_from_user(to,from,n)) \
	return retval; \
})

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/* XXX: Not sure it works well..
   should be such that: 4byte clear and the rest. */
extern __kernel_size_t __clear_user(void *addr, __kernel_size_t size);

#define clear_user(addr,n) ({ \
void * __cl_addr = (addr); \
unsigned long __cl_size = (n); \
if (__cl_size && __access_ok(((unsigned long)(__cl_addr)), __cl_size)) \
__cl_size = __clear_user(__cl_addr, __cl_size); \
__cl_size; })

extern int __strncpy_from_user(unsigned long __dest, unsigned long __src, int __count);

#define strncpy_from_user(dest,src,count) ({ \
unsigned long __sfu_src = (unsigned long) (src); \
int __sfu_count = (int) (count); \
long __sfu_res = -EFAULT; \
if(__access_ok(__sfu_src, __sfu_count)) { \
__sfu_res = __strncpy_from_user((unsigned long) (dest), __sfu_src, __sfu_count); \
} __sfu_res; })

#define strlen_user(str) strnlen_user(str, ~0UL >> 1)

/*
 * Return the size of a string (including the ending 0!)
 */
extern long __strnlen_user(const char *__s, long __n);

static inline long strnlen_user(const char *s, long n)
{
	if (!__addr_ok(s))
		return 0;
	else
		return __strnlen_user(s, n);
}

struct exception_table_entry
{
	unsigned long insn, fixup;
};

#define ARCH_HAS_SEARCH_EXTABLE

/* Returns 0 if exception not found and fixup.unit otherwise.  */
extern unsigned long search_exception_table(unsigned long addr);
extern const struct exception_table_entry *search_exception_tables (unsigned long addr);

#endif /* __ASM_SH_UACCESS_64_H */
