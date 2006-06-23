#ifndef _ASM_M32R_UACCESS_H
#define _ASM_M32R_UACCESS_H

/*
 *  linux/include/asm-m32r/uaccess.h
 *
 *  M32R version.
 *    Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */

/*
 * User space memory access functions
 */
#include <linux/errno.h>
#include <linux/thread_info.h>
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

#ifdef CONFIG_MMU

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)
#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#else /* not CONFIG_MMU */

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(0xFFFFFFFF)
#define get_ds()	(KERNEL_DS)

static inline mm_segment_t get_fs(void)
{
	return USER_DS;
}

static inline void set_fs(mm_segment_t s)
{
}

#endif /* not CONFIG_MMU */

#define segment_eq(a,b)	((a).seg == (b).seg)

#define __addr_ok(addr) \
	((unsigned long)(addr) < (current_thread_info()->addr_limit.seg))

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 *
 * This is equivalent to the following test:
 * (u33)addr + (u33)size >= (u33)current->addr_limit.seg
 *
 * This needs 33-bit arithmetic. We have a carry...
 */
#define __range_ok(addr,size) ({					\
	unsigned long flag, sum; 					\
	__chk_user_ptr(addr);						\
	asm ( 								\
		"	cmpu	%1, %1    ; clear cbit\n"		\
		"	addx	%1, %3    ; set cbit if overflow\n"	\
		"	subx	%0, %0\n"				\
		"	cmpu	%4, %1\n"				\
		"	subx	%0, %5\n"				\
		: "=&r" (flag), "=r" (sum)				\
		: "1" (addr), "r" ((int)(size)), 			\
		  "r" (current_thread_info()->addr_limit.seg), "r" (0)	\
		: "cbit" );						\
	flag; })

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
#ifdef CONFIG_MMU
#define access_ok(type,addr,size) (likely(__range_ok(addr,size) == 0))
#else
static inline int access_ok(int type, const void *addr, unsigned long size)
{
	extern unsigned long memory_start, memory_end;
	unsigned long val = (unsigned long)addr;

	return ((val >= memory_start) && ((val + size) < memory_end));
}
#endif /* CONFIG_MMU */

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

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */

/* Careful: we have to cast the result to the type of the pointer for sign
   reasons */
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
#define get_user(x,ptr)							\
	__get_user_check((x),(ptr),sizeof(*(ptr)))

