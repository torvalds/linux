/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _IA32_SYS_STACK_H
#define	_IA32_SYS_STACK_H

#if !defined(_ASM)

#include <sys/types.h>

#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * In the x86 world, a stack frame looks like this:
 *
 *		|--------------------------|
 * 4n+8(%ebp) ->| argument word n	   |
 *		| ...			   |	(Previous frame)
 *    8(%ebp) ->| argument word 0	   |
 *		|--------------------------|--------------------
 *    4(%ebp) ->| return address	   |
 *		|--------------------------|
 *    0(%ebp) ->| previous %ebp (optional) |
 * 		|--------------------------|
 *   -4(%ebp) ->| unspecified		   |	(Current frame)
 *		| ...			   |
 *    0(%esp) ->| variable size		   |
 * 		|--------------------------|
 */

/*
 * Stack alignment macros.
 */

#define	STACK_ALIGN32		4
#define	STACK_ENTRY_ALIGN32	4
#define	STACK_BIAS32		0
#define	SA32(x)			(((x)+(STACK_ALIGN32-1)) & ~(STACK_ALIGN32-1))
#define	STACK_RESERVE32		0
#define	MINFRAME32		0

#if defined(__amd64)

/*
 * In the amd64 world, a stack frame looks like this:
 *
 *		|--------------------------|
 * 8n+16(%rbp)->| argument word n	   |
 *		| ...			   |	(Previous frame)
 *   16(%rbp) ->| argument word 0	   |
 *		|--------------------------|--------------------
 *    8(%rbp) ->| return address	   |
 *		|--------------------------|
 *    0(%rbp) ->| previous %rbp            |
 * 		|--------------------------|
 *   -8(%rbp) ->| unspecified		   |	(Current frame)
 *		| ...			   |
 *    0(%rsp) ->| variable size		   |
 * 		|--------------------------|
 * -128(%rsp) ->| reserved for function	   |
 * 		|--------------------------|
 *
 * The end of the input argument area must be aligned on a 16-byte
 * boundary; i.e. (%rsp - 8) % 16 == 0 at function entry.
 *
 * The 128-byte location beyond %rsp is considered to be reserved for
 * functions and is NOT modified by signal handlers.  It can be used
 * to store temporary data that is not needed across function calls.
 */

/*
 * Stack alignment macros.
 */

#define	STACK_ALIGN64		16
#define	STACK_ENTRY_ALIGN64 	8
#define	STACK_BIAS64		0
#define	SA64(x)			(((x)+(STACK_ALIGN64-1)) & ~(STACK_ALIGN64-1))
#define	STACK_RESERVE64		128
#define	MINFRAME64		0

#define	STACK_ALIGN		STACK_ALIGN64
#define	STACK_ENTRY_ALIGN	STACK_ENTRY_ALIGN64
#define	STACK_BIAS		STACK_BIAS64
#define	SA(x)			SA64(x)
#define	STACK_RESERVE		STACK_RESERVE64
#define	MINFRAME		MINFRAME64

#elif defined(__i386)

#define	STACK_ALIGN		STACK_ALIGN32
#define	STACK_ENTRY_ALIGN	STACK_ENTRY_ALIGN32
#define	STACK_BIAS		STACK_BIAS32
#define	SA(x)			SA32(x)
#define	STACK_RESERVE		STACK_RESERVE32
#define	MINFRAME		MINFRAME32

#endif	/* __i386 */

#if defined(_KERNEL) && !defined(_ASM)

#if defined(DEBUG)
#if STACK_ALIGN == 4
#define	ASSERT_STACK_ALIGNED()						\
	{								\
		uint32_t __tmp;						\
		ASSERT((((uintptr_t)&__tmp) & (STACK_ALIGN - 1)) == 0);	\
	}
#elif (STACK_ALIGN == 16) && (_LONG_DOUBLE_ALIGNMENT == 16)
#define	ASSERT_STACK_ALIGNED()						\
	{								\
		long double __tmp;					\
		ASSERT((((uintptr_t)&__tmp) & (STACK_ALIGN - 1)) == 0);	\
	}
#endif
#else	/* DEBUG */
#define	ASSERT_STACK_ALIGNED()
#endif	/* DEBUG */

struct regs;

void traceregs(struct regs *);
void traceback(caddr_t);

#endif /* defined(_KERNEL) && !defined(_ASM) */

#define	STACK_GROWTH_DOWN /* stacks grow from high to low addresses */

#ifdef	__cplusplus
}
#endif

#endif	/* _IA32_SYS_STACK_H */
