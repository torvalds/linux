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

#ifdef _KERNEL

#include <sys/_lock.h>
#include <sys/_mutex.h>

#define	ALT_STACK_SIZE	128

struct vmspace;

#endif	/* _KERNEL */

#if __ARM_ARCH >= 6
/* Branch predictor hardening method */
#define PCPU_BP_HARDEN_KIND_NONE		0
#define PCPU_BP_HARDEN_KIND_BPIALL		1
#define PCPU_BP_HARDEN_KIND_ICIALLU		2

#define PCPU_MD_FIELDS							\
	unsigned int pc_vfpsid;						\
	unsigned int pc_vfpmvfr0;					\
	unsigned int pc_vfpmvfr1;					\
	struct pmap *pc_curpmap;					\
	struct mtx pc_cmap_lock;					\
	void *pc_cmap1_pte2p;						\
	void *pc_cmap2_pte2p;						\
	caddr_t pc_cmap1_addr;						\
	caddr_t pc_cmap2_addr;						\
	vm_offset_t pc_qmap_addr;					\
	void *pc_qmap_pte2p;						\
	unsigned int pc_dbreg[32];					\
	int pc_dbreg_cmd;						\
	int pc_bp_harden_kind;						\
	uint32_t pc_original_actlr;					\
	uint64_t pc_clock;						\
	char __pad[139]
#else
#define PCPU_MD_FIELDS							\
	char __pad[93]
#endif

#ifdef _KERNEL

#define	PC_DBREG_CMD_NONE	0
#define	PC_DBREG_CMD_LOAD	1

struct pcb;
struct pcpu;

extern struct pcpu *pcpup;

#if __ARM_ARCH >= 6
#define CPU_MASK (0xf)

#ifndef SMP
#define get_pcpu() (pcpup)
#else
#define get_pcpu() __extension__ ({			  		\
    	int id;								\
        __asm __volatile("mrc p15, 0, %0, c0, c0, 5" : "=r" (id));	\
    	(pcpup + (id & CPU_MASK));					\
    })
#endif

static inline struct thread *
get_curthread(void)
{
	void *ret;

	__asm __volatile("mrc p15, 0, %0, c13, c0, 4" : "=r" (ret));
	return (ret);
}

static inline void
set_curthread(struct thread *td)
{

	__asm __volatile("mcr p15, 0, %0, c13, c0, 4" : : "r" (td));
}


static inline void *
get_tls(void)
{
	void *tls;

	/* TPIDRURW contains the authoritative value. */
	__asm __volatile("mrc p15, 0, %0, c13, c0, 2" : "=r" (tls));
	return (tls);
}

static inline void
set_tls(void *tls)
{

	/*
	 * Update both TPIDRURW and TPIDRURO. TPIDRURW needs to be written
	 * first to ensure that a context switch between the two writes will
	 * still give the desired result of updating both.
	 */
	__asm __volatile(
	    "mcr p15, 0, %0, c13, c0, 2\n"
	    "mcr p15, 0, %0, c13, c0, 3\n"
	     : : "r" (tls));
}

#define curthread get_curthread()

#else
#define get_pcpu()	pcpup
#endif

#define	PCPU_GET(member)	(get_pcpu()->pc_ ## member)
#define	PCPU_ADD(member, value)	(get_pcpu()->pc_ ## member += (value))
#define	PCPU_INC(member)	PCPU_ADD(member, 1)
#define	PCPU_PTR(member)	(&get_pcpu()->pc_ ## member)
#define	PCPU_SET(member,value)	(get_pcpu()->pc_ ## member = (value))

void pcpu0_init(void);
#endif	/* _KERNEL */

#endif	/* !_MACHINE_PCPU_H_ */
