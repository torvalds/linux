/*	$OpenBSD: pcb.h,v 1.25 2022/02/21 14:26:19 jsg Exp $	*/
/*	$NetBSD: pcb.h,v 1.21 1996/01/08 13:51:42 mycroft Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)pcb.h	5.10 (Berkeley) 5/12/91
 */

/*
 * Intel 386 process control block
 */

#ifndef _MACHINE_PCB_H_
#define _MACHINE_PCB_H_

#include <sys/signal.h>

#include <machine/segments.h>
#include <machine/npx.h>
#include <machine/sysarch.h>

/*
 * Please note that pcb_savefpu must be aligned to 16 bytes.
 */
struct pcb {
	union	savefpu pcb_savefpu;	/* floating point state for FPU */
	int	pcb_cr3;
	int	pcb_esp;
	int	pcb_ebp;
	int	pcb_kstack;		/* kernel stack address */
	int	pcb_cr0;		/* saved image of CR0 */
	caddr_t	pcb_onfault;		/* copyin/out fault recovery */
	struct	segment_descriptor pcb_threadsegs[2];
					/* per-thread descriptors */
	int	vm86_eflags;		/* virtual eflags for vm86 mode */
	int	vm86_flagmask;		/* flag mask for vm86 mode */
	void	*vm86_userp;		/* XXX performance hack */
	struct  pmap *pcb_pmap;         /* back pointer to our pmap */
	struct	cpu_info *pcb_fpcpu;	/* cpu holding our fpu state */
	int	pcb_flags;
#define PCB_SAVECTX	0x00000001
};

/* the indexes of the %fs/%gs segments in pcb_threadsegs */
#define	TSEG_FS		0
#define	TSEG_GS		1

#endif /* _MACHINE_PCB_H_ */
