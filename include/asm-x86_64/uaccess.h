#ifndef __X86_64_UACCESS_H
#define __X86_64_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/prefetch.h>
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

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFFFFFFFFFFUL)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a,b)	((a).seg == (b).seg)

#define __addr_ok(addr) (!((unsigned long)(addr) & (current_thread_info()->addr_limit.seg)))

/*
 * Uhhuh, this needs 65-bit arithmetic. We have a carry..
 */
#define __range_not_ok(addr,size) ({ \
	unsigned long flag,roksum; \
	__chk_user_ptr(addr); \
	asm("# range_ok\n\r" \
		"addq %3,%1 ; sbbq %0,%0 ; cmpq %1,%4 ; sbbq $0,%0"  \
		:"=&r" (flag), "=r" (roksum) \
		:"1" (addr),"g" ((long)(size)),"g" (current_thread_info()->addr_limit.seg)); \
	flag; })

#define access_ok(type, addr, size) (__range_not_ok(addr,size) == 0)

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

#define ARCH_HAS_SEARCH_EXTABLE

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

#define __get_user_x(size,ret,x,ptr) \
	asm volatile("call __get_user_" #size \
		:"=a" (ret),"=d" (x) \
		:"c" (ptr) \
		:"r8")

/* Careful: we have to cast the result to the type of the pointer for sign reasons */
#define get_user(x,ptr)							\
({	unsigned long __val_gu;						\
	int __ret_gu; 							\
	__chk_user_ptr(ptr);						\
	switch(sizeof (*(ptr))) {					\
	case 1:  __get_user_x(1,__ret_gu,__val_gu,ptr); break;		\
	case 2:  __get_user_x(2,__ret_gu,__val_gu,ptr); break;		\
	case 4:  __get_user_x(4,__ret_gu,__val_gu,ptr); break;		\
	case 8:  __get_user_x(8,__ret_gu,__val_gu,ptr); break;		\
	default: __get_user_bad(); break;				\
	}								\
	(x) = (typeof(*(ptr)))__val_gu;				\
	__ret_gu;							\
})

extern void __put_user_1(void);
extern void __put_user_2(void);
extern void __put_user_4(void);
extern void __put_user_8(void);
extern void __put_user_bad(void);

#define __put_user_x(size,ret,x,ptr)					\
	asm volatile("call __put_user_" #size			\
		:"=a" (ret)						\
		:"c" (ptr),"d" (x)					\
		:"r8")

#define put_user(x,ptr)							\
  __put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user(x,ptr) \
  __get_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __put_user(x,ptr) \
  __put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user_unaligned __get_user
#define __put_user_unaligned __put_user

#define __put_user_nocheck(x,ptr,size)			\
({							\
	int __pu_err;					\
	__put_user_size((x),(ptr),(size),__pu_err);	\
	__pu_err;					\
})


#define __put_user_check(x,ptr,size)			\
({							\
	int __pu_err;					\
	typeof(*(ptr)) __user *__pu_addr = (ptr);	\
	switch (size) { 				\
	case 1: __put_user_x(1,__pu_err,x,__pu_addr); break;	\
	case 2: __put_user_x(2,__pu_err,x,__pu_addr); break;	\
	case 4: __put_user_x(4,__pu_err,x,__pu_addr); break;	\
	case 8: __put_user_x(8,__pu_err,x,__pu_addr); break;	\
	default: __put_user_bad();			\
	}						\
	__pu_err;					\
})

#define __put_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __put_user_asm(x,ptr,retval,"b","b","iq",-EFAULT); break;\
	  case 2: __put_user_asm(x,ptr,retval,"w","w","ir",-EFAULT); break;\
	  case 4: __put_user_asm(x,ptr,retval,"l","k","ir",-EFAULT); break;\
	  case 8: __put_user_asm(x,ptr,retval,"q","","Zr",-EFAULT); break;\
	  default: __put_user_bad();					\
	}								\
} while (0)

