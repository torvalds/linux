#ifndef __M68K_UACCESS_H
#define __M68K_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/errno.h>
#include <linux/sched.h>
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


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, ptr)				\
({							\
    int __pu_err;					\
    typeof(*(ptr)) __pu_val = (x);			\
    __chk_user_ptr(ptr);				\
    switch (sizeof (*(ptr))) {				\
    case 1:						\
	__put_user_asm(__pu_err, __pu_val, ptr, b);	\
	break;						\
    case 2:						\
	__put_user_asm(__pu_err, __pu_val, ptr, w);	\
	break;						\
    case 4:						\
	__put_user_asm(__pu_err, __pu_val, ptr, l);	\
	break;						\
    case 8:                                             \
       __pu_err = __constant_copy_to_user(ptr, &__pu_val, 8);        \
       break;                                           \
    default:						\
	__pu_err = __put_user_bad();			\
	break;						\
    }							\
    __pu_err;						\
})
#define __put_user(x, ptr) put_user(x, ptr)

extern int __put_user_bad(void);

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_asm(err,x,ptr,bwl)			\
__asm__ __volatile__					\
    ("21:moves" #bwl " %2,%1\n"				\
     "1:\n"						\
     ".section .fixup,\"ax\"\n"				\
     "   .even\n"					\
     "2: movel %3,%0\n"					\
     "   jra 1b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long 21b,2b\n"				\
     "   .long 1b,2b\n"					\
     ".previous"					\
     : "=d"(err)					\
     : "m"(*(ptr)), "r"(x), "i"(-EFAULT), "0"(0))

#define get_user(x, ptr)					\
({								\
    int __gu_err;						\
    typeof(*(ptr)) __gu_val;					\
    __chk_user_ptr(ptr);					\
    switch (sizeof(*(ptr))) {					\
    case 1:							\
	__get_user_asm(__gu_err, __gu_val, ptr, b, "=d");	\
	break;							\
    case 2:							\
	__get_user_asm(__gu_err, __gu_val, ptr, w, "=r");	\
	break;							\
    case 4:							\
	__get_user_asm(__gu_err, __gu_val, ptr, l, "=r");	\
	break;							\
    case 8:                                                     \
        __gu_err = __constant_copy_from_user(&__gu_val, ptr, 8);  \
        break;                                                  \
    default:							\
	__gu_val = (typeof(*(ptr)))0;				\
	__gu_err = __get_user_bad();				\
	break;							\
    }								\
    (x) = __gu_val;						\
    __gu_err;							\
})
#define __get_user(x, ptr) get_user(x, ptr)

extern int __get_user_bad(void);

#define __get_user_asm(err,x,ptr,bwl,reg)	\
__asm__ __volatile__				\
    ("1: moves" #bwl " %2,%1\n"			\
     "2:\n"					\
     ".section .fixup,\"ax\"\n"			\
     "   .even\n"				\
     "3: movel %3,%0\n"				\
     "   sub" #bwl " %1,%1\n"			\
     "   jra 2b\n"				\
     ".previous\n"				\
     ".section __ex_table,\"a\"\n"		\
     "   .align 4\n"				\
     "   .long 1b,3b\n"				\
     ".previous"				\
     : "=d"(err), reg(x)			\
     : "m"(*(ptr)), "i" (-EFAULT), "0"(0))

