/*
 * include/asm-xtensa/uaccess.h
 *
 * User space memory access functions
 *
 * These routines provide basic accessing functions to the user memory
 * space for the kernel. This header file provides fuctions such as:
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_UACCESS_H
#define _XTENSA_UACCESS_H

#include <linux/errno.h>

#define VERIFY_READ    0
#define VERIFY_WRITE   1

#ifdef __ASSEMBLY__

#define _ASMLANGUAGE
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>

/*
 * These assembly macros mirror the C macros that follow below.  They
 * should always have identical functionality.  See
 * arch/xtensa/kernel/sys.S for usage.
 */

#define KERNEL_DS	0
#define USER_DS		1

#define get_ds		(KERNEL_DS)

/*
 * get_fs reads current->thread.current_ds into a register.
 * On Entry:
 * 	<ad>	anything
 * 	<sp>	stack
 * On Exit:
 * 	<ad>	contains current->thread.current_ds
 */
	.macro	get_fs	ad, sp
	GET_CURRENT(\ad,\sp)
	l32i	\ad, \ad, THREAD_CURRENT_DS
	.endm

/*
 * set_fs sets current->thread.current_ds to some value.
 * On Entry:
 *	<at>	anything (temp register)
 *	<av>	value to write
 *	<sp>	stack
 * On Exit:
 *	<at>	destroyed (actually, current)
 *	<av>	preserved, value to write
 */
	.macro	set_fs	at, av, sp
	GET_CURRENT(\at,\sp)
	s32i	\av, \at, THREAD_CURRENT_DS
	.endm

/*
 * kernel_ok determines whether we should bypass addr/size checking.
 * See the equivalent C-macro version below for clarity.
 * On success, kernel_ok branches to a label indicated by parameter
 * <success>.  This implies that the macro falls through to the next
 * insruction on an error.
 *
 * Note that while this macro can be used independently, we designed
 * in for optimal use in the access_ok macro below (i.e., we fall
 * through on error).
 *
 * On Entry:
 * 	<at>		anything (temp register)
 * 	<success>	label to branch to on success; implies
 * 			fall-through macro on error
 * 	<sp>		stack pointer
 * On Exit:
 * 	<at>		destroyed (actually, current->thread.current_ds)
 */

#if ((KERNEL_DS != 0) || (USER_DS == 0))
# error Assembly macro kernel_ok fails
#endif
	.macro	kernel_ok  at, sp, success
	get_fs	\at, \sp
	beqz	\at, \success
	.endm

/*
 * user_ok determines whether the access to user-space memory is allowed.
 * See the equivalent C-macro version below for clarity.
 *
 * On error, user_ok branches to a label indicated by parameter
 * <error>.  This implies that the macro falls through to the next
 * instruction on success.
 *
 * Note that while this macro can be used independently, we designed
 * in for optimal use in the access_ok macro below (i.e., we fall
 * through on success).
 *
 * On Entry:
 * 	<aa>	register containing memory address
 * 	<as>	register containing memory size
 * 	<at>	temp register
 * 	<error>	label to branch to on error; implies fall-through
 * 		macro on success
 * On Exit:
 * 	<aa>	preserved
 * 	<as>	preserved
 * 	<at>	destroyed (actually, (TASK_SIZE + 1 - size))
 */
	.macro	user_ok	aa, as, at, error
	movi	\at, (TASK_SIZE+1)
	bgeu	\as, \at, \error
	sub	\at, \at, \as
	bgeu	\aa, \at, \error
	.endm

/*
 * access_ok determines whether a memory access is allowed.  See the
 * equivalent C-macro version below for clarity.
 *
 * On error, access_ok branches to a label indicated by parameter
 * <error>.  This implies that the macro falls through to the next
 * instruction on success.
 *
 * Note that we assume success is the common case, and we optimize the
 * branch fall-through case on success.
 *
 * On Entry:
 * 	<aa>	register containing memory address
 * 	<as>	register containing memory size
 * 	<at>	temp register
 * 	<sp>
 * 	<error>	label to branch to on error; implies fall-through
 * 		macro on success
 * On Exit:
 * 	<aa>	preserved
 * 	<as>	preserved
 * 	<at>	destroyed
 */
	.macro	access_ok  aa, as, at, sp, error
	kernel_ok  \at, \sp, .Laccess_ok_\@
	user_ok    \aa, \as, \at, \error
.Laccess_ok_\@:
	.endm

#else /* __ASSEMBLY__ not defined */

#include <linux/sched.h>
#include <asm/types.h>

/*
 * The fs value determines whether argument validity checking should
 * be performed or not.  If get_fs() == USER_DS, checking is
 * performed, with get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons (Data Segment Register?), these macros are
 * grossly misnamed.
 */

