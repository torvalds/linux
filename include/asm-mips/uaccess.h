/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000, 03, 04 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/thread_info.h>
#include <asm-generic/uaccess.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */
#ifdef CONFIG_32BIT

#define __UA_LIMIT	0x80000000UL

#define __UA_ADDR	".word"
#define __UA_LA		"la"
#define __UA_ADDU	"addu"
#define __UA_t0		"$8"
#define __UA_t1		"$9"

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

#define __UA_LIMIT	(- TASK_SIZE)

#define __UA_ADDR	".dword"
#define __UA_LA		"dla"
#define __UA_ADDU	"daddu"
#define __UA_t0		"$12"
#define __UA_t1		"$13"

#endif /* CONFIG_64BIT */

/*
 * USER_DS is a bitmask that has the bits set that may not be set in a valid
 * userspace address.  Note that we limit 32-bit userspace to 0x7fff8000 but
 * the arithmetic we're doing only works if the limit is a power of two, so
 * we use 0x80000000 here on 32-bit kernels.  If a process passes an invalid
 * address in this range it's the process's problem, not ours :-)
 */

#define KERNEL_DS	((mm_segment_t) { 0UL })
#define USER_DS		((mm_segment_t) { __UA_LIMIT })

#define VERIFY_READ    0
#define VERIFY_WRITE   1

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)


/*
 * Is a address valid? This does a straighforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - AND "addr+size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 *
 * __ua_size() is a trick to avoid runtime checking of positive constant
 * sizes; for those we already know at compile time that the size is ok.
 */
#define __ua_size(size)							\
	((__builtin_constant_p(size) && (signed long) (size) > 0) ? 0 : (size))

/*
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

#define __access_mask get_fs().seg

#define __access_ok(addr, size, mask)					\
	(((signed long)((mask) & ((addr) | ((addr) + (size)) | __ua_size(size)))) == 0)

#define access_ok(type, addr, size)					\
	likely(__access_ok((unsigned long)(addr), (size), __access_mask))

/*
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
#define put_user(x,ptr)	\
	__put_user_check((x), (ptr), sizeof(*(ptr)))

/*
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
#define get_user(x,ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))

/*
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
	__put_user_nocheck((x), (ptr), sizeof(*(ptr)))

/*
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
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#ifdef CONFIG_32BIT
#define __GET_USER_DW(val, ptr) __get_user_asm_ll32(val, ptr)
#endif
#ifdef CONFIG_64BIT
#define __GET_USER_DW(val, ptr) __get_user_asm(val, "ld", ptr)
#endif

extern void __get_user_unknown(void);

#define __get_user_common(val, size, ptr)				\
do {									\
	switch (size) {							\
	case 1: __get_user_asm(val, "lb", ptr); break;			\
	case 2: __get_user_asm(val, "lh", ptr); break;			\
	case 4: __get_user_asm(val, "lw", ptr); break;			\
	case 8: __GET_USER_DW(val, ptr); break;				\
	default: __get_user_unknown(); break;				\
	}								\
} while (0)

#define __get_user_nocheck(x, ptr, size)				\
({									\
	long __gu_err;							\
									\
	__get_user_common((x), size, ptr);				\
	__gu_err;							\
})

#define __get_user_check(x, ptr, size)					\
({									\
	long __gu_err = -EFAULT;					\
	const __typeof__(*(ptr)) __user * __gu_ptr = (ptr);		\
									\
	if (likely(access_ok(VERIFY_READ,  __gu_ptr, size)))		\
		__get_user_common((x), size, __gu_ptr);			\
									\
	__gu_err;							\
})

#define __get_user_asm(val, insn, addr)					\
{									\
	long __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	" insn "	%1, %3				\n"	\
	"2:							\n"	\
	"	.section .fixup,\"ax\"				\n"	\
	"3:	li	%0, %4					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section __ex_table,\"a\"			\n"	\
	"	"__UA_ADDR "\t1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=r" (__gu_tmp)				\
	: "0" (0), "o" (__m(addr)), "i" (-EFAULT));			\
									\
	(val) = (__typeof__(*(addr))) __gu_tmp;				\
}

/*
 * Get a long long 64 using 32 bit registers.
 */