/* FIXME: this hack is definitely wrong -AK */
struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_asm(x, addr, err, itype, rtype, ltype, errno)	\
	asm volatile(					\
		"1:	mov"itype" %"rtype"1,%2\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	mov %3,%0\n"				\
		"	jmp 2b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 8\n"				\
		"	.quad 1b,3b\n"				\
		".previous"					\
		: "=r"(err)					\
		: ltype (x), "m"(__m(addr)), "i"(errno), "0"(err))


#define __get_user_nocheck(x,ptr,size)				\
({								\
	int __gu_err;						\
	unsigned long __gu_val;					\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);	\
	(x) = (typeof(*(ptr)))__gu_val;			\
	__gu_err;						\
})

extern int __get_user_1(void);
extern int __get_user_2(void);
extern int __get_user_4(void);
extern int __get_user_8(void);
extern int __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __get_user_asm(x,ptr,retval,"b","b","=q",-EFAULT); break;\
	  case 2: __get_user_asm(x,ptr,retval,"w","w","=r",-EFAULT); break;\
	  case 4: __get_user_asm(x,ptr,retval,"l","k","=r",-EFAULT); break;\
	  case 8: __get_user_asm(x,ptr,retval,"q","","=r",-EFAULT); break;\
	  default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype, rtype, ltype, errno)	\
	asm volatile(					\
		"1:	mov"itype" %2,%"rtype"1\n"		\
		"2:\n"						\
		".section .fixup,\"ax\"\n"			\
		"3:	mov %3,%0\n"				\
		"	xor"itype" %"rtype"1,%"rtype"1\n"	\
		"	jmp 2b\n"				\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		"	.align 8\n"				\
		"	.quad 1b,3b\n"				\
		".previous"					\
		: "=r"(err), ltype (x)				\
		: "m"(__m(addr)), "i"(errno), "0"(err))

/*
 * Copy To/From Userspace
 */

/* Handles exceptions in both to and from, but doesn't do access_ok */
__must_check unsigned long
copy_user_generic(void *to, const void *from, unsigned len);

__must_check unsigned long
copy_to_user(void __user *to, const void *from, unsigned len);
__must_check unsigned long
copy_from_user(void *to, const void __user *from, unsigned len);
__must_check unsigned long
copy_in_user(void __user *to, const void __user *from, unsigned len);

static __always_inline __must_check
int __copy_from_user(void *dst, const void __user *src, unsigned size)
{ 
	int ret = 0;
	if (!__builtin_constant_p(size))
		return copy_user_generic(dst,(__force void *)src,size);
	switch (size) { 
	case 1:__get_user_asm(*(u8*)dst,(u8 __user *)src,ret,"b","b","=q",1); 
		return ret;
	case 2:__get_user_asm(*(u16*)dst,(u16 __user *)src,ret,"w","w","=r",2);
		return ret;
	case 4:__get_user_asm(*(u32*)dst,(u32 __user *)src,ret,"l","k","=r",4);
		return ret;
	case 8:__get_user_asm(*(u64*)dst,(u64 __user *)src,ret,"q","","=r",8);
		return ret; 
	case 10:
	       	__get_user_asm(*(u64*)dst,(u64 __user *)src,ret,"q","","=r",16);
		if (unlikely(ret)) return ret;
		__get_user_asm(*(u16*)(8+(char*)dst),(u16 __user *)(8+(char __user *)src),ret,"w","w","=r",2);
		return ret; 
	case 16:
		__get_user_asm(*(u64*)dst,(u64 __user *)src,ret,"q","","=r",16);
		if (unlikely(ret)) return ret;
		__get_user_asm(*(u64*)(8+(char*)dst),(u64 __user *)(8+(char __user *)src),ret,"q","","=r",8);
		return ret; 
	default:
		return copy_user_generic(dst,(__force void *)src,size); 
	}
}	

