/*	$OpenBSD: vmmeter.h,v 1.15 2016/07/27 14:44:59 tedu Exp $	*/
/*	$NetBSD: vmmeter.h,v 1.9 1995/03/26 20:25:04 jtc Exp $	*/

/*-
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
 *	@(#)vmmeter.h	8.2 (Berkeley) 7/10/94
 */

#ifndef	__VMMETER_H__
#define	__VMMETER_H__

/*
 * System wide statistics counters.  Look in <uvm/uvm_extern.h> for the
 * UVM equivalent.
 */

/* systemwide totals computed every five seconds */
struct vmtotal {
	u_int16_t t_rq;		/* length of the run queue */
	u_int16_t t_dw;		/* jobs in ``disk wait'' (neg priority) */
	u_int16_t t_pw;		/* jobs in page wait */
	u_int16_t t_sl;		/* jobs sleeping in core */
	u_int16_t t_sw;		/* swapped out runnable/short block jobs */
	u_int32_t t_vm;		/* total virtual memory */
	u_int32_t t_avm;	/* active virtual memory */
	u_int32_t t_rm;		/* total real memory in use */
	u_int32_t t_arm;	/* active real memory */
	u_int32_t t_vmshr;	/* shared virtual memory */
	u_int32_t t_avmshr;	/* active shared virtual memory */
	u_int32_t t_rmshr;	/* shared real memory */
	u_int32_t t_armshr;	/* active shared real memory */
	u_int32_t t_free;	/* free memory pages */
};

/*
 * Fork/vfork/__tfork accounting.
 */
struct  forkstat {
	uint32_t	cntfork;	/* number of fork() calls */
	uint32_t	cntvfork;	/* number of vfork() calls */
	uint32_t	cnttfork;	/* number of __tfork() calls */
	uint32_t	cntkthread;	/* number of kernel threads created */
	uint64_t	sizfork;	/* VM pages affected by fork() */
	uint64_t	sizvfork;	/* VM pages affected by vfork() */
	uint64_t	siztfork;	/* VM pages affected by __tfork() */
	uint64_t	sizkthread;	/* VM pages affected by kernel threads */
};

/* These sysctl names are only really used by sysctl(8) */
#define KERN_FORKSTAT_FORK		1
#define KERN_FORKSTAT_VFORK		2
#define KERN_FORKSTAT_TFORK		3
#define KERN_FORKSTAT_KTHREAD		4
#define KERN_FORKSTAT_SIZFORK		5
#define KERN_FORKSTAT_SIZVFORK		6
#define KERN_FORKSTAT_SIZTFORK		7
#define KERN_FORKSTAT_SIZKTHREAD	8
#define KERN_FORKSTAT_MAXID		9

#define CTL_KERN_FORKSTAT_NAMES { \
	{ 0, 0 }, \
	{ "forks", CTLTYPE_INT }, \
	{ "vforks", CTLTYPE_INT }, \
	{ "tforks", CTLTYPE_INT }, \
	{ "kthreads", CTLTYPE_INT }, \
	{ "fork_pages", CTLTYPE_INT }, \
	{ "vfork_pages", CTLTYPE_INT }, \
	{ "tfork_pages", CTLTYPE_INT }, \
	{ "kthread_pages", CTLTYPE_INT }, \
}
#endif /* __VMMETER_H__ */
