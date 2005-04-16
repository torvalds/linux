/* $Id: string.h,v 1.20 2001/09/27 04:36:24 kanoj Exp $
 * string.h: External definitions for optimized assembly string
 *           routines for the Linux Kernel.
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997,1999 Jakub Jelinek (jakub@redhat.com)
 */

#ifndef __SPARC64_STRING_H__
#define __SPARC64_STRING_H__

/* Really, userland/ksyms should not see any of this stuff. */

#ifdef __KERNEL__

#include <asm/asi.h>

extern void *__memset(void *,int,__kernel_size_t);

#ifndef EXPORT_SYMTAB_STROPS

/* First the mem*() things. */
#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *, const void *, __kernel_size_t);

#define __HAVE_ARCH_MEMSET
extern void *__builtin_memset(void *,int,__kernel_size_t);

static inline void *__constant_memset(void *s, int c, __kernel_size_t count)
{
	extern __kernel_size_t __bzero(void *, __kernel_size_t);

	if (!c) {
		__bzero(s, count);
		return s;
	} else
		return __memset(s, c, count);
}

#undef memset
#define memset(s, c, count) \
((__builtin_constant_p(count) && (count) <= 32) ? \
 __builtin_memset((s), (c), (count)) : \
 (__builtin_constant_p(c) ? \
  __constant_memset((s), (c), (count)) : \
  __memset((s), (c), (count))))

#define __HAVE_ARCH_MEMSCAN

#undef memscan
#define memscan(__arg0, __char, __arg2)					\
({									\
	extern void *__memscan_zero(void *, size_t);			\
	extern void *__memscan_generic(void *, int, size_t);		\
	void *__retval, *__addr = (__arg0);				\
	size_t __size = (__arg2);					\
									\
	if(__builtin_constant_p(__char) && !(__char))			\
		__retval = __memscan_zero(__addr, __size);		\
	else								\
		__retval = __memscan_generic(__addr, (__char), __size);	\
									\
	__retval;							\
})

#define __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t);

/* Now the str*() stuff... */
#define __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *);

#define __HAVE_ARCH_STRNCMP
extern int strncmp(const char *, const char *, __kernel_size_t);

#endif /* !EXPORT_SYMTAB_STROPS */

#endif /* __KERNEL__ */

#endif /* !(__SPARC64_STRING_H__) */
