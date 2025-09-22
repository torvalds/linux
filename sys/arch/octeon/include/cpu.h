/* $OpenBSD: cpu.h,v 1.12 2018/02/18 14:50:08 visa Exp $ */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
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
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 *	from: @(#)cpu.h	8.4 (Berkeley) 1/4/94
 */

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

#ifdef _KERNEL

#define OCTEON_MAXCPUS	16

#if defined(MULTIPROCESSOR) && !defined(_LOCORE)
#define MAXCPUS OCTEON_MAXCPUS
struct cpu_info;
void hw_cpu_boot_secondary(struct cpu_info *);
void hw_cpu_hatch(struct cpu_info *);
void hw_cpu_spinup_trampoline(struct cpu_info *);
int  hw_ipi_intr_establish(int (*)(void *), u_long);
void hw_ipi_intr_set(u_long);
void hw_ipi_intr_clear(u_long);

/* Keep in sync with HW_GET_CPU_INFO(). */
static inline struct cpu_info *
hw_getcurcpu(void)
{
	struct cpu_info *ci;
	__asm__ volatile ("dmfc0 %0, $30" /* ErrorEPC */ : "=r" (ci));
	return ci;
}

static inline void
hw_setcurcpu(struct cpu_info *ci)
{
	__asm__ volatile ("dmtc0 %0, $30" /* ErrorEPC */ : : "r" (ci));
}
#endif	/* MULTIPROCESSOR && !_LOCORE */

#define CACHELINESIZE 128

/*
 * No need to use the per-cpu_info function pointers, as we only support
 * one processor type.
 */
#define	Mips_SyncCache(ci)			\
	Octeon_SyncCache((ci))
#define	Mips_InvalidateICache(ci, va, l)	\
	Octeon_InvalidateICache((ci), (va), (l))
#define	Mips_InvalidateICachePage(ci, va)	\
	Octeon_InvalidateICachePage((ci), (va))
#define	Mips_SyncICache(ci)			\
	Octeon_SyncICache((ci))
#define	Mips_SyncDCachePage(ci, va, pa)		\
	Octeon_SyncDCachePage((ci), (va), (pa))
#define	Mips_HitSyncDCachePage(ci, va, pa)	\
	Octeon_SyncDCachePage((ci), (va), (pa))
#define	Mips_HitSyncDCache(ci, va, l)		\
	Octeon_HitSyncDCache((ci), (va), (l))
#define	Mips_IOSyncDCache(ci, va, l, h)		\
	Octeon_IOSyncDCache((ci), (va), (l), (h))
#define	Mips_HitInvalidateDCache(ci, va, l)	\
	Octeon_HitInvalidateDCache((ci), (va), (l))

#endif/* _KERNEL */

#include <mips64/cpu.h>

#endif /* !_MACHINE_CPU_H_ */
