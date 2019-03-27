/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Luoqi Chen <luoqi@freebsd.org>
 * Copyright (c) Peter Wemm <peter@netplex.com.au>
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_PCPU_H_
#define	_MACHINE_PCPU_H_

#include <machine/cpufunc.h>
#include <machine/slb.h>
#include <machine/tlb.h>

struct pmap;
struct pvo_entry;
#define	CPUSAVE_LEN	9

#define	PCPU_MD_COMMON_FIELDS						\
	int		pc_inside_intr;					\
	struct pmap	*pc_curpmap;		/* current pmap */	\
	struct thread	*pc_fputhread;		/* current fpu user */  \
	struct thread	*pc_vecthread;		/* current vec user */  \
	struct thread	*pc_htmthread;		/* current htm user */  \
	uintptr_t	pc_hwref;					\
	int		pc_bsp;						\
	volatile int	pc_awake;					\
	uint32_t	pc_ipimask;					\
	register_t	pc_tempsave[CPUSAVE_LEN];			\
	register_t	pc_disisave[CPUSAVE_LEN];			\
	register_t	pc_dbsave[CPUSAVE_LEN];				\
	void		*pc_restore;					\
	vm_offset_t	pc_qmap_addr;

#define PCPU_MD_AIM32_FIELDS						\
	struct pvo_entry *qmap_pvo;					\
	struct mtx	qmap_lock;					\
	char		__pad[128];

#define PCPU_MD_AIM64_FIELDS						\
	struct slb	slb[64];					\
	struct slb	**userslb;					\
	register_t	slbsave[18];					\
	uint8_t		slbstack[1024];				\
	struct pvo_entry *qmap_pvo;					\
	struct mtx	qmap_lock;					\
	char		__pad[1345];

#ifdef __powerpc64__
#define PCPU_MD_AIM_FIELDS	PCPU_MD_AIM64_FIELDS
#else
#define PCPU_MD_AIM_FIELDS	PCPU_MD_AIM32_FIELDS
#endif

#define	BOOKE_CRITSAVE_LEN	(CPUSAVE_LEN + 2)
#define	BOOKE_TLB_MAXNEST	3
#define	BOOKE_TLB_SAVELEN	16
#define	BOOKE_TLBSAVE_LEN	(BOOKE_TLB_SAVELEN * BOOKE_TLB_MAXNEST)

#ifdef __powerpc64__
#define	BOOKE_PCPU_PAD	901
#else
#define	BOOKE_PCPU_PAD	429
#endif
#define PCPU_MD_BOOKE_FIELDS						\
	register_t	critsave[BOOKE_CRITSAVE_LEN];		\
	register_t	mchksave[CPUSAVE_LEN];			\
	register_t	tlbsave[BOOKE_TLBSAVE_LEN];		\
	register_t	tlb_level;				\
	uintptr_t	*tlb_lock;				\
	int		tid_next;					\
	char		__pad[BOOKE_PCPU_PAD];

/* Definitions for register offsets within the exception tmp save areas */
#define	CPUSAVE_R27	0		/* where r27 gets saved */
#define	CPUSAVE_R28	1		/* where r28 gets saved */
#define	CPUSAVE_R29	2		/* where r29 gets saved */
#define	CPUSAVE_R30	3		/* where r30 gets saved */
#define	CPUSAVE_R31	4		/* where r31 gets saved */
#define	CPUSAVE_AIM_DAR		5	/* where SPR_DAR gets saved */
#define	CPUSAVE_AIM_DSISR	6	/* where SPR_DSISR gets saved */
#define	CPUSAVE_BOOKE_DEAR	5	/* where SPR_DEAR gets saved */
#define	CPUSAVE_BOOKE_ESR	6	/* where SPR_ESR gets saved */
#define	CPUSAVE_SRR0	7		/* where SRR0 gets saved */
#define	CPUSAVE_SRR1	8		/* where SRR1 gets saved */
#define	BOOKE_CRITSAVE_SRR0	9	/* where real SRR0 gets saved (critical) */
#define	BOOKE_CRITSAVE_SRR1	10	/* where real SRR0 gets saved (critical) */

/* Book-E TLBSAVE is more elaborate */
#define TLBSAVE_BOOKE_LR	0
#define TLBSAVE_BOOKE_CR	1
#define TLBSAVE_BOOKE_SRR0	2
#define TLBSAVE_BOOKE_SRR1	3
#define TLBSAVE_BOOKE_R20	4
#define TLBSAVE_BOOKE_R21	5
#define TLBSAVE_BOOKE_R22	6
#define TLBSAVE_BOOKE_R23	7
#define TLBSAVE_BOOKE_R24	8
#define TLBSAVE_BOOKE_R25	9
#define TLBSAVE_BOOKE_R26	10
#define TLBSAVE_BOOKE_R27	11
#define TLBSAVE_BOOKE_R28	12
#define TLBSAVE_BOOKE_R29	13
#define TLBSAVE_BOOKE_R30	14
#define TLBSAVE_BOOKE_R31	15

#define	PCPU_MD_FIELDS		\
	PCPU_MD_COMMON_FIELDS	\
	union {			\
	    struct {		\
		PCPU_MD_AIM_FIELDS	\
	    } pc_aim;		\
	    struct {		\
		PCPU_MD_BOOKE_FIELDS	\
	    } pc_booke;		\
	}

#ifdef _KERNEL

#define pcpup	(get_pcpu())

static __inline __pure2 struct thread *
__curthread(void)
{
	struct thread *td;
#ifdef __powerpc64__
	__asm __volatile("mr %0,13" : "=r"(td));
#else
	__asm __volatile("mr %0,2" : "=r"(td));
#endif
	return (td);
}
#define curthread (__curthread())

#define	PCPU_GET(member)	(pcpup->pc_ ## member)

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