#define __get_user_asm_ll32(val, addr)					\
{									\
	union {								\
		unsigned long long	l;				\
		__typeof__(*(addr))	t;				\
	} __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	lw	%1, (%3)				\n"	\
	"2:	lw	%D1, 4(%3)				\n"	\
	"3:	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	move	%1, $0					\n"	\
	"	move	%D1, $0					\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=&r" (__gu_tmp.l)				\
	: "0" (0), "r" (addr), "i" (-EFAULT));				\
									\
	(val) = __gu_tmp.t;						\
}

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#ifdef CONFIG_32BIT
#define __PUT_USER_DW(ptr) __put_user_asm_ll32(ptr)
#endif
#ifdef CONFIG_64BIT
#define __PUT_USER_DW(ptr) __put_user_asm("sd", ptr)
#endif

#define __put_user_nocheck(x, ptr, size)				\
({									\
	__typeof__(*(ptr)) __pu_val;					\
	long __pu_err = 0;						\
									\
	__pu_val = (x);							\
	switch (size) {							\
	case 1: __put_user_asm("sb", ptr); break;			\
	case 2: __put_user_asm("sh", ptr); break;			\
	case 4: __put_user_asm("sw", ptr); break;			\
	case 8: __PUT_USER_DW(ptr); break;				\
	default: __put_user_unknown(); break;				\
	}								\
	__pu_err;							\
})

#define __put_user_check(x, ptr, size)					\
({									\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (x);				\
	long __pu_err = -EFAULT;					\
									\
	if (likely(access_ok(VERIFY_WRITE,  __pu_addr, size))) {	\
		switch (size) {						\
		case 1: __put_user_asm("sb", __pu_addr); break;		\
		case 2: __put_user_asm("sh", __pu_addr); break;		\
		case 4: __put_user_asm("sw", __pu_addr); break;		\
		case 8: __PUT_USER_DW(__pu_addr); break;		\
		default: __put_user_unknown(); break;			\
		}							\
	}								\
	__pu_err;							\
})

#define __put_user_asm(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	" insn "	%z2, %3		# __put_user_asm\n"	\
	"2:							\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"3:	li	%0, %4					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__pu_err)						\
	: "0" (0), "Jr" (__pu_val), "o" (__m(ptr)),			\
	  "i" (-EFAULT));						\
}

#define __put_user_asm_ll32(ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	sw	%2, (%3)	# __put_user_asm_ll32	\n"	\
	"2:	sw	%D2, 4(%3)				\n"	\
	"3:							\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous"						\
	: "=r" (__pu_err)						\
	: "0" (0), "r" (__pu_val), "r" (ptr),				\
	  "i" (-EFAULT));						\
}

extern void __put_user_unknown(void);

/*
 * We're generating jump to subroutines which will be outside the range of
 * jump instructions
 */
#ifdef MODULE
#define __MODULE_JAL(destination)					\
	".set\tnoat\n\t"						\
	__UA_LA "\t$1, " #destination "\n\t" 				\
	"jalr\t$1\n\t"							\
	".set\tat\n\t"
#else
#define __MODULE_JAL(destination)					\
	"jal\t" #destination "\n\t"
#endif

extern size_t __copy_user(void *__to, const void *__from, size_t __n);

#define __invoke_copy_to_user(to, from, n)				\
({									\
	register void __user *__cu_to_r __asm__("$4");			\
	register const void *__cu_from_r __asm__("$5");			\
	register long __cu_len_r __asm__("$6");				\
									\
	__cu_to_r = (to);						\
	__cu_from_r = (from);						\
	__cu_len_r = (n);						\
	__asm__ __volatile__(						\
	__MODULE_JAL(__copy_user)					\
	: "+r" (__cu_to_r), "+r" (__cu_from_r), "+r" (__cu_len_r)	\
	:								\
	: "$8", "$9", "$10", "$11", "$12", "$15", "$24", "$31",		\
	  "memory");							\
	__cu_len_r;							\
})