static inline unsigned long
__generic_copy_from_user(void *to, const void __user *from, unsigned long n)
{
    unsigned long tmp;
    __asm__ __volatile__
	("   tstl %2\n"
	 "   jeq 2f\n"
	 "1: movesl (%1)+,%3\n"
	 "   movel %3,(%0)+\n"
	 "   subql #1,%2\n"
	 "   jne 1b\n"
	 "2: movel %4,%2\n"
	 "   bclr #1,%2\n"
	 "   jeq 4f\n"
	 "3: movesw (%1)+,%3\n"
	 "   movew %3,(%0)+\n"
	 "4: bclr #0,%2\n"
	 "   jeq 6f\n"
	 "5: movesb (%1)+,%3\n"
	 "   moveb %3,(%0)+\n"
	 "6:\n"
	 ".section .fixup,\"ax\"\n"
	 "   .even\n"
	 "7: movel %2,%%d0\n"
	 "71:clrl (%0)+\n"
	 "   subql #1,%%d0\n"
	 "   jne 71b\n"
	 "   lsll #2,%2\n"
	 "   addl %4,%2\n"
	 "   btst #1,%4\n"
	 "   jne 81f\n"
	 "   btst #0,%4\n"
	 "   jne 91f\n"
	 "   jra 6b\n"
	 "8: addql #2,%2\n"
	 "81:clrw (%0)+\n"
	 "   btst #0,%4\n"
	 "   jne 91f\n"
	 "   jra 6b\n"
	 "9: addql #1,%2\n"
	 "91:clrb (%0)+\n"
	 "   jra 6b\n"
         ".previous\n"
	 ".section __ex_table,\"a\"\n"
	 "   .align 4\n"
	 "   .long 1b,7b\n"
	 "   .long 3b,8b\n"
	 "   .long 5b,9b\n"
	 ".previous"
	 : "=a"(to), "=a"(from), "=d"(n), "=&d"(tmp)
	 : "d"(n & 3), "0"(to), "1"(from), "2"(n/4)
	 : "d0", "memory");
    return n;
}

static inline unsigned long
__generic_copy_to_user(void __user *to, const void *from, unsigned long n)
{
    unsigned long tmp;
    __asm__ __volatile__
	("   tstl %2\n"
	 "   jeq 3f\n"
	 "1: movel (%1)+,%3\n"
	 "22:movesl %3,(%0)+\n"
	 "2: subql #1,%2\n"
	 "   jne 1b\n"
	 "3: movel %4,%2\n"
	 "   bclr #1,%2\n"
	 "   jeq 4f\n"
	 "   movew (%1)+,%3\n"
	 "24:movesw %3,(%0)+\n"
	 "4: bclr #0,%2\n"
	 "   jeq 5f\n"
	 "   moveb (%1)+,%3\n"
	 "25:movesb %3,(%0)+\n"
	 "5:\n"
	 ".section .fixup,\"ax\"\n"
	 "   .even\n"
	 "60:addql #1,%2\n"
	 "6: lsll #2,%2\n"
	 "   addl %4,%2\n"
	 "   jra 5b\n"
	 "7: addql #2,%2\n"
	 "   jra 5b\n"
	 "8: addql #1,%2\n"
	 "   jra 5b\n"
	 ".previous\n"
	 ".section __ex_table,\"a\"\n"
	 "   .align 4\n"
	 "   .long 1b,60b\n"
	 "   .long 22b,6b\n"
	 "   .long 2b,6b\n"
	 "   .long 24b,7b\n"
	 "   .long 3b,60b\n"
	 "   .long 4b,7b\n"
	 "   .long 25b,8b\n"
	 "   .long 5b,8b\n"
	 ".previous"
	 : "=a"(to), "=a"(from), "=d"(n), "=&d"(tmp)
	 : "r"(n & 3), "0"(to), "1"(from), "2"(n / 4)
	 : "memory");
    return n;
}

#define __copy_from_user_big(to, from, n, fixup, copy)	\
    __asm__ __volatile__				\
	("10: movesl (%1)+,%%d0\n"			\
	 "    movel %%d0,(%0)+\n"			\
	 "    subql #1,%2\n"				\
	 "    jne 10b\n"				\
	 ".section .fixup,\"ax\"\n"			\
	 "    .even\n"					\
	 "11: movel %2,%%d0\n"				\
	 "13: clrl (%0)+\n"				\
	 "    subql #1,%%d0\n"				\
	 "    jne 13b\n"				\
	 "    lsll #2,%2\n"				\
	 fixup "\n"					\
	 "    jra 12f\n"				\
	 ".previous\n"					\
	 ".section __ex_table,\"a\"\n"			\
	 "    .align 4\n"				\
	 "    .long 10b,11b\n"				\
	 ".previous\n"					\
	 copy "\n"					\
	 "12:"						\
	 : "=a"(to), "=a"(from), "=d"(n)		\
	 : "0"(to), "1"(from), "2"(n/4)			\
	 : "d0", "memory")