#define KERNEL_DS	((mm_segment_t) { 0 })
#define USER_DS		((mm_segment_t) { 1 })

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->thread.current_ds)
#define set_fs(val)	(current->thread.current_ds = (val))

#define segment_eq(a,b)	((a).seg == (b).seg)

#define __kernel_ok (segment_eq(get_fs(), KERNEL_DS))
#define __user_ok(addr,size) (((size) <= TASK_SIZE)&&((addr) <= TASK_SIZE-(size)))
#define __access_ok(addr,size) (__kernel_ok || __user_ok((addr),(size)))
#define access_ok(type,addr,size) __access_ok((unsigned long)(addr),(size))

/*
 * These are the main single-value transfer routines.  They
 * automatically use the right size if we just have the right pointer
 * type.
 *
 * This gets kind of ugly. We want to return _two_ values in
 * "get_user()" and yet we don't want to do any pointers, because that
 * is too much of a performance impact. Thus we have a few rather ugly
 * macros here, and hide all the uglyness from the user.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	__put_user_check((x),(ptr),sizeof(*(ptr)))
#define get_user(x,ptr) __get_user_check((x),(ptr),sizeof(*(ptr)))

/*
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */
#define __put_user(x,ptr) __put_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)))


extern long __put_user_bad(void);

#define __put_user_nocheck(x,ptr,size)			\
({							\
	long __pu_err;					\
	__put_user_size((x),(ptr),(size),__pu_err);	\
	__pu_err;					\
})

#define __put_user_check(x,ptr,size)				\
({								\
	long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) *__pu_addr = (ptr);			\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))		\
		__put_user_size((x),__pu_addr,(size),__pu_err);	\
	__pu_err;						\
})

#define __put_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
        case 1: __put_user_asm(x,ptr,retval,1,"s8i");  break;	\
        case 2: __put_user_asm(x,ptr,retval,2,"s16i"); break;   \
        case 4: __put_user_asm(x,ptr,retval,4,"s32i"); break;   \
        case 8: {						\
		     __typeof__(*ptr) __v64 = x;		\
		     retval = __copy_to_user(ptr,&__v64,8);	\
		     break;					\
	        }						\
	default: __put_user_bad();				\
	}							\
} while (0)


/*
 * Consider a case of a user single load/store would cause both an
 * unaligned exception and an MMU-related exception (unaligned
 * exceptions happen first):
 *
 * User code passes a bad variable ptr to a system call.
 * Kernel tries to access the variable.
 * Unaligned exception occurs.
 * Unaligned exception handler tries to make aligned accesses.
 * Double exception occurs for MMU-related cause (e.g., page not mapped).
 * do_page_fault() thinks the fault address belongs to the kernel, not the
 * user, and panics.
 *
 * The kernel currently prohibits user unaligned accesses.  We use the
 * __check_align_* macros to check for unaligned addresses before
 * accessing user space so we don't crash the kernel.  Both
 * __put_user_asm and __get_user_asm use these alignment macros, so
 * macro-specific labels such as 0f, 1f, %0, %2, and %3 must stay in
 * sync.
 */

#define __check_align_1  ""

#define __check_align_2				\
	"   _bbci.l %2,  0, 1f		\n"	\
	"   movi    %0, %3		\n"	\
	"   _j      2f			\n"

#define __check_align_4				\
	"   _bbsi.l %2,  0, 0f		\n"	\
	"   _bbci.l %2,  1, 1f		\n"	\
	"0: movi    %0, %3		\n"	\
	"   _j      2f			\n"


/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 *
 * WARNING: If you modify this macro at all, verify that the
 * __check_align_* macros still work.
 */
#define __put_user_asm(x, addr, err, align, insn) \
   __asm__ __volatile__(			\
	__check_align_##align			\
	"1: "insn"  %1, %2, 0		\n"	\
	"2:				\n"	\
	"   .section  .fixup,\"ax\"	\n"	\
	"   .align 4			\n"	\
	"4:				\n"	\
	"   .long  2b			\n"	\
	"5:				\n"	\
	"   l32r   %2, 4b		\n"	\
        "   movi   %0, %3		\n"	\
        "   jx     %2			\n"	\
	"   .previous			\n"	\
	"   .section  __ex_table,\"a\"	\n"	\
	"   .long	1b, 5b		\n"	\
	"   .previous"				\
	:"=r" (err)				\
	:"r" ((int)(x)), "r" (addr), "i" (-EFAULT), "0" (err))

