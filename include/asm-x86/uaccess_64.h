#ifndef __X86_64_UACCESS_H
#define __X86_64_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/prefetch.h>
#include <asm/page.h>

#define ARCH_HAS_SEARCH_EXTABLE

extern void __put_user_1(void);
extern void __put_user_2(void);
extern void __put_user_4(void);
extern void __put_user_8(void);
extern void __put_user_bad(void);

#define __put_user_x(size, ret, x, ptr)					\
	asm volatile("call __put_user_" #size				\
		     :"=a" (ret)					\
		     :"c" (ptr),"a" (x)					\
		     :"ebx")

#define put_user(x, ptr)						\
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user(x, ptr)						\
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))
#define __put_user(x, ptr)						\
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

#define __get_user_unaligned __get_user
#define __put_user_unaligned __put_user

#define __put_user_check(x, ptr, size)				\
({								\
	int __pu_err;						\
	typeof(*(ptr)) __user *__pu_addr = (ptr);		\
	switch (size) {						\
	case 1:							\
		__put_user_x(1, __pu_err, x, __pu_addr);	\
		break;						\
	case 2:							\
		__put_user_x(2, __pu_err, x, __pu_addr);	\
		break;						\
	case 4:							\
		__put_user_x(4, __pu_err, x, __pu_addr);	\
		break;						\
	case 8:							\
		__put_user_x(8, __pu_err, x, __pu_addr);	\
		break;						\
	default:						\
		__put_user_bad();				\
	}							\
	__pu_err;						\
})

#define __get_user_nocheck(x, ptr, size)			\
({								\
	int __gu_err;						\
	unsigned long __gu_val;					\
	__get_user_size(__gu_val, (ptr), (size), __gu_err, -EFAULT);\
	(x) = (__force typeof(*(ptr)))__gu_val;			\
	__gu_err;						\
})

#define __get_user_size(x, ptr, size, retval, errret)			\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	case 1:								\
		__get_user_asm(x, ptr, retval, "b", "b", "=q", errret);\
		break;							\
	case 2:								\
		__get_user_asm(x, ptr, retval, "w", "w", "=r", errret);\
		break;							\
	case 4:								\
		__get_user_asm(x, ptr, retval, "l", "k", "=r", errret);\
		break;							\
	case 8:								\
		__get_user_asm(x, ptr, retval, "q", "", "=r", errret);	\
		break;							\
	default:							\
		(x) = __get_user_bad();					\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype, rtype, ltype, errno)	\
	asm volatile("1:	mov"itype" %2,%"rtype"1\n"		\
		     "2:\n"						\
		     ".section .fixup, \"ax\"\n"			\
		     "3:	mov %3,%0\n"				\
		     "	xor"itype" %"rtype"1,%"rtype"1\n"		\
		     "	jmp 2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE(1b, 3b)				\
		     : "=r" (err), ltype (x)				\
		     : "m" (__m(addr)), "i"(errno), "0"(err))

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
		return copy_user_generic(dst, (__force void *)src, size);
	switch (size) {
	case 1:__get_user_asm(*(u8 *)dst, (u8 __user *)src,
			      ret, "b", "b", "=q", 1);
		return ret;
	case 2:__get_user_asm(*(u16 *)dst, (u16 __user *)src,
			      ret, "w", "w", "=r", 2);
		return ret;
	case 4:__get_user_asm(*(u32 *)dst, (u32 __user *)src,
			      ret, "l", "k", "=r", 4);
		return ret;
	case 8:__get_user_asm(*(u64 *)dst, (u64 __user *)src,
			      ret, "q", "", "=r", 8);
		return ret;
	case 10:
		__get_user_asm(*(u64 *)dst, (u64 __user *)src,
			       ret, "q", "", "=r", 16);
		if (unlikely(ret))
			return ret;
		__get_user_asm(*(u16 *)(8 + (char *)dst),
			       (u16 __user *)(8 + (char __user *)src),
			       ret, "w", "w", "=r", 2);
		return ret;
	case 16:
		__get_user_asm(*(u64 *)dst, (u64 __user *)src,
			       ret, "q", "", "=r", 16);
		if (unlikely(ret))
			return ret;
		__get_user_asm(*(u64 *)(8 + (char *)dst),
			       (u64 __user *)(8 + (char __user *)src),
			       ret, "q", "", "=r", 8);
		return ret;
	default:
		return copy_user_generic(dst, (__force void *)src, size);
	}
}

