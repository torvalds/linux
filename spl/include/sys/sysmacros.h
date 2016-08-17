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

#ifndef _SPL_SYSMACROS_H
#define	_SPL_SYSMACROS_H

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <sys/debug.h>
#include <sys/varargs.h>
#include <sys/zone.h>
#include <sys/signal.h>
#include <asm/page.h>

#ifdef HAVE_SCHED_RT_HEADER
#include <linux/sched/rt.h>
#endif

#ifndef _KERNEL
#define	_KERNEL				__KERNEL__
#endif

#define	FALSE				0
#define	TRUE				1

#define	INT8_MAX			(127)
#define	INT8_MIN			(-128)
#define	UINT8_MAX			(255)
#define	UINT8_MIN			(0)

#define	INT16_MAX			(32767)
#define	INT16_MIN			(-32768)
#define	UINT16_MAX			(65535)
#define	UINT16_MIN			(0)

#define	INT32_MAX			INT_MAX
#define	INT32_MIN			INT_MIN
#define	UINT32_MAX			UINT_MAX
#define	UINT32_MIN			UINT_MIN

#define	INT64_MAX			LLONG_MAX
#define	INT64_MIN			LLONG_MIN
#define	UINT64_MAX			ULLONG_MAX
#define	UINT64_MIN			ULLONG_MIN

#define	NBBY				8
#define	ENOTSUP				EOPNOTSUPP

#define	MAXMSGLEN			256
#define	MAXNAMELEN			256
#define	MAXPATHLEN			PATH_MAX
#define	MAXOFFSET_T			LLONG_MAX
#define	MAXBSIZE			8192
#define	DEV_BSIZE			512
#define	DEV_BSHIFT			9 /* log2(DEV_BSIZE) */

#define	proc_pageout			NULL
#define	curproc				current
#define	max_ncpus			num_possible_cpus()
#define	boot_ncpus			num_online_cpus()
#define	CPU_SEQID			smp_processor_id()
#define	_NOTE(x)
#define	is_system_labeled()		0

#ifndef RLIM64_INFINITY
#define	RLIM64_INFINITY			(~0ULL)
#endif

/*
 * 0..MAX_PRIO-1:		Process priority
 * 0..MAX_RT_PRIO-1:		RT priority tasks
 * MAX_RT_PRIO..MAX_PRIO-1:	SCHED_NORMAL tasks
 *
 * Treat shim tasks as SCHED_NORMAL tasks
 */
#define	minclsyspri			(MAX_PRIO-1)
#define	maxclsyspri			(MAX_RT_PRIO)
#define	defclsyspri			(DEFAULT_PRIO)

#ifndef NICE_TO_PRIO
#define	NICE_TO_PRIO(nice)		(MAX_RT_PRIO + (nice) + 20)
#endif
#ifndef PRIO_TO_NICE
#define	PRIO_TO_NICE(prio)		((prio) - MAX_RT_PRIO - 20)
#endif

/*
 * Missing macros
 */
#ifndef PAGESIZE
#define	PAGESIZE			PAGE_SIZE
#endif

#ifndef PAGESHIFT
#define	PAGESHIFT			PAGE_SHIFT
#endif

/* from Solaris sys/byteorder.h */
#define	BSWAP_8(x)	((x) & 0xff)
#define	BSWAP_16(x)	((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define	BSWAP_32(x)	((BSWAP_16(x) << 16) | BSWAP_16((x) >> 16))
#define	BSWAP_64(x)	((BSWAP_32(x) << 32) | BSWAP_32((x) >> 32))

/*
 * Map some simple functions.
 */
#define	bzero(ptr, size)		memset(ptr, 0, size)
#define	bcopy(src, dest, size)		memmove(dest, src, size)
#define	bcmp(src, dest, size)		memcmp((src), (dest), (size_t)(size))

/* Dtrace probes do not exist in the linux kernel */
#ifdef DTRACE_PROBE
#undef  DTRACE_PROBE
#endif  /* DTRACE_PROBE */
#define	DTRACE_PROBE(a)					((void)0)

#ifdef DTRACE_PROBE1
#undef  DTRACE_PROBE1
#endif  /* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)				((void)0)

#ifdef DTRACE_PROBE2
#undef  DTRACE_PROBE2
#endif  /* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)			((void)0)

#ifdef DTRACE_PROBE3
#undef  DTRACE_PROBE3
#endif  /* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)		((void)0)

#ifdef DTRACE_PROBE4
#undef  DTRACE_PROBE4
#endif  /* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)

/* Missing globals */
extern char spl_version[32];
extern unsigned long spl_hostid;

/* Missing misc functions */
extern uint32_t zone_get_hostid(void *zone);
extern void spl_setup(void);
extern void spl_cleanup(void);

#define	highbit(x)		__fls(x)
#define	lowbit(x)		__ffs(x)

#define	highbit64(x)		fls64(x)
#define	makedevice(maj, min)	makedev(maj, min)

/* common macros */
#ifndef MIN
#define	MIN(a, b)		((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)		((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)			((a) < 0 ? -(a) : (a))
#endif
#ifndef DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif
#ifndef roundup
#define	roundup(x, y)		((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef howmany
#define	howmany(x, y)		(((x) + ((y) - 1)) / (y))
#endif

/*
 * Compatibility macros/typedefs needed for Solaris -> Linux port
 */
#define	P2ALIGN(x, align)	((x) & -(align))
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#define	P2ROUNDUP(x, align)	((((x) - 1) | ((align) - 1)) + 1)
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#define	P2NPHASE(x, align)	(-(x) & ((align) - 1))
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#define	P2BOUNDARY(off, len, align) \
				(((off) ^ ((off) + (len) - 1)) > (align) - 1)

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *
 * P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 * P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)   \
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)   \
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)  \
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type) \
	((((type)(x) - 1) | ((type)(align) - 1)) + 1)
#define	P2END_TYPED(x, align, type)     \
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)  \
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)	\
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type) \
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)

/* avoid any possibility of clashing with <stddef.h> version */

#define	offsetof(s, m)  ((size_t)(&(((s *)0)->m)))
#endif

#endif  /* _SPL_SYSMACROS_H */