/*
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
#define __copy_to_user(to, from, n)					\
({									\
	void __user *__cu_to;						\
	const void *__cu_from;						\
	long __cu_len;							\
									\
	might_sleep();							\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	__cu_len = __invoke_copy_to_user(__cu_to, __cu_from, __cu_len);	\
	__cu_len;							\
})

extern size_t __copy_user_inatomic(void *__to, const void *__from, size_t __n);

#define __copy_to_user_inatomic(to, from, n)				\
({									\
	void __user *__cu_to;						\
	const void *__cu_from;						\
	long __cu_len;							\
									\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	__cu_len = __invoke_copy_to_user(__cu_to, __cu_from, __cu_len);	\
	__cu_len;							\
})

#define __copy_from_user_inatomic(to, from, n)				\
({									\
	void *__cu_to;							\
	const void __user *__cu_from;					\
	long __cu_len;							\
									\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	__cu_len = __invoke_copy_from_user_inatomic(__cu_to, __cu_from,	\
	                                            __cu_len);		\
	__cu_len;							\
})

/*
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
#define copy_to_user(to, from, n)					\
({									\
	void __user *__cu_to;						\
	const void *__cu_from;						\
	long __cu_len;							\
									\
	might_sleep();							\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	if (access_ok(VERIFY_WRITE, __cu_to, __cu_len))			\
		__cu_len = __invoke_copy_to_user(__cu_to, __cu_from,	\
		                                 __cu_len);		\
	__cu_len;							\
})

#define __invoke_copy_from_user(to, from, n)				\
({									\
	register void *__cu_to_r __asm__("$4");				\
	register const void __user *__cu_from_r __asm__("$5");		\
	register long __cu_len_r __asm__("$6");				\
									\
	__cu_to_r = (to);						\
	__cu_from_r = (from);						\
	__cu_len_r = (n);						\
	__asm__ __volatile__(						\
	".set\tnoreorder\n\t"						\
	__MODULE_JAL(__copy_user)					\
	".set\tnoat\n\t"						\
	__UA_ADDU "\t$1, %1, %2\n\t"					\
	".set\tat\n\t"							\
	".set\treorder"							\
	: "+r" (__cu_to_r), "+r" (__cu_from_r), "+r" (__cu_len_r)	\
	:								\
	: "$8", "$9", "$10", "$11", "$12", "$15", "$24", "$31",		\
	  "memory");							\
	__cu_len_r;							\
})

#define __invoke_copy_from_user_inatomic(to, from, n)			\
({									\
	register void *__cu_to_r __asm__("$4");				\
	register const void __user *__cu_from_r __asm__("$5");		\
	register long __cu_len_r __asm__("$6");				\
									\
	__cu_to_r = (to);						\
	__cu_from_r = (from);						\
	__cu_len_r = (n);						\
	__asm__ __volatile__(						\
	".set\tnoreorder\n\t"						\
	__MODULE_JAL(__copy_user_inatomic)				\
	".set\tnoat\n\t"						\
	__UA_ADDU "\t$1, %1, %2\n\t"					\
	".set\tat\n\t"							\
	".set\treorder"							\
	: "+r" (__cu_to_r), "+r" (__cu_from_r), "+r" (__cu_len_r)	\
	:								\
	: "$8", "$9", "$10", "$11", "$12", "$15", "$24", "$31",		\
	  "memory");							\
	__cu_len_r;							\
})

/*
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
#define __copy_from_user(to, from, n)					\
({									\
	void *__cu_to;							\
	const void __user *__cu_from;					\
	long __cu_len;							\
									\
	might_sleep();							\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	__cu_len = __invoke_copy_from_user(__cu_to, __cu_from,		\
	                                   __cu_len);			\
	__cu_len;							\
})

/*
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
#define copy_from_user(to, from, n)					\
({									\
	void *__cu_to;							\
	const void __user *__cu_from;					\
	long __cu_len;							\
									\
	might_sleep();							\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	if (access_ok(VERIFY_READ, __cu_from, __cu_len))		\
		__cu_len = __invoke_copy_from_user(__cu_to, __cu_from,	\
		                                   __cu_len);		\
	__cu_len;							\
})

#define __copy_in_user(to, from, n)	__copy_from_user(to, from, n)

#define copy_in_user(to, from, n)					\
({									\
	void __user *__cu_to;						\
	const void __user *__cu_from;					\
	long __cu_len;							\
									\
	might_sleep();							\
	__cu_to = (to);							\
	__cu_from = (from);						\
	__cu_len = (n);							\
	if (likely(access_ok(VERIFY_READ, __cu_from, __cu_len) &&	\
	           access_ok(VERIFY_WRITE, __cu_to, __cu_len)))		\
		__cu_len = __invoke_copy_from_user(__cu_to, __cu_from,	\
		                                   __cu_len);		\
	__cu_len;							\
})

/*
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
static inline __kernel_size_t
__clear_user(void __user *addr, __kernel_size_t size)
{
	__kernel_size_t res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, $0\n\t"
		"move\t$6, %2\n\t"
		__MODULE_JAL(__bzero)
		"move\t%0, $6"
		: "=r" (res)
		: "r" (addr), "r" (size)
		: "$4", "$5", "$6", __UA_t0, __UA_t1, "$31");

	return res;
}

#define clear_user(addr,n)						\
({									\
	void __user * __cl_addr = (addr);				\
	unsigned long __cl_size = (n);					\
	if (__cl_size && access_ok(VERIFY_WRITE,			\
		((unsigned long)(__cl_addr)), __cl_size))		\
		__cl_size = __clear_user(__cl_addr, __cl_size);		\
	__cl_size;							\
})

/*
 * __strncpy_from_user: - Copy a NUL terminated string from userspace, with less checking.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 * Caller must check the specified block with access_ok() before calling
 * this function.
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
static inline long
__strncpy_from_user(char *__to, const char __user *__from, long __len)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		"move\t$6, %3\n\t"
		__MODULE_JAL(__strncpy_from_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (__to), "r" (__from), "r" (__len)
		: "$2", "$3", "$4", "$5", "$6", __UA_t0, "$31", "memory");

	return res;
}

/*
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
static inline long
strncpy_from_user(char *__to, const char __user *__from, long __len)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		"move\t$6, %3\n\t"
		__MODULE_JAL(__strncpy_from_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (__to), "r" (__from), "r" (__len)
		: "$2", "$3", "$4", "$5", "$6", __UA_t0, "$31", "memory");

	return res;
}

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
static inline long __strlen_user(const char __user *s)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		__MODULE_JAL(__strlen_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s)
		: "$2", "$4", __UA_t0, "$31");

	return res;
}

/*
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
static inline long strlen_user(const char __user *s)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		__MODULE_JAL(__strlen_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s)
		: "$2", "$4", __UA_t0, "$31");

	return res;
}

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
static inline long __strnlen_user(const char __user *s, long n)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		__MODULE_JAL(__strnlen_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s), "r" (n)
		: "$2", "$4", "$5", __UA_t0, "$31");

	return res;
}

/*
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
static inline long strnlen_user(const char __user *s, long n)
{
	long res;

	might_sleep();
	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		__MODULE_JAL(__strnlen_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s), "r" (n)
		: "$2", "$4", "$5", __UA_t0, "$31");

	return res;
}

struct exception_table_entry
{
	unsigned long insn;
	unsigned long nextinsn;
};

extern int fixup_exception(struct pt_regs *regs);

#endif /* _ASM_UACCESS_H */
