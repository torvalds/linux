/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

#ifndef _SYS_DEBUG_H
#define	_SYS_DEBUG_H

#include <sys/types.h>
#include <sys/note.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ASSERT(ex) causes a panic or debugger entry if expression ex is not
 * true.  ASSERT() is included only for debugging, and is a no-op in
 * production kernels.  VERIFY(ex), on the other hand, behaves like
 * ASSERT and is evaluated on both debug and non-debug kernels.
 */

extern int assfail(const char *, const char *, int);
#define	VERIFY(EX) ((void)((EX) || assfail(#EX, __FILE__, __LINE__)))
#ifdef DEBUG
#define	ASSERT(EX) ((void)((EX) || assfail(#EX, __FILE__, __LINE__)))
#else
#define	ASSERT(x)  ((void)0)
#endif

/*
 * Assertion variants sensitive to the compilation data model
 */
#if defined(_LP64)
#define	ASSERT64(x)	ASSERT(x)
#define	ASSERT32(x)
#else
#define	ASSERT64(x)
#define	ASSERT32(x)	ASSERT(x)
#endif

/*
 * IMPLY and EQUIV are assertions of the form:
 *
 *	if (a) then (b)
 * and
 *	if (a) then (b) *AND* if (b) then (a)
 */
#ifdef DEBUG
#define	IMPLY(A, B) \
	((void)(((!(A)) || (B)) || \
	    assfail("(" #A ") implies (" #B ")", __FILE__, __LINE__)))
#define	EQUIV(A, B) \
	((void)((!!(A) == !!(B)) || \
	    assfail("(" #A ") is equivalent to (" #B ")", __FILE__, __LINE__)))
#else
#define	IMPLY(A, B) ((void)0)
#define	EQUIV(A, B) ((void)0)
#endif

/*
 * ASSERT3() behaves like ASSERT() except that it is an explicit conditional,
 * and prints out the values of the left and right hand expressions as part of
 * the panic message to ease debugging.  The three variants imply the type
 * of their arguments.  ASSERT3S() is for signed data types, ASSERT3U() is
 * for unsigned, and ASSERT3P() is for pointers.  The VERIFY3*() macros
 * have the same relationship as above.
 */
extern void assfail3(const char *, uintmax_t, const char *, uintmax_t,
    const char *, int);
#define	VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE) do { \
	const TYPE __left = (TYPE)(LEFT); \
	const TYPE __right = (TYPE)(RIGHT); \
	if (!(__left OP __right)) \
		assfail3(#LEFT " " #OP " " #RIGHT, \
			(uintmax_t)__left, #OP, (uintmax_t)__right, \
			__FILE__, __LINE__); \
_NOTE(CONSTCOND) } while (0)

#define	VERIFY3B(x, y, z)	VERIFY3_IMPL(x, y, z, boolean_t)
#define	VERIFY3S(x, y, z)	VERIFY3_IMPL(x, y, z, int64_t)
#define	VERIFY3U(x, y, z)	VERIFY3_IMPL(x, y, z, uint64_t)
#define	VERIFY3P(x, y, z)	VERIFY3_IMPL(x, y, z, uintptr_t)
#define	VERIFY0(x)		VERIFY3_IMPL(x, ==, 0, uintmax_t)

#ifdef DEBUG
#define	ASSERT3B(x, y, z)	VERIFY3_IMPL(x, y, z, boolean_t)
#define	ASSERT3S(x, y, z)	VERIFY3_IMPL(x, y, z, int64_t)
#define	ASSERT3U(x, y, z)	VERIFY3_IMPL(x, y, z, uint64_t)
#define	ASSERT3P(x, y, z)	VERIFY3_IMPL(x, y, z, uintptr_t)
#define	ASSERT0(x)		VERIFY3_IMPL(x, ==, 0, uintmax_t)
#else
#define	ASSERT3B(x, y, z)	((void)0)
#define	ASSERT3S(x, y, z)	((void)0)
#define	ASSERT3U(x, y, z)	((void)0)
#define	ASSERT3P(x, y, z)	((void)0)
#define	ASSERT0(x)		((void)0)
#endif

/*
 * Compile-time assertion. The condition 'x' must be constant.
 */
#ifndef CTASSERT
#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y) 	\
	_Static_assert((x), "Static assert failed at " #y)
#endif

#ifdef	_KERNEL

extern void abort_sequence_enter(char *);
extern void debug_enter(char *);

#endif	/* _KERNEL */

#if defined(DEBUG) && !defined(__sun)
/* CSTYLED */
#define	STATIC
#else
/* CSTYLED */
#define	STATIC static
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DEBUG_H */