static __always_inline __must_check
int __copy_to_user(void __user *dst, const void *src, unsigned size)
{ 
	int ret = 0;
	if (!__builtin_constant_p(size))
		return copy_user_generic((__force void *)dst,src,size);
	switch (size) { 
	case 1:__put_user_asm(*(u8*)src,(u8 __user *)dst,ret,"b","b","iq",1); 
		return ret;
	case 2:__put_user_asm(*(u16*)src,(u16 __user *)dst,ret,"w","w","ir",2);
		return ret;
	case 4:__put_user_asm(*(u32*)src,(u32 __user *)dst,ret,"l","k","ir",4);
		return ret;
	case 8:__put_user_asm(*(u64*)src,(u64 __user *)dst,ret,"q","","ir",8);
		return ret; 
	case 10:
		__put_user_asm(*(u64*)src,(u64 __user *)dst,ret,"q","","ir",10);
		if (unlikely(ret)) return ret;
		asm("":::"memory");
		__put_user_asm(4[(u16*)src],4+(u16 __user *)dst,ret,"w","w","ir",2);
		return ret; 
	case 16:
		__put_user_asm(*(u64*)src,(u64 __user *)dst,ret,"q","","ir",16);
		if (unlikely(ret)) return ret;
		asm("":::"memory");
		__put_user_asm(1[(u64*)src],1+(u64 __user *)dst,ret,"q","","ir",8);
		return ret; 
	default:
		return copy_user_generic((__force void *)dst,src,size); 
	}
}	

static __always_inline __must_check
int __copy_in_user(void __user *dst, const void __user *src, unsigned size)
{ 
	int ret = 0;
	if (!__builtin_constant_p(size))
		return copy_user_generic((__force void *)dst,(__force void *)src,size);
	switch (size) { 
	case 1: { 
		u8 tmp;
		__get_user_asm(tmp,(u8 __user *)src,ret,"b","b","=q",1); 
		if (likely(!ret))
			__put_user_asm(tmp,(u8 __user *)dst,ret,"b","b","iq",1); 
		return ret;
	}
	case 2: { 
		u16 tmp;
		__get_user_asm(tmp,(u16 __user *)src,ret,"w","w","=r",2); 
		if (likely(!ret))
			__put_user_asm(tmp,(u16 __user *)dst,ret,"w","w","ir",2); 
		return ret;
	}

	case 4: { 
		u32 tmp;
		__get_user_asm(tmp,(u32 __user *)src,ret,"l","k","=r",4); 
		if (likely(!ret))
			__put_user_asm(tmp,(u32 __user *)dst,ret,"l","k","ir",4); 
		return ret;
	}
	case 8: { 
		u64 tmp;
		__get_user_asm(tmp,(u64 __user *)src,ret,"q","","=r",8); 
		if (likely(!ret))
			__put_user_asm(tmp,(u64 __user *)dst,ret,"q","","ir",8); 
		return ret;
	}
	default:
		return copy_user_generic((__force void *)dst,(__force void *)src,size); 
	}
}	

__must_check long 
strncpy_from_user(char *dst, const char __user *src, long count);
__must_check long 
__strncpy_from_user(char *dst, const char __user *src, long count);
__must_check long strnlen_user(const char __user *str, long n);
__must_check long __strnlen_user(const char __user *str, long n);
__must_check long strlen_user(const char __user *str);
__must_check unsigned long clear_user(void __user *mem, unsigned long len);
__must_check unsigned long __clear_user(void __user *mem, unsigned long len);

__must_check long __copy_from_user_inatomic(void *dst, const void __user *src, unsigned size);

static __must_check __always_inline int
__copy_to_user_inatomic(void __user *dst, const void *src, unsigned size)
{
	return copy_user_generic((__force void *)dst, src, size);
}

#define ARCH_HAS_NOCACHE_UACCESS 1
extern long __copy_user_nocache(void *dst, const void __user *src, unsigned size, int zerorest);

static inline int __copy_from_user_nocache(void *dst, const void __user *src, unsigned size)
{
	might_sleep();
	return __copy_user_nocache(dst, src, size, 1);
}

static inline int __copy_from_user_inatomic_nocache(void *dst, const void __user *src, unsigned size)
{
	return __copy_user_nocache(dst, src, size, 0);
}

#endif /* __X86_64_UACCESS_H */