/**
 * put_user: - Write a simple value into user space.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define put_user(x,ptr)							\
	__put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

/**
 * __get_user: - Get a simple variable from user space, with less checking.
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
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define __get_user(x,ptr) \
	__get_user_nocheck((x),(ptr),sizeof(*(ptr)))

#define __get_user_nocheck(x,ptr,size)					\
({									\
	long __gu_err = 0;						\
	unsigned long __gu_val;						\
	might_sleep();							\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);		\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

#define __get_user_check(x,ptr,size)					\
({									\
	long __gu_err = -EFAULT;					\
	unsigned long __gu_val = 0;					\
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	might_sleep();							\
	if (access_ok(VERIFY_READ,__gu_addr,size))			\
		__get_user_size(__gu_val,__gu_addr,(size),__gu_err);	\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __get_user_asm(x,ptr,retval,"ub"); break;		\
	  case 2: __get_user_asm(x,ptr,retval,"uh"); break;		\
	  case 4: __get_user_asm(x,ptr,retval,""); break;		\
	  default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype)				\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	ld"itype" %1,@%2\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	ldi %0,%3\n"					\
		"	seth r14,#high(2b)\n"				\
		"	or3 r14,r14,#low(2b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,3b\n"					\
		".previous"						\
		: "=&r" (err), "=&r" (x)				\
		: "r" (addr), "i" (-EFAULT), "0" (err)			\
		: "r14", "memory")

/**
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only.  This function may sleep.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define __put_user(x,ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))


#define __put_user_nocheck(x,ptr,size)					\
({									\
	long __pu_err;							\
	might_sleep();							\
	__put_user_size((x),(ptr),(size),__pu_err);			\
	__pu_err;							\
})


#define __put_user_check(x,ptr,size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	might_sleep();							\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))			\
		__put_user_size((x),__pu_addr,(size),__pu_err);		\
	__pu_err;							\
})

#if defined(__LITTLE_ENDIAN__)
#define __put_user_u64(x, addr, err)					\
        __asm__ __volatile__(						\
                "       .fillinsn\n"					\
                "1:     st %L1,@%2\n"					\
                "       .fillinsn\n"					\
                "2:     st %H1,@(4,%2)\n"				\
                "       .fillinsn\n"					\
                "3:\n"							\
                ".section .fixup,\"ax\"\n"				\
                "       .balign 4\n"					\
                "4:     ldi %0,%3\n"					\
                "       seth r14,#high(3b)\n"				\
                "       or3 r14,r14,#low(3b)\n"				\
                "       jmp r14\n"					\
                ".previous\n"						\
                ".section __ex_table,\"a\"\n"				\
                "       .balign 4\n"					\
                "       .long 1b,4b\n"					\
                "       .long 2b,4b\n"					\
                ".previous"						\
                : "=&r" (err)						\
                : "r" (x), "r" (addr), "i" (-EFAULT), "0" (err)		\
                : "r14", "memory")

#elif defined(__BIG_ENDIAN__)
#define __put_user_u64(x, addr, err)					\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	st %H1,@%2\n"					\
		"	.fillinsn\n"					\
		"2:	st %L1,@(4,%2)\n"				\
		"	.fillinsn\n"					\
		"3:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"4:	ldi %0,%3\n"					\
		"	seth r14,#high(3b)\n"				\
		"	or3 r14,r14,#low(3b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,4b\n"					\
		"	.long 2b,4b\n"					\
		".previous"						\
		: "=&r" (err)						\
		: "r" (x), "r" (addr), "i" (-EFAULT), "0" (err)		\
		: "r14", "memory")
#else
#error no endian defined
#endif

extern void __put_user_bad(void);

#define __put_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __put_user_asm(x,ptr,retval,"b"); break;		\
	  case 2: __put_user_asm(x,ptr,retval,"h"); break;		\
	  case 4: __put_user_asm(x,ptr,retval,""); break;		\
	  case 8: __put_user_u64((__typeof__(*ptr))(x),ptr,retval); break;\
	  default: __put_user_bad();					\
	}								\
} while (0)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_asm(x, addr, err, itype)				\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	st"itype" %1,@%2\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	ldi %0,%3\n"					\
		"	seth r14,#high(2b)\n"				\
		"	or3 r14,r14,#low(2b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,3b\n"					\
		".previous"						\
		: "=&r" (err)						\
		: "r" (x), "r" (addr), "i" (-EFAULT), "0" (err)		\
		: "r14", "memory")

/*
 * Here we special-case 1, 2 and 4-byte copy_*_user invocations.  On a fault
 * we return the initial request size (1, 2 or 4), as copy_*_user should do.
 * If a store crosses a page boundary and gets a fault, the m32r will not write
 * anything, so this is accurate.
 */

/*
 * Copy To/From Userspace
 */

