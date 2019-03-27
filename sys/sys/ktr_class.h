/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from BSDI $Id: ktr.h,v 1.10.2.7 2000/03/16 21:44:42 cp Exp $
 * $FreeBSD$
 */

#ifndef _SYS_KTR_CLASS_H_
#define _SYS_KTR_CLASS_H_

/*
 * KTR trace classes
 *
 * Two of the trace classes (KTR_DEV and KTR_SUBSYS) are special in that
 * they are really placeholders so that indvidual drivers and subsystems
 * can map their internal tracing to the general class when they wish to
 * have tracing enabled and map it to 0 when they don't.
 */
#define	KTR_GEN		0x00000001		/* General (TR) */
#define	KTR_NET		0x00000002		/* Network */
#define	KTR_DEV		0x00000004		/* Device driver */
#define	KTR_LOCK	0x00000008		/* MP locking */
#define	KTR_SMP		0x00000010		/* MP general */
#define	KTR_SUBSYS	0x00000020		/* Subsystem. */
#define	KTR_PMAP	0x00000040		/* Pmap tracing */
#define	KTR_MALLOC	0x00000080		/* Malloc tracing */
#define	KTR_TRAP	0x00000100		/* Trap processing */
#define	KTR_INTR	0x00000200		/* Interrupt tracing */
#define	KTR_SIG		0x00000400		/* Signal processing */
#define	KTR_SPARE2	0x00000800		/* cxgb, amd64, xen, clk, &c */
#define	KTR_PROC	0x00001000		/* Process scheduling */
#define	KTR_SYSC	0x00002000		/* System call */
#define	KTR_INIT	0x00004000		/* System initialization */
#define	KTR_SPARE3	0x00008000		/* cxgb, drm2, ioat, ntb */
#define	KTR_SPARE4	0x00010000		/* geom_sched */
#define	KTR_EVH		0x00020000		/* Eventhandler */
#define	KTR_VFS		0x00040000		/* VFS events */
#define	KTR_VOP		0x00080000		/* Auto-generated vop events */
#define	KTR_VM		0x00100000		/* The virtual memory system */
#define	KTR_INET	0x00200000		/* IPv4 stack */
#define	KTR_RUNQ	0x00400000		/* Run queue */
#define	KTR_SPARE5	0x00800000
#define	KTR_UMA		0x01000000		/* UMA slab allocator */
#define	KTR_CALLOUT	0x02000000		/* Callouts and timeouts */
#define	KTR_GEOM	0x04000000		/* GEOM I/O events */
#define	KTR_BUSDMA	0x08000000		/* busdma(9) events */
#define	KTR_INET6	0x10000000		/* IPv6 stack */
#define	KTR_SCHED	0x20000000		/* Machine parsed sched info. */
#define	KTR_BUF		0x40000000		/* Buffer cache */
#define	KTR_PTRACE	0x80000000		/* Process debugging. */
#define	KTR_ALL		0xffffffff

/* KTR trace classes to compile in */
#ifdef KTR
#ifndef KTR_COMPILE
#define	KTR_COMPILE	(KTR_ALL)
#endif
#else	/* !KTR */
#undef KTR_COMPILE
#define KTR_COMPILE 0
#endif	/* KTR */

#endif /* !_SYS_KTR_CLASS_H_ */
