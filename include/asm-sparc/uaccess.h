/* $Id: uaccess.h,v 1.24 2001/10/30 04:32:24 davem Exp $
 * uaccess.h: User space memore access functions.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#ifdef __KERNEL__
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/vac-ops.h>
#endif

#ifndef __ASSEMBLY__

/* Sparc is not segmented, however we need to be able to fool access_ok()
 * when doing system calls from kernel mode legitimately.
 *
 * "For historical reasons, these macros are grossly misnamed." -Linus
 */

#define KERNEL_DS   ((mm_segment_t) { 0 })
#define USER_DS     ((mm_segment_t) { -1 })

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->thread.current_ds)
#define set_fs(val)	((current->thread.current_ds) = (val))

#define segment_eq(a,b)	((a).seg == (b).seg)

/* We have there a nice not-mapped page at PAGE_OFFSET - PAGE_SIZE, so that this test
 * can be fairly lightweight.
 * No one can read/write anything from userland in the kernel space by setting
 * large size and address near to PAGE_OFFSET - a fault will break his intentions.
 */
#define __user_ok(addr, size) ({ (void)(size); (addr) < STACK_TOP; })
#define __kernel_ok (segment_eq(get_fs(), KERNEL_DS))
#define __access_ok(addr,size) (__user_ok((addr) & get_fs().seg,(size)))
#define access_ok(type, addr, size)					\
	({ (void)(type); __access_ok((unsigned long)(addr), size); })

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
 *
 * There is a special way how to put a range of potentially faulting
 * insns (like twenty ldd/std's with now intervening other instructions)
 * You specify address of first in insn and 0 in fixup and in the next
 * exception_table_entry you specify last potentially faulting insn + 1
 * and in fixup the routine which should handle the fault.
 * That fixup code will get
 * (faulting_insn_address - first_insn_in_the_range_address)/4
 * in %g2 (ie. index of the faulting instruction in the range).
 */

struct exception_table_entry
{
        unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_extables_range(unsigned long addr, unsigned long *g2);

extern void __ret_efault(void);

/* Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 */
#define put_user(x,ptr) ({ \
unsigned long __pu_addr = (unsigned long)(ptr); \
__chk_user_ptr(ptr); \
__put_user_check((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr))); })

#define get_user(x,ptr) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__chk_user_ptr(ptr); \
__get_user_check((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr))); })

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr) __put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)),__typeof__(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct __user *)(x))

#define __put_user_check(x,addr,size) ({ \
register int __pu_ret; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __put_user_asm(x,b,addr,__pu_ret); break; \
case 2: __put_user_asm(x,h,addr,__pu_ret); break; \
case 4: __put_user_asm(x,,addr,__pu_ret); break; \
case 8: __put_user_asm(x,d,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} } else { __pu_ret = -EFAULT; } __pu_ret; })

#define __put_user_nocheck(x,addr,size) ({ \
register int __pu_ret; \
switch (size) { \
case 1: __put_user_asm(x,b,addr,__pu_ret); break; \
case 2: __put_user_asm(x,h,addr,__pu_ret); break; \
case 4: __put_user_asm(x,,addr,__pu_ret); break; \
case 8: __put_user_asm(x,d,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} __pu_ret; })

#define __put_user_asm(x,size,addr,ret)					\
__asm__ __volatile__(							\
	"/* Put user asm, inline. */\n"					\
"1:\t"	"st"#size " %1, %2\n\t"						\
	"clr	%0\n"							\
"2:\n\n\t"								\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"b	2b\n\t"							\
	" mov	%3, %0\n\t"						\
        ".previous\n\n\t"						\
	".section __ex_table,#alloc\n\t"				\
	".align	4\n\t"							\
	".word	1b, 3b\n\t"						\
	".previous\n\n\t"						\
       : "=&r" (ret) : "r" (x), "m" (*__m(addr)),			\
	 "i" (-EFAULT))

extern int __put_user_bad(void);

#define __get_user_check(x,addr,size,type) ({ \
register int __gu_ret; \
register unsigned long __gu_val; \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __get_user_asm(__gu_val,ub,addr,__gu_ret); break; \
case 2: __get_user_asm(__gu_val,uh,addr,__gu_ret); break; \
case 4: __get_user_asm(__gu_val,,addr,__gu_ret); break; \
case 8: __get_user_asm(__gu_val,d,addr,__gu_ret); break; \
default: __gu_val = 0; __gu_ret = __get_user_bad(); break; \
} } else { __gu_val = 0; __gu_ret = -EFAULT; } x = (type) __gu_val; __gu_ret; })

#define __get_user_check_ret(x,addr,size,type,retval) ({ \
register unsigned long __gu_val __asm__ ("l1"); \
if (__access_ok(addr,size)) { \
switch (size) { \
case 1: __get_user_asm_ret(__gu_val,ub,addr,retval); break; \
case 2: __get_user_asm_ret(__gu_val,uh,addr,retval); break; \
case 4: __get_user_asm_ret(__gu_val,,addr,retval); break; \
case 8: __get_user_asm_ret(__gu_val,d,addr,retval); break; \
default: if (__get_user_bad()) return retval; \
} x = (type) __gu_val; } else return retval; })

