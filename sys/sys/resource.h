/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)resource.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_RESOURCE_H_
#define	_SYS_RESOURCE_H_

#include <sys/cdefs.h>
#include <sys/_timeval.h>
#include <sys/_types.h>

#ifndef _ID_T_DECLARED
typedef	__id_t		id_t;
#define	_ID_T_DECLARED
#endif

#ifndef _RLIM_T_DECLARED
typedef	__rlim_t	rlim_t;
#define	_RLIM_T_DECLARED
#endif

/*
 * Process priority specifications to get/setpriority.
 */
#define	PRIO_MIN	-20
#define	PRIO_MAX	20

#define	PRIO_PROCESS	0
#define	PRIO_PGRP	1
#define	PRIO_USER	2

/*
 * Resource utilization information.
 *
 * All fields are only modified by curthread and
 * no locks are required to read.
 */

#define	RUSAGE_SELF	0
#define	RUSAGE_CHILDREN	-1
#define	RUSAGE_THREAD	1

struct rusage {
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	long	ru_maxrss;		/* max resident set size */
#define	ru_first	ru_ixrss
	long	ru_ixrss;		/* integral shared memory size */
	long	ru_idrss;		/* integral unshared data " */
	long	ru_isrss;		/* integral unshared stack " */
	long	ru_minflt;		/* page reclaims */
	long	ru_majflt;		/* page faults */
	long	ru_nswap;		/* swaps */
	long	ru_inblock;		/* block input operations */
	long	ru_oublock;		/* block output operations */
	long	ru_msgsnd;		/* messages sent */
	long	ru_msgrcv;		/* messages received */
	long	ru_nsignals;		/* signals received */
	long	ru_nvcsw;		/* voluntary context switches */
	long	ru_nivcsw;		/* involuntary " */
#define	ru_last		ru_nivcsw
};

#if __BSD_VISIBLE
struct __wrusage {
	struct rusage	wru_self;
	struct rusage	wru_children;
};
#endif

/*
 * Resource limits
 */
#define	RLIMIT_CPU	0		/* maximum cpu time in seconds */
#define	RLIMIT_FSIZE	1		/* maximum file size */
#define	RLIMIT_DATA	2		/* data size */
#define	RLIMIT_STACK	3		/* stack size */
#define	RLIMIT_CORE	4		/* core file size */
#define	RLIMIT_RSS	5		/* resident set size */
#define	RLIMIT_MEMLOCK	6		/* locked-in-memory address space */
#define	RLIMIT_NPROC	7		/* number of processes */
#define	RLIMIT_NOFILE	8		/* number of open files */
#define	RLIMIT_SBSIZE	9		/* maximum size of all socket buffers */
#define	RLIMIT_VMEM	10		/* virtual process size (incl. mmap) */
#define	RLIMIT_AS	RLIMIT_VMEM	/* standard name for RLIMIT_VMEM */
#define	RLIMIT_NPTS	11		/* pseudo-terminals */
#define	RLIMIT_SWAP	12		/* swap used */
#define	RLIMIT_KQUEUES	13		/* kqueues allocated */
#define	RLIMIT_UMTXP	14		/* process-shared umtx */

#define	RLIM_NLIMITS	15		/* number of resource limits */

#define	RLIM_INFINITY	((rlim_t)(((__uint64_t)1 << 63) - 1))
#define	RLIM_SAVED_MAX	RLIM_INFINITY
#define	RLIM_SAVED_CUR	RLIM_INFINITY

/*
 * Resource limit string identifiers
 */

#ifdef _RLIMIT_IDENT
static const char *rlimit_ident[RLIM_NLIMITS] = {
	"cpu",
	"fsize",
	"data",
	"stack",
	"core",
	"rss",
	"memlock",
	"nproc",
	"nofile",
	"sbsize",
	"vmem",
	"npts",
	"swap",
	"kqueues",
	"umtx",
};
#endif

struct rlimit {
	rlim_t	rlim_cur;		/* current (soft) limit */
	rlim_t	rlim_max;		/* maximum value for rlim_cur */
};

#if __BSD_VISIBLE

struct orlimit {
	__int32_t	rlim_cur;	/* current (soft) limit */
	__int32_t	rlim_max;	/* maximum value for rlim_cur */
};

struct loadavg {
	__fixpt_t	ldavg[3];
	long		fscale;
};

#define	CP_USER		0
#define	CP_NICE		1
#define	CP_SYS		2
#define	CP_INTR		3
#define	CP_IDLE		4
#define	CPUSTATES	5

#endif	/* __BSD_VISIBLE */

#ifdef _KERNEL

extern struct loadavg averunnable;
void	read_cpu_time(long *cp_time);	/* Writes array of CPUSTATES */

#else

__BEGIN_DECLS
/* XXX 2nd arg to [gs]etpriority() should be an id_t */
int	getpriority(int, int);
int	getrlimit(int, struct rlimit *);
int	getrusage(int, struct rusage *);
int	setpriority(int, int, int);
int	setrlimit(int, const struct rlimit *);
__END_DECLS

#endif	/* _KERNEL */
#endif	/* !_SYS_RESOURCE_H_ */