static __always_inline __must_check
int __copy_to_user(void __user *dst, const void *src, unsigned size)
{
	int ret = 0;
	if (!__builtin_constant_p(size))
		return copy_user_generic((__force void *)dst, src, size);
	switch (size) {
	case 1:__put_user_asm(*(u8 *)src, (u8 __user *)dst,
			      ret, "b", "b", "iq", 1);
		return ret;
	case 2:__put_user_asm(*(u16 *)src, (u16 __user *)dst,
			      ret, "w", "w", "ir", 2);
		return ret;
	case 4:__put_user_asm(*(u32 *)src, (u32 __user *)dst,
			      ret, "l", "k", "ir", 4);
		return ret;
	case 8:__put_user_asm(*(u64 *)src, (u64 __user *)dst,
			      ret, "q", "", "ir", 8);
		return ret;
	case 10:
		__put_user_asm(*(u64 *)src, (u64 __user *)dst,
			       ret, "q", "", "ir", 10);
		if (unlikely(ret))
			return ret;
		asm("":::"memory");
		__put_user_asm(4[(u16 *)src], 4 + (u16 __user *)dst,
			       ret, "w", "w", "ir", 2);
		return ret;
	case 16:
		__put_user_asm(*(u64 *)src, (u64 __user *)dst,
			       ret, "q", "", "ir", 16);
		if (unlikely(ret))
			return ret;
		asm("":::"memory");
		__put_user_asm(1[(u64 *)src], 1 + (u64 __user *)dst,
			       ret, "q", "", "ir", 8);
		return ret;
	default:
		return copy_user_generic((__force void *)dst, src, size);
	}
}

static __always_inline __must_check
int __copy_in_user(void __user *dst, const void __user *src, unsigned size)
{
	int ret = 0;
	if (!__builtin_constant_p(size))
		return copy_user_generic((__force void *)dst,
					 (__force void *)src, size);
	switch (size) {
	case 1: {
		u8 tmp;
		__get_user_asm(tmp, (u8 __user *)src,
			       ret, "b", "b", "=q", 1);
		if (likely(!ret))
			__put_user_asm(tmp, (u8 __user *)dst,
				       ret, "b", "b", "iq", 1);
		return ret;
	}
	case 2: {
		u16 tmp;
		__get_user_asm(tmp, (u16 __user *)src,
			       ret, "w", "w", "=r", 2);
		if (likely(!ret))
			__put_user_asm(tmp, (u16 __user *)dst,
				       ret, "w", "w", "ir", 2);
		return ret;
	}

	case 4: {
		u32 tmp;
		__get_user_asm(tmp, (u32 __user *)src,
			       ret, "l", "k", "=r", 4);
		if (likely(!ret))
			__put_user_asm(tmp, (u32 __user *)dst,
				       ret, "l", "k", "ir", 4);
		return ret;
	}
	case 8: {
		u64 tmp;
		__get_user_asm(tmp, (u64 __user *)src,
			       ret, "q", "", "=r", 8);
		if (likely(!ret))
			__put_user_asm(tmp, (u64 __user *)dst,
				       ret, "q", "", "ir", 8);
		return ret;
	}
	default:
		return copy_user_generic((__force void *)dst,
					 (__force void *)src, size);
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

__must_check long __copy_from_user_inatomic(void *dst, const void __user *src,
					    unsigned size);

static __must_check __always_inline int
__copy_to_user_inatomic(void __user *dst, const void *src, unsigned size)
{
	return copy_user_generic((__force void *)dst, src, size);
}

#define ARCH_HAS_NOCACHE_UACCESS 1
extern long __copy_user_nocache(void *dst, const void __user *src,
				unsigned size, int zerorest);

static inline int __copy_from_user_nocache(void *dst, const void __user *src,
					   unsigned size)
{
	might_sleep();
	return __copy_user_nocache(dst, src, size, 1);
}

static inline int __copy_from_user_inatomic_nocache(void *dst,
						    const void __user *src,
						    unsigned size)
{
	return __copy_user_nocache(dst, src, size, 0);
}

#endif /* __X86_64_UACCESS_H */
