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
 		const void __user *__pu_ptr = (ptr);			\
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
	(x) = (typeof(*(ptr)))(unsigned long)__gu_val;		\
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

#define __constant_copy_from_user_asm(res, to, from, tmp, n, s1, s2, s3)\
	asm volatile ("\n"						\
		"1:	moves."#s1"	(%2)+,%3\n"			\
		"	move."#s1"	%3,(%1)+\n"			\
		"2:	moves."#s2"	(%2)+,%3\n"			\
		"	move."#s2"	%3,(%1)+\n"			\
		"	.ifnc	\""#s3"\",\"\"\n"			\
		"3:	moves."#s3"	(%2)+,%3\n"			\
		"	move."#s3"	%3,(%1)+\n"			\
		"	.endif\n"					\
		"4:\n"							\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	4\n"					\
		"	.long	1b,10f\n"				\
		"	.long	2b,20f\n"				\
		"	.ifnc	\""#s3"\",\"\"\n"			\
		"	.long	3b,30f\n"				\
		"	.endif\n"					\
		"	.previous\n"					\
		"\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.even\n"					\
		"10:	clr."#s1"	(%1)+\n"			\
		"20:	clr."#s2"	(%1)+\n"			\
		"	.ifnc	\""#s3"\",\"\"\n"			\
		"30:	clr."#s3"	(%1)+\n"			\
		"	.endif\n"					\
		"	moveq.l	#"#n",%0\n"				\
		"	jra	4b\n"					\
		"	.previous\n"					\
		: "+d" (res), "+&a" (to), "+a" (from), "=&d" (tmp)	\
		: : "memory")

static __always_inline unsigned long
__constant_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = 0, tmp;

	switch (n) {
	case 1:
		__get_user_asm(res, *(u8 *)to, (u8 __user *)from, u8, b, d, 1);
		break;
	case 2:
		__get_user_asm(res, *(u16 *)to, (u16 __user *)from, u16, w, d, 2);
		break;
	case 3:
		__constant_copy_from_user_asm(res, to, from, tmp, 3, w, b,);
		break;
	case 4:
		__get_user_asm(res, *(u32 *)to, (u32 __user *)from, u32, l, r, 4);
		break;
	case 5:
		__constant_copy_from_user_asm(res, to, from, tmp, 5, l, b,);
		break;
	case 6:
		__constant_copy_from_user_asm(res, to, from, tmp, 6, l, w,);
		break;
	case 7:
		__constant_copy_from_user_asm(res, to, from, tmp, 7, l, w, b);
		break;
	case 8:
		__constant_copy_from_user_asm(res, to, from, tmp, 8, l, l,);
		break;
	case 9:
		__constant_copy_from_user_asm(res, to, from, tmp, 9, l, l, b);
		break;
	case 10:
		__constant_copy_from_user_asm(res, to, from, tmp, 10, l, l, w);
		break;
	case 12:
		__constant_copy_from_user_asm(res, to, from, tmp, 12, l, l, l);
		break;
	default:
		/* we limit the inlined version to 3 moves */
		return __generic_copy_from_user(to, from, n);
	}

	return res;
}

#define __constant_copy_to_user_asm(res, to, from, tmp, n, s1, s2, s3)	\
	asm volatile ("\n"						\
		"	move."#s1"	(%2)+,%3\n"			\
		"11:	moves."#s1"	%3,(%1)+\n"			\
		"12:	move."#s2"	(%2)+,%3\n"			\
		"21:	moves."#s2"	%3,(%1)+\n"			\
		"22:\n"							\
		"	.ifnc	\""#s3"\",\"\"\n"			\
		"	move."#s3"	(%2)+,%3\n"			\
		"31:	moves."#s3"	%3,(%1)+\n"			\
		"32:\n"							\
		"	.endif\n"					\
		"4:\n"							\
		"\n"							\
		"	.section __ex_table,\"a\"\n"			\
		"	.align	4\n"					\
		"	.long	11b,5f\n"				\
		"	.long	12b,5f\n"				\
		"	.long	21b,5f\n"				\
		"	.long	22b,5f\n"				\
		"	.ifnc	\""#s3"\",\"\"\n"			\
		"	.long	31b,5f\n"				\
		"	.long	32b,5f\n"				\
		"	.endif\n"					\
		"	.previous\n"					\
		"\n"							\
		"	.section .fixup,\"ax\"\n"			\
		"	.even\n"					\
		"5:	moveq.l	#"#n",%0\n"				\
		"	jra	4b\n"					\
		"	.previous\n"					\
		: "+d" (res), "+a" (to), "+a" (from), "=&d" (tmp)	\
		: : "memory")

static __always_inline unsigned long
__constant_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	unsigned long res = 0, tmp;

	switch (n) {
	case 1:
		__put_user_asm(res, *(u8 *)from, (u8 __user *)to, b, d, 1);
		break;
	case 2:
		__put_user_asm(res, *(u16 *)from, (u16 __user *)to, w, d, 2);
		break;
	case 3:
		__constant_copy_to_user_asm(res, to, from, tmp, 3, w, b,);
		break;
	case 4:
		__put_user_asm(res, *(u32 *)from, (u32 __user *)to, l, r, 4);
		break;
	case 5:
		__constant_copy_to_user_asm(res, to, from, tmp, 5, l, b,);
		break;
	case 6:
		__constant_copy_to_user_asm(res, to, from, tmp, 6, l, w,);
		break;
	case 7:
		__constant_copy_to_user_asm(res, to, from, tmp, 7, l, w, b);
		break;
	case 8:
		__constant_copy_to_user_asm(res, to, from, tmp, 8, l, l,);
		break;
	case 9:
		__constant_copy_to_user_asm(res, to, from, tmp, 9, l, l, b);
		break;
	case 10:
		__constant_copy_to_user_asm(res, to, from, tmp, 10, l, l, w);
		break;
	case 12:
		__constant_copy_to_user_asm(res, to, from, tmp, 12, l, l, l);
		break;
	default:
		/* limit the inlined version to 3 moves */
		return __generic_copy_to_user(to, from, n);
	}

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