#define __get_user_nocheck(x,ptr,size)				\
({								\
	long __gu_err, __gu_val;				\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT, __gu_val = 0;				\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	if (access_ok(VERIFY_READ,__gu_addr,size))			\
		__get_user_size(__gu_val,__gu_addr,(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
        switch (size) {							\
          case 1: __get_user_asm(x,ptr,retval,1,"l8ui");  break;	\
          case 2: __get_user_asm(x,ptr,retval,2,"l16ui"); break;	\
          case 4: __get_user_asm(x,ptr,retval,4,"l32i");  break;	\
          case 8: retval = __copy_from_user(&x,ptr,8);    break;	\
          default: (x) = __get_user_bad();				\
        }								\
} while (0)


/*
 * WARNING: If you modify this macro at all, verify that the
 * __check_align_* macros still work.
 */
#define __get_user_asm(x, addr, err, align, insn) \
   __asm__ __volatile__(			\
	__check_align_##align			\
	"1: "insn"  %1, %2, 0		\n"	\
	"2:				\n"	\
	"   .section  .fixup,\"ax\"	\n"	\
	"   .align 4			\n"	\
	"4:				\n"	\
	"   .long  2b			\n"	\
	"5:				\n"	\
	"   l32r   %2, 4b		\n"	\
	"   movi   %1, 0		\n"	\
        "   movi   %0, %3		\n"	\
        "   jx     %2			\n"	\
	"   .previous			\n"	\
	"   .section  __ex_table,\"a\"	\n"	\
	"   .long	1b, 5b		\n"	\
	"   .previous"				\
	:"=r" (err), "=r" (x)			\
	:"r" (addr), "i" (-EFAULT), "0" (err))


/*
 * Copy to/from user space
 */

/*
 * We use a generic, arbitrary-sized copy subroutine.  The Xtensa
 * architecture would cause heavy code bloat if we tried to inline
 * these functions and provide __constant_copy_* equivalents like the
 * i386 versions.  __xtensa_copy_user is quite efficient.  See the
 * .fixup section of __xtensa_copy_user for a discussion on the
 * X_zeroing equivalents for Xtensa.
 */

extern unsigned __xtensa_copy_user(void *to, const void *from, unsigned n);
#define __copy_user(to,from,size) __xtensa_copy_user(to,from,size)


static inline unsigned long
__generic_copy_from_user_nocheck(void *to, const void *from, unsigned long n)
{
	return __copy_user(to,from,n);
}

static inline unsigned long
__generic_copy_to_user_nocheck(void *to, const void *from, unsigned long n)
{
	return __copy_user(to,from,n);
}

static inline unsigned long
__generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	prefetch(from);
	if (access_ok(VERIFY_WRITE, to, n))
		return __copy_user(to,from,n);
	return n;
}

static inline unsigned long
__generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	prefetchw(to);
	if (access_ok(VERIFY_READ, from, n))
		return __copy_user(to,from,n);
	else
		memset(to, 0, n);
	return n;
}

#define copy_to_user(to,from,n) __generic_copy_to_user((to),(from),(n))
#define copy_from_user(to,from,n) __generic_copy_from_user((to),(from),(n))
#define __copy_to_user(to,from,n) __generic_copy_to_user_nocheck((to),(from),(n))
#define __copy_from_user(to,from,n) __generic_copy_from_user_nocheck((to),(from),(n))
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user


/*
 * We need to return the number of bytes not cleared.  Our memset()
 * returns zero if a problem occurs while accessing user-space memory.
 * In that event, return no memory cleared.  Otherwise, zero for
 * success.
 */

static inline unsigned long
__xtensa_clear_user(void *addr, unsigned long size)
{
	if ( ! memset(addr, 0, size) )
		return size;
	return 0;
}

static inline unsigned long
clear_user(void *addr, unsigned long size)
{
	if (access_ok(VERIFY_WRITE, addr, size))
		return __xtensa_clear_user(addr, size);
	return size ? -EFAULT : 0;
}

#define __clear_user  __xtensa_clear_user


extern long __strncpy_user(char *, const char *, long);
#define __strncpy_from_user __strncpy_user

static inline long
strncpy_from_user(char *dst, const char *src, long count)
{
	if (access_ok(VERIFY_READ, src, 1))
		return __strncpy_from_user(dst, src, count);
	return -EFAULT;
}


#define strlen_user(str) strnlen_user((str), TASK_SIZE - 1)

/*
 * Return the size of a string (including the ending 0!)
 */
extern long __strnlen_user(const char *, long);

static inline long strnlen_user(const char *str, long len)
{
	unsigned long top = __kernel_ok ? ~0UL : TASK_SIZE - 1;

	if ((unsigned long)str > top)
		return 0;
	return __strnlen_user(str, len);
}


struct exception_table_entry
{
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup.unit otherwise.  */

extern unsigned long search_exception_table(unsigned long addr);
extern void sort_exception_table(void);

/* Returns the new pc */
#define fixup_exception(map_reg, fixup_unit, pc)                \
({                                                              \
	fixup_unit;                                             \
})

#endif	/* __ASSEMBLY__ */
#endif	/* _XTENSA_UACCESS_H */
