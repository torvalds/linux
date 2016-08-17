/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

/*
 * Available Solaris debug functions.  All of the ASSERT() macros will be
 * compiled out when NDEBUG is defined, this is the default behavior for
 * the SPL.  To enable assertions use the --enable-debug with configure.
 * The VERIFY() functions are never compiled out and cannot be disabled.
 *
 * PANIC()	- Panic the node and print message.
 * ASSERT()	- Assert X is true, if not panic.
 * ASSERTV()	- Wraps a variable declaration which is only used by ASSERT().
 * ASSERT3S()	- Assert signed X OP Y is true, if not panic.
 * ASSERT3U()	- Assert unsigned X OP Y is true, if not panic.
 * ASSERT3P()	- Assert pointer X OP Y is true, if not panic.
 * ASSERT0()	- Assert value is zero, if not panic.
 * VERIFY()	- Verify X is true, if not panic.
 * VERIFY3S()	- Verify signed X OP Y is true, if not panic.
 * VERIFY3U()	- Verify unsigned X OP Y is true, if not panic.
 * VERIFY3P()	- Verify pointer X OP Y is true, if not panic.
 * VERIFY0()	- Verify value is zero, if not panic.
 */

#ifndef _SPL_DEBUG_H
#define	_SPL_DEBUG_H

/*
 * Common DEBUG functionality.
 */
int spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...);
void spl_dumpstack(void);

#define	PANIC(fmt, a...)						\
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)							\
	(void)(unlikely(!(cond)) &&					\
	    spl_panic(__FILE__, __FUNCTION__, __LINE__,			\
	    "%s", "VERIFY(" #cond ") failed\n"))

#define	VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)			\
	(void)((!((TYPE)(LEFT) OP (TYPE)(RIGHT))) &&			\
	    spl_panic(__FILE__, __FUNCTION__, __LINE__,			\
	    "VERIFY3(" #LEFT " " #OP " " #RIGHT ") "			\
	    "failed (" FMT " " #OP " " FMT ")\n",			\
	    CAST (LEFT), CAST (RIGHT)))

#define	VERIFY3S(x,y,z)	VERIFY3_IMPL(x, y, z, int64_t, "%lld", (long long))
#define	VERIFY3U(x,y,z)	VERIFY3_IMPL(x, y, z, uint64_t, "%llu",		\
				    (unsigned long long))
#define	VERIFY3P(x,y,z)	VERIFY3_IMPL(x, y, z, uintptr_t, "%p", (void *))
#define	VERIFY0(x)	VERIFY3_IMPL(0, ==, x, int64_t, "%lld",	(long long))

#define	CTASSERT_GLOBAL(x)		_CTASSERT(x, __LINE__)
#define	CTASSERT(x)			{ _CTASSERT(x, __LINE__); }
#define	_CTASSERT(x, y)			__CTASSERT(x, y)
#define	__CTASSERT(x, y)						\
	typedef char __attribute__ ((unused))				\
	__compile_time_assertion__ ## y[(x) ? 1 : -1]

/*
 * Debugging disabled (--disable-debug)
 */
#ifdef NDEBUG

#define	SPL_DEBUG_STR		""
#define	ASSERT(x)		((void)0)
#define	ASSERTV(x)
#define	ASSERT3S(x,y,z)		((void)0)
#define	ASSERT3U(x,y,z)		((void)0)
#define	ASSERT3P(x,y,z)		((void)0)
#define	ASSERT0(x)		((void)0)
#define	IMPLY(A, B)		((void)0)
#define	EQUIV(A, B)		((void)0)

/*
 * Debugging enabled (--enable-debug)
 */
#else

#define	SPL_DEBUG_STR		" (DEBUG mode)"
#define	ASSERT(cond)		VERIFY(cond)
#define	ASSERTV(x)		x
#define	ASSERT3S(x,y,z)		VERIFY3S(x, y, z)
#define	ASSERT3U(x,y,z)		VERIFY3U(x, y, z)
#define	ASSERT3P(x,y,z)		VERIFY3P(x, y, z)
#define	ASSERT0(x)		VERIFY0(x)
#define	IMPLY(A, B) \
	((void)(((!(A)) || (B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") implies (" #B ")")))
#define	EQUIV(A, B) \
	((void)((!!(A) == !!(B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") is equivalent to (" #B ")")))

#endif /* NDEBUG */

#endif /* SPL_DEBUG_H */