static inline unsigned long
__constant_copy_from_user(void *to, const void __user *from, unsigned long n)
{
    switch (n) {
    case 0:
	break;
    case 1:
	__asm__ __volatile__
	    ("1: movesb (%1)+,%%d0\n"
	     "   moveb %%d0,(%0)+\n"
	     "2:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "3: addql #1,%2\n"
	     "   clrb (%0)+\n"
	     "   jra 2b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,3b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 2:
	__asm__ __volatile__
	    ("1: movesw (%1)+,%%d0\n"
	     "   movew %%d0,(%0)+\n"
	     "2:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "3: addql #2,%2\n"
	     "   clrw (%0)+\n"
	     "   jra 2b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,3b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 3:
	__asm__ __volatile__
	    ("1: movesw (%1)+,%%d0\n"
	     "   movew %%d0,(%0)+\n"
	     "2: movesb (%1)+,%%d0\n"
	     "   moveb %%d0,(%0)+\n"
	     "3:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "4: addql #2,%2\n"
	     "   clrw (%0)+\n"
	     "5: addql #1,%2\n"
	     "   clrb (%0)+\n"
	     "   jra 3b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,4b\n"
	     "   .long 2b,5b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 4:
	__asm__ __volatile__
	    ("1: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "2:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "3: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "   jra 2b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,3b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 8:
	__asm__ __volatile__
	    ("1: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "2: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "3:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "4: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "5: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "   jra 3b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,4b\n"
	     "   .long 2b,5b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 12:
	__asm__ __volatile__
	    ("1: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "2: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "3: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "4:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "5: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "6: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "7: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "   jra 4b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,5b\n"
	     "   .long 2b,6b\n"
	     "   .long 3b,7b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 16:
	__asm__ __volatile__
	    ("1: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "2: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "3: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "4: movesl (%1)+,%%d0\n"
	     "   movel %%d0,(%0)+\n"
	     "5:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "6: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "7: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "8: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "9: addql #4,%2\n"
	     "   clrl (%0)+\n"
	     "   jra 5b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 1b,6b\n"
	     "   .long 2b,7b\n"
	     "   .long 3b,8b\n"
	     "   .long 4b,9b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    default:
	switch (n & 3) {
	case 0:
	    __copy_from_user_big(to, from, n, "", "");
	    break;
	case 1:
	    __copy_from_user_big(to, from, n,
				 /* fixup */
				 "1: addql #1,%2\n"
				 "   clrb (%0)+",
				 /* copy */
				 "2: movesb (%1)+,%%d0\n"
				 "   moveb %%d0,(%0)+\n"
				 ".section __ex_table,\"a\"\n"
				 "   .long 2b,1b\n"
				 ".previous");
	    break;
	case 2:
	    __copy_from_user_big(to, from, n,
				 /* fixup */
				 "1: addql #2,%2\n"
				 "   clrw (%0)+",
				 /* copy */
				 "2: movesw (%1)+,%%d0\n"
				 "   movew %%d0,(%0)+\n"
				 ".section __ex_table,\"a\"\n"
				 "   .long 2b,1b\n"
				 ".previous");
	    break;
	case 3:
	    __copy_from_user_big(to, from, n,
				 /* fixup */
				 "1: addql #2,%2\n"
				 "   clrw (%0)+\n"
				 "2: addql #1,%2\n"
				 "   clrb (%0)+",
				 /* copy */
				 "3: movesw (%1)+,%%d0\n"
				 "   movew %%d0,(%0)+\n"
				 "4: movesb (%1)+,%%d0\n"
				 "   moveb %%d0,(%0)+\n"
				 ".section __ex_table,\"a\"\n"
				 "   .long 3b,1b\n"
				 "   .long 4b,2b\n"
				 ".previous");
	    break;
	}
	break;
    }
    return n;
}

#define __copy_to_user_big(to, from, n, fixup, copy)	\
    __asm__ __volatile__				\
	("10: movel (%1)+,%%d0\n"			\
	 "31: movesl %%d0,(%0)+\n"			\
	 "11: subql #1,%2\n"				\
	 "    jne 10b\n"				\
	 "41:\n"					\
	 ".section .fixup,\"ax\"\n"			\
	 "   .even\n"					\
	 "22: addql #1,%2\n"				\
	 "12: lsll #2,%2\n"				\
	 fixup "\n"					\
	 "    jra 13f\n"				\
	 ".previous\n"					\
	 ".section __ex_table,\"a\"\n"			\
	 "    .align 4\n"				\
	 "    .long 10b,22b\n"				\
	 "    .long 31b,12b\n"				\
	 "    .long 11b,12b\n"				\
	 "    .long 41b,22b\n"				\
	 ".previous\n"					\
	 copy "\n"					\
	 "13:"						\
	 : "=a"(to), "=a"(from), "=d"(n)		\
	 : "0"(to), "1"(from), "2"(n/4)			\
	 : "d0", "memory")

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

static inline unsigned long
__constant_copy_to_user(void __user *to, const void *from, unsigned long n)
{
    switch (n) {
    case 0:
	break;
    case 1:
	__asm__ __volatile__
	    ("   moveb (%1)+,%%d0\n"
	     "21:movesb %%d0,(%0)+\n"
	     "1:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "2: addql #1,%2\n"
	     "   jra 1b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n  "
	     "   .long 21b,2b\n"
	     "   .long 1b,2b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 2:
	__asm__ __volatile__
	    ("   movew (%1)+,%%d0\n"
	     "21:movesw %%d0,(%0)+\n"
	     "1:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "2: addql #2,%2\n"
	     "   jra 1b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,2b\n"
	     "   .long 1b,2b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 3:
	__asm__ __volatile__
	    ("   movew (%1)+,%%d0\n"
	     "21:movesw %%d0,(%0)+\n"
	     "1: moveb (%1)+,%%d0\n"
	     "22:movesb %%d0,(%0)+\n"
	     "2:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "3: addql #2,%2\n"
	     "4: addql #1,%2\n"
	     "   jra 2b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,3b\n"
	     "   .long 1b,3b\n"
	     "   .long 22b,4b\n"
	     "   .long 2b,4b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 4:
	__asm__ __volatile__
	    ("   movel (%1)+,%%d0\n"
	     "21:movesl %%d0,(%0)+\n"
	     "1:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "2: addql #4,%2\n"
	     "   jra 1b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,2b\n"
	     "   .long 1b,2b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 8:
	__asm__ __volatile__
	    ("   movel (%1)+,%%d0\n"
	     "21:movesl %%d0,(%0)+\n"
	     "1: movel (%1)+,%%d0\n"
	     "22:movesl %%d0,(%0)+\n"
	     "2:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "3: addql #4,%2\n"
	     "4: addql #4,%2\n"
	     "   jra 2b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,3b\n"
	     "   .long 1b,3b\n"
	     "   .long 22b,4b\n"
	     "   .long 2b,4b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 12:
	__asm__ __volatile__
	    ("   movel (%1)+,%%d0\n"
	     "21:movesl %%d0,(%0)+\n"
	     "1: movel (%1)+,%%d0\n"
	     "22:movesl %%d0,(%0)+\n"
	     "2: movel (%1)+,%%d0\n"
	     "23:movesl %%d0,(%0)+\n"
	     "3:\n"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "4: addql #4,%2\n"
	     "5: addql #4,%2\n"
	     "6: addql #4,%2\n"
	     "   jra 3b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,4b\n"
	     "   .long 1b,4b\n"
	     "   .long 22b,5b\n"
	     "   .long 2b,5b\n"
	     "   .long 23b,6b\n"
	     "   .long 3b,6b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    case 16:
	__asm__ __volatile__
	    ("   movel (%1)+,%%d0\n"
	     "21:movesl %%d0,(%0)+\n"
	     "1: movel (%1)+,%%d0\n"
	     "22:movesl %%d0,(%0)+\n"
	     "2: movel (%1)+,%%d0\n"
	     "23:movesl %%d0,(%0)+\n"
	     "3: movel (%1)+,%%d0\n"
	     "24:movesl %%d0,(%0)+\n"
	     "4:"
	     ".section .fixup,\"ax\"\n"
	     "   .even\n"
	     "5: addql #4,%2\n"
	     "6: addql #4,%2\n"
	     "7: addql #4,%2\n"
	     "8: addql #4,%2\n"
	     "   jra 4b\n"
	     ".previous\n"
	     ".section __ex_table,\"a\"\n"
	     "   .align 4\n"
	     "   .long 21b,5b\n"
	     "   .long 1b,5b\n"
	     "   .long 22b,6b\n"
	     "   .long 2b,6b\n"
	     "   .long 23b,7b\n"
	     "   .long 3b,7b\n"
	     "   .long 24b,8b\n"
	     "   .long 4b,8b\n"
	     ".previous"
	     : "=a"(to), "=a"(from), "=d"(n)
	     : "0"(to), "1"(from), "2"(0)
	     : "d0", "memory");
	break;
    default:
	switch (n & 3) {
	case 0:
	    __copy_to_user_big(to, from, n, "", "");
	    break;
	case 1:
	    __copy_to_user_big(to, from, n,
			       /* fixup */
			       "1: addql #1,%2",
			       /* copy */
			       "   moveb (%1)+,%%d0\n"
			       "22:movesb %%d0,(%0)+\n"
			       "2:"
			       ".section __ex_table,\"a\"\n"
			       "   .long 22b,1b\n"
			       "   .long 2b,1b\n"
			       ".previous");
	    break;
	case 2:
	    __copy_to_user_big(to, from, n,
			       /* fixup */
			       "1: addql #2,%2",
			       /* copy */
			       "   movew (%1)+,%%d0\n"
			       "22:movesw %%d0,(%0)+\n"
			       "2:"
			       ".section __ex_table,\"a\"\n"
			       "   .long 22b,1b\n"
			       "   .long 2b,1b\n"
			       ".previous");
	    break;
	case 3:
	    __copy_to_user_big(to, from, n,
			       /* fixup */
			       "1: addql #2,%2\n"
			       "2: addql #1,%2",
			       /* copy */
			       "   movew (%1)+,%%d0\n"
			       "23:movesw %%d0,(%0)+\n"
			       "3: moveb (%1)+,%%d0\n"
			       "24:movesb %%d0,(%0)+\n"
			       "4:"
			       ".section __ex_table,\"a\"\n"
			       "   .long 23b,1b\n"
			       "   .long 3b,1b\n"
			       "   .long 24b,2b\n"
			       "   .long 4b,2b\n"
			       ".previous");
	    break;
	}
	break;
    }
    return n;
}

#define copy_from_user(to, from, n)		\
(__builtin_constant_p(n) ?			\
 __constant_copy_from_user(to, from, n) :	\
 __generic_copy_from_user(to, from, n))

#define copy_to_user(to, from, n)		\
(__builtin_constant_p(n) ?			\
 __constant_copy_to_user(to, from, n) :		\
 __generic_copy_to_user(to, from, n))

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)
#define __copy_to_user(to, from, n) copy_to_user(to, from, n)

/*
 * Copy a null terminated string from userspace.
 */

static inline long
strncpy_from_user(char *dst, const char __user *src, long count)
{
    long res;
    if (count == 0) return count;
    __asm__ __volatile__
	("1: movesb (%2)+,%%d0\n"
	 "12:moveb %%d0,(%1)+\n"
	 "   jeq 2f\n"
	 "   subql #1,%3\n"
	 "   jne 1b\n"
	 "2: subl %3,%0\n"
	 "3:\n"
	 ".section .fixup,\"ax\"\n"
	 "   .even\n"
	 "4: movel %4,%0\n"
	 "   jra 3b\n"
	 ".previous\n"
	 ".section __ex_table,\"a\"\n"
	 "   .align 4\n"
	 "   .long 1b,4b\n"
	 "   .long 12b,4b\n"
	 ".previous"
	 : "=d"(res), "=a"(dst), "=a"(src), "=d"(count)
	 : "i"(-EFAULT), "0"(count), "1"(dst), "2"(src), "3"(count)
	 : "d0", "memory");
    return res;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
static inline long strnlen_user(const char __user *src, long n)
{
	long res;

	res = -(unsigned long)src;
	__asm__ __volatile__
		("1:\n"
		 "   tstl %2\n"
		 "   jeq 3f\n"
		 "2: movesb (%1)+,%%d0\n"
		 "22:\n"
		 "   subql #1,%2\n"
		 "   tstb %%d0\n"
		 "   jne 1b\n"
		 "   jra 4f\n"
		 "3:\n"
		 "   addql #1,%0\n"
		 "4:\n"
		 "   addl %1,%0\n"
		 "5:\n"
		 ".section .fixup,\"ax\"\n"
		 "   .even\n"
		 "6: moveq %3,%0\n"
		 "   jra 5b\n"
		 ".previous\n"
		 ".section __ex_table,\"a\"\n"
		 "   .align 4\n"
		 "   .long 2b,6b\n"
		 "   .long 22b,6b\n"
		 ".previous"
		 : "=d"(res), "=a"(src), "=d"(n)
		 : "i"(0), "0"(res), "1"(src), "2"(n)
		 : "d0");
	return res;
}

#define strlen_user(str) strnlen_user(str, 32767)

/*
 * Zero Userspace
 */

static inline unsigned long
clear_user(void __user *to, unsigned long n)
{
    __asm__ __volatile__
	("   tstl %1\n"
	 "   jeq 3f\n"
	 "1: movesl %3,(%0)+\n"
	 "2: subql #1,%1\n"
	 "   jne 1b\n"
	 "3: movel %2,%1\n"
	 "   bclr #1,%1\n"
	 "   jeq 4f\n"
	 "24:movesw %3,(%0)+\n"
	 "4: bclr #0,%1\n"
	 "   jeq 5f\n"
	 "25:movesb %3,(%0)+\n"
	 "5:\n"
	 ".section .fixup,\"ax\"\n"
	 "   .even\n"
	 "61:addql #1,%1\n"
	 "6: lsll #2,%1\n"
	 "   addl %2,%1\n"
	 "   jra 5b\n"
	 "7: addql #2,%1\n"
	 "   jra 5b\n"
	 "8: addql #1,%1\n"
	 "   jra 5b\n"
	 ".previous\n"
	 ".section __ex_table,\"a\"\n"
	 "   .align 4\n"
	 "   .long 1b,61b\n"
	 "   .long 2b,6b\n"
	 "   .long 3b,61b\n"
	 "   .long 24b,7b\n"
	 "   .long 4b,7b\n"
	 "   .long 25b,8b\n"
	 "   .long 5b,8b\n"
	 ".previous"
	 : "=a"(to), "=d"(n)
	 : "r"(n & 3), "r"(0), "0"(to), "1"(n/4));
    return n;
}

#endif /* _M68K_UACCESS_H */
