#ifndef _ASM_UACCES_H_
#define _ASM_UACCES_H_
/*
 * User space memory access functions
 */
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/prefetch.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(-1UL)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 *
 * This is equivalent to the following test:
 * (u33)addr + (u33)size >= (u33)current->addr_limit.seg (u65 for x86_64)
 *
 * This needs 33-bit (65-bit for x86_64) arithmetic. We have a carry...
 */

#define __range_not_ok(addr, size)					\
({									\
	unsigned long flag, roksum;					\
	__chk_user_ptr(addr);						\
	asm("add %3,%1 ; sbb %0,%0 ; cmp %1,%4 ; sbb $0,%0"		\
	    : "=&r" (flag), "=r" (roksum)				\
	    : "1" (addr), "g" ((long)(size)),				\
	      "rm" (current_thread_info()->addr_limit.seg));		\
	flag;								\
})

/**
 * access_ok: - Checks if a user space pointer is valid
 * @type: Type of access: %VERIFY_READ or %VERIFY_WRITE.  Note that
 *        %VERIFY_WRITE is a superset of %VERIFY_READ - if it is safe
 *        to write to a block, it is always safe to read from it.
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only.  This function may sleep.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Returns true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 */
#define access_ok(type, addr, size) (likely(__range_not_ok(addr, size) == 0))

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

struct exception_table_entry {
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

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
 */

extern int __get_user_1(void);
extern int __get_user_2(void);
extern int __get_user_4(void);
extern int __get_user_8(void);
extern int __get_user_bad(void);

#define __get_user_x(size, ret, x, ptr)		      \
	asm volatile("call __get_user_" #size	      \
		     : "=a" (ret),"=d" (x)	      \
		     : "0" (ptr))		      \

/* Careful: we have to cast the result to the type of the pointer
 * for sign reasons */

/**
 * get_user: - Get a simple variable from user space.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#ifdef CONFIG_X86_32
#define __get_user_8(__ret_gu, __val_gu, ptr)				\
		__get_user_x(X, __ret_gu, __val_gu, ptr)
#else
#define __get_user_8(__ret_gu, __val_gu, ptr)				\
		__get_user_x(8, __ret_gu, __val_gu, ptr)
#endif

#define get_user(x, ptr)						\
({									\
	int __ret_gu;							\
	unsigned long __val_gu;						\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_x(1, __ret_gu, __val_gu, ptr);		\
		break;							\
	case 2:								\
		__get_user_x(2, __ret_gu, __val_gu, ptr);		\
		break;							\
	case 4:								\
		__get_user_x(4, __ret_gu, __val_gu, ptr);		\
		break;							\
	case 8:								\
		__get_user_8(__ret_gu, __val_gu, ptr);			\
		break;							\
	default:							\
		__get_user_x(X, __ret_gu, __val_gu, ptr);		\
		break;							\
	}								\
	(x) = (__typeof__(*(ptr)))__val_gu;				\
	__ret_gu;							\
})


#ifdef CONFIG_X86_32
# include "uaccess_32.h"
#else
# include "uaccess_64.h"
#endif

#endif