#define __get_user_nocheck(x,addr,size,type) ({ \
register int __gu_ret; \
register unsigned long __gu_val; \
switch (size) { \
case 1: __get_user_asm(__gu_val,ub,addr,__gu_ret); break; \
case 2: __get_user_asm(__gu_val,uh,addr,__gu_ret); break; \
case 4: __get_user_asm(__gu_val,,addr,__gu_ret); break; \
case 8: __get_user_asm(__gu_val,d,addr,__gu_ret); break; \
default: __gu_val = 0; __gu_ret = __get_user_bad(); break; \
} x = (type) __gu_val; __gu_ret; })

#define __get_user_nocheck_ret(x,addr,size,type,retval) ({ \
register unsigned long __gu_val __asm__ ("l1"); \
switch (size) { \
case 1: __get_user_asm_ret(__gu_val,ub,addr,retval); break; \
case 2: __get_user_asm_ret(__gu_val,uh,addr,retval); break; \
case 4: __get_user_asm_ret(__gu_val,,addr,retval); break; \
case 8: __get_user_asm_ret(__gu_val,d,addr,retval); break; \
default: if (__get_user_bad()) return retval; \
} x = (type) __gu_val; })

#define __get_user_asm(x,size,addr,ret)					\
__asm__ __volatile__(							\
	"/* Get user asm, inline. */\n"					\
"1:\t"	"ld"#size " %2, %1\n\t"						\
	"clr	%0\n"							\
"2:\n\n\t"								\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"clr	%1\n\t"							\
	"b	2b\n\t"							\
	" mov	%3, %0\n\n\t"						\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	4\n\t"							\
	".word	1b, 3b\n\n\t"						\
	".previous\n\t"							\
       : "=&r" (ret), "=&r" (x) : "m" (*__m(addr)),			\
	 "i" (-EFAULT))

#define __get_user_asm_ret(x,size,addr,retval)				\
if (__builtin_constant_p(retval) && retval == -EFAULT)			\
__asm__ __volatile__(							\
	"/* Get user asm ret, inline. */\n"				\
"1:\t"	"ld"#size " %1, %0\n\n\t"					\
	".section __ex_table,#alloc\n\t"				\
	".align	4\n\t"							\
	".word	1b,__ret_efault\n\n\t"					\
	".previous\n\t"							\
       : "=&r" (x) : "m" (*__m(addr)));					\
else									\
__asm__ __volatile__(							\
	"/* Get user asm ret, inline. */\n"				\
"1:\t"	"ld"#size " %1, %0\n\n\t"					\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"ret\n\t"							\
	" restore %%g0, %2, %%o0\n\n\t"					\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	4\n\t"							\
	".word	1b, 3b\n\n\t"						\
	".previous\n\t"							\
       : "=&r" (x) : "m" (*__m(addr)), "i" (retval))

extern int __get_user_bad(void);

extern unsigned long __copy_user(void __user *to, const void __user *from, unsigned long size);

static inline unsigned long copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (n && __access_ok((unsigned long) to, n))
		return __copy_user(to, (__force void __user *) from, n);
	else
		return n;
}

static inline unsigned long __copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user(to, (__force void __user *) from, n);
}

static inline unsigned long copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (n && __access_ok((unsigned long) from, n))
		return __copy_user((__force void __user *) to, from, n);
	else
		return n;
}

static inline unsigned long __copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return __copy_user((__force void __user *) to, from, n);
}

#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

static inline unsigned long __clear_user(void __user *addr, unsigned long size)
{
	unsigned long ret;

	__asm__ __volatile__ (
		".section __ex_table,#alloc\n\t"
		".align 4\n\t"
		".word 1f,3\n\t"
		".previous\n\t"
		"mov %2, %%o1\n"
		"1:\n\t"
		"call __bzero\n\t"
		" mov %1, %%o0\n\t"
		"mov %%o0, %0\n"
		: "=r" (ret) : "r" (addr), "r" (size) :
		"o0", "o1", "o2", "o3", "o4", "o5", "o7",
		"g1", "g2", "g3", "g4", "g5", "g7", "cc");

	return ret;
}

static inline unsigned long clear_user(void __user *addr, unsigned long n)
{
	if (n && __access_ok((unsigned long) addr, n))
		return __clear_user(addr, n);
	else
		return n;
}

extern long __strncpy_from_user(char *dest, const char __user *src, long count);

static inline long strncpy_from_user(char *dest, const char __user *src, long count)
{
	if (__access_ok((unsigned long) src, count))
		return __strncpy_from_user(dest, src, count);
	else
		return -EFAULT;
}

extern long __strlen_user(const char __user *);
extern long __strnlen_user(const char __user *, long len);

static inline long strlen_user(const char __user *str)
{
	if (!access_ok(VERIFY_READ, str, 0))
		return 0;
	else
		return __strlen_user(str);
}

static inline long strnlen_user(const char __user *str, long len)
{
	if (!access_ok(VERIFY_READ, str, 0))
		return 0;
	else
		return __strnlen_user(str, len);
}

#endif  /* __ASSEMBLY__ */

#endif /* _ASM_UACCESS_H */
