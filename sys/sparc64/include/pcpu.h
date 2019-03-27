/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Luoqi Chen <luoqi@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/i386/include/globaldata.h,v 1.27 2001/04/27
 * $FreeBSD$
 */

#ifndef	_MACHINE_PCPU_H_
#define	_MACHINE_PCPU_H_

#include <machine/asmacros.h>
#include <machine/cache.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>

#define	ALT_STACK_SIZE	128

struct pmap;

/*
 * Inside the kernel, the globally reserved register g7 is used to
 * point at the globaldata structure.
 */
#define	PCPU_MD_FIELDS							\
	struct	cacheinfo pc_cache;					\
	struct	intr_request pc_irpool[IR_FREE];			\
	struct	intr_request *pc_irhead;				\
	struct	intr_request **pc_irtail;				\
	struct	intr_request *pc_irfree;				\
	struct	pmap *pc_pmap;						\
	vm_offset_t pc_addr;						\
	vm_offset_t pc_qmap_addr;					\
	u_long	pc_tickref;						\
	u_long	pc_tickadj;						\
	u_long	pc_tickincrement;					\
	u_int	pc_clock;						\
	u_int	pc_impl;						\
	u_int	pc_mid;							\
	u_int	pc_node;						\
	u_int	pc_tlb_ctx;						\
	u_int	pc_tlb_ctx_max;						\
	u_int	pc_tlb_ctx_min;						\
	char	__pad[653]

#ifdef _KERNEL

extern void *dpcpu0;

struct pcb;
struct pcpu;

register struct pcb *curpcb __asm__(__XSTRING(PCB_REG));
register struct pcpu *pcpup __asm__(__XSTRING(PCPU_REG));

#define	get_pcpu()		(pcpup)
#define	PCPU_GET(member)	(pcpup->pc_ ## member)

static __inline __pure2 struct thread *
__curthread(void)
{
	struct thread *td;

	__asm("ldx [%" __XSTRING(PCPU_REG) "], %0" : "=r" (td));
	return (td);
}
#define	curthread	(__curthread())

/*
 * XXX The implementation of this operation should be made atomic
 * with respect to preemption.
 */
#define	PCPU_ADD(member, value)	(pcpup->pc_ ## member += (value))
#define	PCPU_INC(member)	PCPU_ADD(member, 1)
#define	PCPU_PTR(member)	(&pcpup->pc_ ## member)
#define	PCPU_SET(member,value)	(pcpup->pc_ ## member = (value))

#endif	/* _KERNEL */

#endif	/* !_MACHINE_PCPU_H_ */
