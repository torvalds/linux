/* unaligned.h: unaligned access handler
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_UNALIGNED_H
#define _ASM_UNALIGNED_H

#include <linux/config.h>

/*
 * Unaligned accesses on uClinux can't be performed in a fault handler - the
 * CPU detects them as imprecise exceptions making this impossible.
 *
 * With the FR451, however, they are precise, and so we used to fix them up in
 * the memory access fault handler.  However, instruction bundling make this
 * impractical.  So, now we fall back to using memcpy.
 */
#ifdef CONFIG_MMU

/*
 * The asm statement in the macros below is a way to get GCC to copy a
 * value from one variable to another without having any clue it's
 * actually doing so, so that it won't have any idea that the values
 * in the two variables are related.
 */

#define get_unaligned(ptr) ({				\
	typeof((*(ptr))) __x;				\
	void *__ptrcopy;				\
	asm("" : "=r" (__ptrcopy) : "0" (ptr));		\
	memcpy(&__x, __ptrcopy, sizeof(*(ptr)));	\
	__x;						\
})

#define put_unaligned(val, ptr) ({			\
	typeof((*(ptr))) __x = (val);			\
	void *__ptrcopy;				\
	asm("" : "=r" (__ptrcopy) : "0" (ptr));		\
	memcpy(__ptrcopy, &__x, sizeof(*(ptr)));	\
})

extern int handle_misalignment(unsigned long esr0, unsigned long ear0, unsigned long epcr0);

#else

#define get_unaligned(ptr)							\
({										\
	typeof(*(ptr)) x;							\
	const char *__p = (const char *) (ptr);					\
										\
	switch (sizeof(x)) {							\
	case 1:									\
		x = *(ptr);							\
		break;								\
	case 2:									\
	{									\
		uint8_t a;							\
		asm("	ldub%I2		%M2,%0		\n"			\
		    "	ldub%I3.p	%M3,%1		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%1,%0	\n"			\
		    : "=&r"(x), "=&r"(a)					\
		    : "m"(__p[0]),  "m"(__p[1])					\
		    );								\
		break;								\
	}									\
										\
	case 4:									\
	{									\
		uint8_t a;							\
		asm("	ldub%I2		%M2,%0		\n"			\
		    "	ldub%I3.p	%M3,%1		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%1,%0	\n"			\
		    "	ldub%I4.p	%M4,%1		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%1,%0	\n"			\
		    "	ldub%I5.p	%M5,%1		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%1,%0	\n"			\
		    : "=&r"(x), "=&r"(a)					\
		    : "m"(__p[0]),  "m"(__p[1]), "m"(__p[2]), "m"(__p[3])	\
		    );								\
		break;								\
	}									\
										\
	case 8:									\
	{									\
		union { uint64_t x; u32 y[2]; } z;				\
		uint8_t a;							\
		asm("	ldub%I3		%M3,%0		\n"			\
		    "	ldub%I4.p	%M4,%2		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%2,%0	\n"			\
		    "	ldub%I5.p	%M5,%2		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%2,%0	\n"			\
		    "	ldub%I6.p	%M6,%2		\n"			\
		    "	slli		%0,#8,%0	\n"			\
		    "	or		%0,%2,%0	\n"			\
		    "	ldub%I7		%M7,%1		\n"			\
		    "	ldub%I8.p	%M8,%2		\n"			\
		    "	slli		%1,#8,%1	\n"			\
		    "	or		%1,%2,%1	\n"			\
		    "	ldub%I9.p	%M9,%2		\n"			\
		    "	slli		%1,#8,%1	\n"			\
		    "	or		%1,%2,%1	\n"			\
		    "	ldub%I10.p	%M10,%2		\n"			\
		    "	slli		%1,#8,%1	\n"			\
		    "	or		%1,%2,%1	\n"			\
		    : "=&r"(z.y[0]), "=&r"(z.y[1]), "=&r"(a)			\
		    : "m"(__p[0]), "m"(__p[1]), "m"(__p[2]), "m"(__p[3]),	\
		      "m"(__p[4]), "m"(__p[5]), "m"(__p[6]), "m"(__p[7])	\
		    );								\
		x = z.x;							\
		break;								\
	}									\
										\
	default:								\
		x = 0;								\
		BUG();								\
		break;								\
	}									\
										\
	x;									\
})

#define put_unaligned(val, ptr)								\
do {											\
	char *__p = (char *) (ptr);							\
	int x;										\
											\
	switch (sizeof(*ptr)) {								\
	case 2:										\
	{										\
		asm("	stb%I1.p	%0,%M1		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I2		%0,%M2		\n"				\
		    : "=r"(x), "=m"(__p[1]),  "=m"(__p[0])				\
		    : "0"(val)								\
		    );									\
		break;									\
	}										\
											\
	case 4:										\
	{										\
		asm("	stb%I1.p	%0,%M1		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I2.p	%0,%M2		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I3.p	%0,%M3		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I4		%0,%M4		\n"				\
		    : "=r"(x), "=m"(__p[3]),  "=m"(__p[2]), "=m"(__p[1]), "=m"(__p[0])	\
		    : "0"(val)								\
		    );									\
		break;									\
	}										\
											\
	case 8:										\
	{										\
		uint32_t __high, __low;							\
		__high = (uint64_t)val >> 32;						\
		__low = val & 0xffffffff;						\
		asm("	stb%I2.p	%0,%M2		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I3.p	%0,%M3		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I4.p	%0,%M4		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I5.p	%0,%M5		\n"				\
		    "	srli		%0,#8,%0	\n"				\
		    "	stb%I6.p	%1,%M6		\n"				\
		    "	srli		%1,#8,%1	\n"				\
		    "	stb%I7.p	%1,%M7		\n"				\
		    "	srli		%1,#8,%1	\n"				\
		    "	stb%I8.p	%1,%M8		\n"				\
		    "	srli		%1,#8,%1	\n"				\
		    "	stb%I9		%1,%M9		\n"				\
		    : "=&r"(__low), "=&r"(__high), "=m"(__p[7]), "=m"(__p[6]), 		\
		      "=m"(__p[5]), "=m"(__p[4]), "=m"(__p[3]), "=m"(__p[2]), 		\
		      "=m"(__p[1]), "=m"(__p[0])					\
		    : "0"(__low), "1"(__high)						\
		    );									\
		break;									\
	}										\
											\
        default:									\
		*(ptr) = (val);								\
		break;									\
	}										\
} while(0)

#endif

#endif