/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied.  */
#define __copy_user(to,from,size)					\
do {									\
	unsigned long __dst, __src, __c;				\
	__asm__ __volatile__ (						\
		"	mv	r14, %0\n"				\
		"	or	r14, %1\n"				\
		"	beq	%0, %1, 9f\n"				\
		"	beqz	%2, 9f\n"				\
		"	and3	r14, r14, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%2, %2, #3\n"				\
		"	beqz	%3, 2f\n"				\
		"	addi	%0, #-4		; word_copy \n"		\
		"	.fillinsn\n"					\
		"0:	ld	r14, @%1+\n"				\
		"	addi	%3, #-1\n"				\
		"	.fillinsn\n"					\
		"1:	st	r14, @+%0\n"				\
		"	bnez	%3, 0b\n"				\
		"	beqz	%2, 9f\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"2:	ldb	r14, @%1	; byte_copy \n"		\
		"	.fillinsn\n"					\
		"3:	stb	r14, @%0\n"				\
		"	addi	%1, #1\n"				\
		"	addi	%2, #-1\n"				\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"5:	addi	%3, #1\n"				\
		"	addi	%1, #-4\n"				\
		"	.fillinsn\n"					\
		"6:	slli	%3, #2\n"				\
		"	add	%2, %3\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"7:	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,6b\n"					\
		"	.long 1b,5b\n"					\
		"	.long 2b,9b\n"					\
		"	.long 3b,9b\n"					\
		".previous\n"						\
		: "=&r" (__dst), "=&r" (__src), "=&r" (size),		\
		  "=&r" (__c)						\
		: "0" (to), "1" (from), "2" (size), "3" (size / 4)	\
		: "r14", "memory");					\
} while (0)

#define __copy_user_zeroing(to,from,size)				\
do {									\
	unsigned long __dst, __src, __c;				\
	__asm__ __volatile__ (						\
		"	mv	r14, %0\n"				\
		"	or	r14, %1\n"				\
		"	beq	%0, %1, 9f\n"				\
		"	beqz	%2, 9f\n"				\
		"	and3	r14, r14, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%2, %2, #3\n"				\
		"	beqz	%3, 2f\n"				\
		"	addi	%0, #-4		; word_copy \n"		\
		"	.fillinsn\n"					\
		"0:	ld	r14, @%1+\n"				\
		"	addi	%3, #-1\n"				\
		"	.fillinsn\n"					\
		"1:	st	r14, @+%0\n"				\
		"	bnez	%3, 0b\n"				\
		"	beqz	%2, 9f\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"2:	ldb	r14, @%1	; byte_copy \n"		\
		"	.fillinsn\n"					\
		"3:	stb	r14, @%0\n"				\
		"	addi	%1, #1\n"				\
		"	addi	%2, #-1\n"				\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"5:	addi	%3, #1\n"				\
		"	addi	%1, #-4\n"				\
		"	.fillinsn\n"					\
		"6:	slli	%3, #2\n"				\
		"	add	%2, %3\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"7:	ldi	r14, #0		; store zero \n"	\
		"	.fillinsn\n"					\
		"8:	addi	%2, #-1\n"				\
		"	stb	r14, @%0	; ACE? \n"		\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 8b\n"				\
		"	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,6b\n"					\
		"	.long 1b,5b\n"					\
		"	.long 2b,7b\n"					\
		"	.long 3b,7b\n"					\
		".previous\n"						\
		: "=&r" (__dst), "=&r" (__src), "=&r" (size),		\
		  "=&r" (__c)						\
		: "0" (to), "1" (from), "2" (size), "3" (size / 4)	\
		: "r14", "memory");					\
} while (0)


/* We let the __ versions of copy_from/to_user inline, because they're often
 * used in fast paths and have only a small space overhead.
 */
static inline unsigned long __generic_copy_from_user_nocheck(void *to,
	const void __user *from, unsigned long n)
{
	__copy_user_zeroing(to,from,n);
	return n;
}

static inline unsigned long __generic_copy_to_user_nocheck(void __user *to,
	const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}

unsigned long __generic_copy_to_user(void __user *, const void *, unsigned long);
unsigned long __generic_copy_from_user(void *, const void __user *, unsigned long);

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
#define __copy_to_user(to,from,n)			\
	__generic_copy_to_user_nocheck((to),(from),(n))

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
#define copy_to_user(to,from,n)				\
({							\
	might_sleep();					\
	__generic_copy_to_user((to),(from),(n));	\
})

/**
 * __copy_from_user: - Copy a block of data from user space, with less checking. * @to:   Destination address, in kernel space.
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
#define __copy_from_user(to,from,n)			\
	__generic_copy_from_user_nocheck((to),(from),(n))

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
#define copy_from_user(to,from,n)			\
({							\
	might_sleep();					\
	__generic_copy_from_user((to),(from),(n));	\
})

long __must_check strncpy_from_user(char *dst, const char __user *src,
				long count);
long __must_check __strncpy_from_user(char *dst,
				const char __user *src, long count);

/**
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long __clear_user(void __user *mem, unsigned long len);

/**
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long clear_user(void __user *mem, unsigned long len);

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
#define strlen_user(str) strnlen_user(str, ~0UL >> 1)
long strnlen_user(const char __user *str, long n);

#endif /* _ASM_M32R_UACCESS_H */
