/*	$NetBSD: pcb.h,v 1.10 2003/10/13 21:46:39 scw Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Matt Thomas <matt@3am-software.com>.
 * Copyright (c) 1994 Mark Brinicombe.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the RiscBSD team.
 * 4. The name "RiscBSD" nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RISCBSD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL RISCBSD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_PCB_H_
#define	_MACHINE_PCB_H_

#include <machine/frame.h>
#include <machine/vfp.h>


/*
 * WARNING!
 * Keep pcb_regs first for faster access in switch.S
 */
struct pcb {
	struct switchframe pcb_regs;		/* CPU state */
	u_int	pcb_flags;
#define	PCB_OWNFPU	0x00000001
#define PCB_NOALIGNFLT	0x00000002
	caddr_t	pcb_onfault;			/* On fault handler */
	vm_offset_t	pcb_pagedir;		/* TTB0 value */
	/*
	 * XXX:
	 * Variables pcb_pl1vec, pcb_l1vec, pcb_dacr are used solely
	 * by old PMAP. Keep them here for PCB binary compatibility
	 * between old and new PMAP.
	 */
	uint32_t *pcb_pl1vec;			/* PTR to vector_base L1 entry*/
	uint32_t pcb_l1vec;			/* Value to stuff on ctx sw */
	u_int	pcb_dacr;			/* Domain Access Control Reg */

	struct vfp_state pcb_vfpstate;          /* VP/NEON state */
	u_int pcb_vfpcpu;                       /* VP/NEON last cpu */
} __aligned(8); /*
		 * We need the PCB to be aligned on 8 bytes, as we may
		 * access it using ldrd/strd, and ARM ABI require it
		 * to by aligned on 8 bytes.
		 */

/*
 * No additional data for core dumps.
 */
struct md_coredump {
	int	md_empty;
};

void	makectx(struct trapframe *tf, struct pcb *pcb);

#ifdef _KERNEL

void    savectx(struct pcb *) __returns_twice;
#endif	/* _KERNEL */

#endif	/* !_MACHINE_PCB_H_ */
