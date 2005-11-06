#ifndef _ASM_GENERIC_UNALIGNED_H_
#define _ASM_GENERIC_UNALIGNED_H_

/*
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents. 
 *
 * This is based almost entirely upon Richard Henderson's
 * asm-alpha/unaligned.h implementation.  Some comments were
 * taken from David Mosberger's asm-ia64/unaligned.h header.
 */

#include <linux/types.h>

/* 
 * The main single-value unaligned transfer routines.
 */
#define get_unaligned(ptr) \
	__get_unaligned((ptr), sizeof(*(ptr)))
#define put_unaligned(x,ptr) \
	__put_unaligned((__u64)(x), (ptr), sizeof(*(ptr)))

/*
 * This function doesn't actually exist.  The idea is that when
 * someone uses the macros below with an unsupported size (datatype),
 * the linker will alert us to the problem via an unresolved reference
 * error.
 */
extern void bad_unaligned_access_length(void) __attribute__((noreturn));

struct __una_u64 { __u64 x __attribute__((packed)); };
struct __una_u32 { __u32 x __attribute__((packed)); };
struct __una_u16 { __u16 x __attribute__((packed)); };

/*
 * Elemental unaligned loads 
 */

static inline __u64 __uldq(const __u64 *addr)
{
	const struct __una_u64 *ptr = (const struct __una_u64 *) addr;
	return ptr->x;
}

static inline __u32 __uldl(const __u32 *addr)
{
	const struct __una_u32 *ptr = (const struct __una_u32 *) addr;
	return ptr->x;
}

static inline __u16 __uldw(const __u16 *addr)
{
	const struct __una_u16 *ptr = (const struct __una_u16 *) addr;
	return ptr->x;
}

/*
 * Elemental unaligned stores 
 */

static inline void __ustq(__u64 val, __u64 *addr)
{
	struct __una_u64 *ptr = (struct __una_u64 *) addr;
	ptr->x = val;
}

static inline void __ustl(__u32 val, __u32 *addr)
{
	struct __una_u32 *ptr = (struct __una_u32 *) addr;
	ptr->x = val;
}

static inline void __ustw(__u16 val, __u16 *addr)
{
	struct __una_u16 *ptr = (struct __una_u16 *) addr;
	ptr->x = val;
}

#define __get_unaligned(ptr, size) ({		\
	const void *__gu_p = ptr;		\
	__typeof__(*(ptr)) val;			\
	switch (size) {				\
	case 1:					\
		val = *(const __u8 *)__gu_p;	\
		break;				\
	case 2:					\
		val = __uldw(__gu_p);		\
		break;				\
	case 4:					\
		val = __uldl(__gu_p);		\
		break;				\
	case 8:					\
		val = __uldq(__gu_p);		\
		break;				\
	default:				\
		bad_unaligned_access_length();	\
	};					\
	val;					\
})

#define __put_unaligned(val, ptr, size)		\
do {						\
	void *__gu_p = ptr;			\
	switch (size) {				\
	case 1:					\
		*(__u8 *)__gu_p = val;		\
	        break;				\
	case 2:					\
		__ustw(val, __gu_p);		\
		break;				\
	case 4:					\
		__ustl(val, __gu_p);		\
		break;				\
	case 8:					\
		__ustq(val, __gu_p);		\
		break;				\
	default:				\
	    	bad_unaligned_access_length();	\
	};					\
} while(0)

#endif /* _ASM_GENERIC_UNALIGNED_H */
