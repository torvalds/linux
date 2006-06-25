#ifndef __M68K_UACCESS_H
#define __M68K_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <asm/segment.h>

#define VERIFY_READ	0
#define VERIFY_WRITE	1

/* We let the MMU do all checking */
#define access_ok(type,addr,size) 1

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

extern int __put_user_bad(void);
extern int __get_user_bad(void);

#define __put_user_asm(res, x, ptr, bwl, reg, err)	\
asm volatile ("\n"					\
	"1:	moves."#bwl"	%2,%1\n"		\
	"2:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.even\n"				\
	"10:	moveq.l	%3,%0\n"			\
	"	jra 2b\n"				\
	"	.previous\n"				\
	"\n"						\
	"	.section __ex_table,\"a\"\n"		\
	"	.align	4\n"				\
	"	.long	1b,10b\n"			\
	"	.long	2b,10b\n"			\
	"	.previous"				\
	: "+d" (res), "=m" (*(ptr))			\
	: #reg (x), "i" (err))

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define __put_user(x, ptr)						\
({									\
	typeof(*(ptr)) __pu_val = (x);					\
	int __pu_err = 0;						\
	__chk_user_ptr(ptr);						\
	switch (sizeof (*(ptr))) {					\
	case 1:								\
		__put_user_asm(__pu_err, __pu_val, ptr, b, d, -EFAULT);	\
		break;							\
	case 2:								\
		__put_user_asm(__pu_err, __pu_val, ptr, w, d, -EFAULT);	\
		break;							\
	case 4:								\
		__put_user_asm(__pu_err, __pu_val, ptr, l, r, -EFAULT);	\
		break;							\
	case 8:								\
 	    {								\
 		const void *__pu_ptr = (ptr);				\
		asm volatile ("\n"					\
			"1:	moves.l	%2,(%1)+\n"			\
			"2:	moves.l	%R2,(%1)\n"			\
			"3:\n"						\
			"	.section .fixup,\"ax\"\n"		\
			"	.even\n"				\
			"10:	movel %3,%0\n"				\
			"	jra 3b\n"				\
			"	.previous\n"				\
			"\n"						\
			"	.section __ex_table,\"a\"\n"		\
			"	.align 4\n"				\
			"	.long 1b,10b\n"				\
			"	.long 2b,10b\n"				\
			"	.long 3b,10b\n"				\
			"	.previous"				\
			: "+d" (__pu_err), "+a" (__pu_ptr)		\
			: "r" (__pu_val), "i" (-EFAULT)			\
			: "memory");					\
		break;							\
	    }								\
	default:							\
		__pu_err = __put_user_bad();				\
		break;							\
	}								\
	__pu_err;							\
})
#define put_user(x, ptr)	__put_user(x, ptr)


#define __get_user_asm(res, x, ptr, type, bwl, reg, err) ({	\
	type __gu_val;						\
	asm volatile ("\n"					\
		"1:	moves."#bwl"	%2,%1\n"		\
		"2:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.even\n"				\
		"10:	move.l	%3,%0\n"			\
		"	sub."#bwl"	%1,%1\n"		\
		"	jra	2b\n"				\
		"	.previous\n"				\
		"\n"						\
		"	.section __ex_table,\"a\"\n"		\
		"	.align	4\n"				\
		"	.long	1b,10b\n"			\
		"	.previous"				\
		: "+d" (res), "=&" #reg (__gu_val)		\
		: "m" (*(ptr)), "i" (err));			\
	(x) = (typeof(*(ptr)))(long)__gu_val;			\
})

#define __get_user(x, ptr)						\
({									\
	int __gu_err = 0;						\
	__chk_user_ptr(ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__get_user_asm(__gu_err, x, ptr, u8, b, d, -EFAULT);	\
		break;							\
	case 2:								\
		__get_user_asm(__gu_err, x, ptr, u16, w, d, -EFAULT);	\
		break;							\
	case 4:								\
		__get_user_asm(__gu_err, x, ptr, u32, l, r, -EFAULT);	\
		break;							\
/*	case 8:	disabled because gcc-4.1 has a broken typeof		\
 	    {								\
 		const void *__gu_ptr = (ptr);				\
 		u64 __gu_val;						\
		asm volatile ("\n"					\
			"1:	moves.l	(%2)+,%1\n"			\
			"2:	moves.l	(%2),%R1\n"			\
			"3:\n"						\
			"	.section .fixup,\"ax\"\n"		\
			"	.even\n"				\
			"10:	move.l	%3,%0\n"			\
			"	sub.l	%1,%1\n"			\
			"	sub.l	%R1,%R1\n"			\
			"	jra	3b\n"				\
			"	.previous\n"				\
			"\n"						\
			"	.section __ex_table,\"a\"\n"		\
			"	.align	4\n"				\
			"	.long	1b,10b\n"			\
			"	.long	2b,10b\n"			\
			"	.previous"				\
			: "+d" (__gu_err), "=&r" (__gu_val),		\
			  "+a" (__gu_ptr)				\
			: "i" (-EFAULT)					\
			: "memory");					\
		(x) = (typeof(*(ptr)))__gu_val;				\
		break;							\
	    }	*/							\
	default:							\
		__gu_err = __get_user_bad();				\
		break;							\
	}								\
	__gu_err;							\
})
#define get_user(x, ptr) __get_user(x, ptr)

unsigned long __generic_copy_from_user(void *to, const void __user *from, unsigned long n);
unsigned long __generic_copy_to_user(void __user *to, const void *from, unsigned long n);

