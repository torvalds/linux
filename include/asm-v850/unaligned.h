/*
 * include/asm-v850/unaligned.h -- Unaligned memory access
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file is a copy of the arm version, include/asm-arm/unaligned.h
 *
 * Note that some v850 chips support unaligned access, but it seems too
 * annoying to use.
 */

#ifndef __V850_UNALIGNED_H__
#define __V850_UNALIGNED_H__

#include <asm/types.h>

extern int __bug_unaligned_x(void *ptr);

/*
 * What is the most efficient way of loading/storing an unaligned value?
 *
 * That is the subject of this file.  Efficiency here is defined as
 * minimum code size with minimum register usage for the common cases.
 * It is currently not believed that long longs are common, so we
 * trade efficiency for the chars, shorts and longs against the long
 * longs.
 *
 * Current stats with gcc 2.7.2.2 for these functions:
 *
 *	ptrsize	get:	code	regs	put:	code	regs
 *	1		1	1		1	2
 *	2		3	2		3	2
 *	4		7	3		7	3
 *	8		20	6		16	6
 *
 * gcc 2.95.1 seems to code differently:
 *
 *	ptrsize	get:	code	regs	put:	code	regs
 *	1		1	1		1	2
 *	2		3	2		3	2
 *	4		7	4		7	4
 *	8		19	8		15	6
 *
 * which may or may not be more efficient (depending upon whether
 * you can afford the extra registers).  Hopefully the gcc 2.95
 * is inteligent enough to decide if it is better to use the
 * extra register, but evidence so far seems to suggest otherwise.
 *
 * Unfortunately, gcc is not able to optimise the high word
 * out of long long >> 32, or the low word from long long << 32
 */

#define __get_unaligned_2(__p)					\
	(__p[0] | __p[1] << 8)

#define __get_unaligned_4(__p)					\
	(__p[0] | __p[1] << 8 | __p[2] << 16 | __p[3] << 24)

#define get_unaligned(ptr)					\
	({							\
		__typeof__(*(ptr)) __v;				\
		__u8 *__p = (__u8 *)(ptr);			\
		switch (sizeof(*(ptr))) {			\
		case 1:	__v = *(ptr);			break;	\
		case 2: __v = __get_unaligned_2(__p);	break;	\
		case 4: __v = __get_unaligned_4(__p);	break;	\
		case 8: {					\
				unsigned int __v1, __v2;	\
				__v2 = __get_unaligned_4((__p+4)); \
				__v1 = __get_unaligned_4(__p);	\
				__v = ((unsigned long long)__v2 << 32 | __v1);	\
			}					\
			break;					\
		default: __v = __bug_unaligned_x(__p);	break;	\
		}						\
		__v;						\
	})


extern inline void __put_unaligned_2(__u32 __v, register __u8 *__p)
{
	*__p++ = __v;
	*__p++ = __v >> 8;
}

extern inline void __put_unaligned_4(__u32 __v, register __u8 *__p)
{
	__put_unaligned_2(__v >> 16, __p + 2);
	__put_unaligned_2(__v, __p);
}

extern inline void __put_unaligned_8(const unsigned long long __v, register __u8 *__p)
{
	/*
	 * tradeoff: 8 bytes of stack for all unaligned puts (2
	 * instructions), or an extra register in the long long
	 * case - go for the extra register.
	 */
	__put_unaligned_4(__v >> 32, __p+4);
	__put_unaligned_4(__v, __p);
}

/*
 * Try to store an unaligned value as efficiently as possible.
 */
#define put_unaligned(val,ptr)					\
	({							\
		switch (sizeof(*(ptr))) {			\
		case 1:						\
			*(ptr) = (val);				\
			break;					\
		case 2: __put_unaligned_2((val),(__u8 *)(ptr));	\
			break;					\
		case 4:	__put_unaligned_4((val),(__u8 *)(ptr));	\
			break;					\
		case 8:	__put_unaligned_8((val),(__u8 *)(ptr)); \
			break;					\
		default: __bug_unaligned_x(ptr);		\
			break;					\
		}						\
		(void) 0;					\
	})


#endif /* __V850_UNALIGNED_H__ */