static __always_inline unsigned long
__constant_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = 0, tmp;

	/* limit the inlined version to 3 moves */
	if (n == 11 || n > 12)
		return __generic_copy_from_user(to, from, n);

	switch (n) {
	case 1:
		__get_user_asm(res, *(u8 *)to, (u8 *)from, u8, b, d, 1);
		return res;
	case 2:
		__get_user_asm(res, *(u16 *)to, (u16 *)from, u16, w, d, 2);
		return res;
	case 4:
		__get_user_asm(res, *(u32 *)to, (u32 *)from, u32, l, r, 4);
		return res;
	}

	asm volatile ("\n"
		"	.ifndef	.Lfrom_user\n"
		"	.set	.Lfrom_user,1\n"
		"	.macro	copy_from_user to,from,tmp\n"
		"	.if	.Lcnt >= 4\n"
		"1:	moves.l	(\\from)+,\\tmp\n"
		"	move.l	\\tmp,(\\to)+\n"
		"	.set	.Lcnt,.Lcnt-4\n"
		"	.elseif	.Lcnt & 2\n"
		"1:	moves.w	(\\from)+,\\tmp\n"
		"	move.w	\\tmp,(\\to)+\n"
		"	.set	.Lcnt,.Lcnt-2\n"
		"	.elseif	.Lcnt & 1\n"
		"1:	moves.b	(\\from)+,\\tmp\n"
		"	move.b	\\tmp,(\\to)+\n"
		"	.set	.Lcnt,.Lcnt-1\n"
		"	.else\n"
		"	.exitm\n"
		"	.endif\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	1b,3f\n"
		"	.previous\n"
		"	.endm\n"
		"	.endif\n"
		"\n"
		"	.set	.Lcnt,%c4\n"
		"	copy_from_user %1,%2,%3\n"
		"	copy_from_user %1,%2,%3\n"
		"	copy_from_user %1,%2,%3\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"3:	moveq.l	%4,%0\n"
		"	move.l	%5,%1\n"
		"	.rept	%c4 / 4\n"
		"	clr.l	(%1)+\n"
		"	.endr\n"
		"	.if	%c4 & 2\n"
		"	clr.w	(%1)+\n"
		"	.endif\n"
		"	.if	%c4 & 1\n"
		"	clr.b	(%1)+\n"
		"	.endif\n"
		"	jra	2b\n"
		"	.previous\n"
		: "+r" (res), "+a" (to), "+a" (from), "=&d" (tmp)
		: "i" (n), "g" (to)
		: "memory");

	return res;
}

static __always_inline unsigned long
__constant_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	unsigned long res = 0, tmp;

	/* limit the inlined version to 3 moves */
	if (n == 11 || n > 12)
		return __generic_copy_to_user(to, from, n);

	switch (n) {
	case 1:
		__put_user_asm(res, *(u8 *)from, (u8 *)to, b, d, 1);
		return res;
	case 2:
		__put_user_asm(res, *(u16 *)from, (u16 *)to, w, d, 2);
		return res;
	case 4:
		__put_user_asm(res, *(u32 *)from, (u32 *)to, l, r, 4);
		return res;
	}

	asm volatile ("\n"
		"	.ifndef	.Lto_user\n"
		"	.set	.Lto_user,1\n"
		"	.macro	copy_to_user to,from,tmp\n"
		"	.if	.Lcnt >= 4\n"
		"	move.l	(\\from)+,\\tmp\n"
		"11:	moves.l	\\tmp,(\\to)+\n"
		"12:	.set	.Lcnt,.Lcnt-4\n"
		"	.elseif	.Lcnt & 2\n"
		"	move.w	(\\from)+,\\tmp\n"
		"11:	moves.w	\\tmp,(\\to)+\n"
		"12:	.set	.Lcnt,.Lcnt-2\n"
		"	.elseif	.Lcnt & 1\n"
		"	move.b	(\\from)+,\\tmp\n"
		"11:	moves.b	\\tmp,(\\to)+\n"
		"12:	.set	.Lcnt,.Lcnt-1\n"
		"	.else\n"
		"	.exitm\n"
		"	.endif\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	11b,3f\n"
		"	.long	12b,3f\n"
		"	.previous\n"
		"	.endm\n"
		"	.endif\n"
		"\n"
		"	.set	.Lcnt,%c4\n"
		"	copy_to_user %1,%2,%3\n"
		"	copy_to_user %1,%2,%3\n"
		"	copy_to_user %1,%2,%3\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"3:	moveq.l	%4,%0\n"
		"	jra	2b\n"
		"	.previous\n"
		: "+r" (res), "+a" (to), "+a" (from), "=&d" (tmp)
		: "i" (n)
		: "memory");

	return res;
}

#define __copy_from_user(to, from, n)		\
(__builtin_constant_p(n) ?			\
 __constant_copy_from_user(to, from, n) :	\
 __generic_copy_from_user(to, from, n))

#define __copy_to_user(to, from, n)		\
(__builtin_constant_p(n) ?			\
 __constant_copy_to_user(to, from, n) :		\
 __generic_copy_to_user(to, from, n))

#define __copy_to_user_inatomic		__copy_to_user
#define __copy_from_user_inatomic	__copy_from_user

#define copy_from_user(to, from, n)	__copy_from_user(to, from, n)
#define copy_to_user(to, from, n)	__copy_to_user(to, from, n)

long strncpy_from_user(char *dst, const char __user *src, long count);
long strnlen_user(const char __user *src, long n);
unsigned long clear_user(void __user *to, unsigned long n);

#define strlen_user(str) strnlen_user(str, 32767)

#endif /* _M68K_UACCESS_H */
