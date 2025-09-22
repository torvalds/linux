/*	$OpenBSD: pcb.h,v 1.11 2022/10/21 18:55:42 miod Exp $	*/
/*	$NetBSD: pcb.h,v 1.7 2000/12/29 17:12:05 eeh Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)pcb.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _MACHINE_PCB_H_
#define _MACHINE_PCB_H_

#include <machine/reg.h>

#ifdef notyet
#define	PCB_MAXWIN	32	/* architectural limit */
#else
#define	PCB_MAXWIN	8	/* worried about u area sizes ... */
#endif

/*
 * SPARC Process Control Block.
 *
 * pcb_uw is positive if there are any user windows that are
 * are currently in the CPU windows rather than on the user
 * stack.  Whenever we are running in the kernel with traps
 * enabled, we decrement pcb_uw for each ``push'' of a CPU
 * register window into the stack, and we increment it for
 * each ``pull'' from the stack into the CPU.  (If traps are
 * disabled, or if we are in user mode, pcb_uw is junk.)
 *
 * To ease computing pcb_uw on traps from user mode, we keep track
 * of the log base 2 of the single bit that is set in %wim.
 *
 * If an overflow occurs while the associated user stack pages
 * are invalid (paged out), we have to store the registers
 * in a page that is locked in core while the process runs,
 * i.e., right here in the pcb.  We also need the stack pointer
 * for the last such window (but only the last, as the others
 * are in each window) and the count of windows saved.  We
 * cheat by having a whole window structure for that one %sp.
 * Thus, to save window pcb_rw[i] to memory, we write it at
 * pcb_rw[i + 1].rw_in[6].
 *
 * pcb_nsaved has three `kinds' of values.  If 0, it means no
 * registers are in the PCB (though if pcb_uw is positive,
 * there may be the next time you look).  If positive, it means
 * there are no user registers in the CPU, but there are some
 * saved in pcb_rw[].  As a special case, traps that needed
 * assistance to pull user registers from the stack also store
 * the registers in pcb_rw[], and set pcb_nsaved to -1.  This
 * special state is normally short-term: it can only last until the
 * trap returns, and it can never persist across entry to user code.
 */
/*
 * v9 addendum:
 *
 * Window handling between v8 and v9 has changed somewhat.  There
 * is no %wim.  Instead, we have a %cwp, %cansave, %canrestore,
 * %cleanwin, and %otherwin.  By definition:
 *
 *	 %cansave + %canrestore + %otherwin = NWINDOWS - 2
 *
 * In addition, %cleanwin >= %canrestore since restorable windows
 * are considered clean.  This means that by storing %canrestore
 * and %otherwin, we should be able to compute the values of all
 * the other registers.
 *
 * The only other register we need to save is %cwp because it cannot
 * be trivially computed from the other registers.  The %cwp is
 * stored in the %tstate register, but if the machine was in a register
 * window spill/fill handler, the value of that %cwp may be off by 
 * as much as 2 register windows.  We will also store %cwp.  [We will
 * try to steal pcb_uw or pcb_nsaved for this purpose eventually.]
 *
 * To calculate what registers are in the pcb, start with pcb_cwp
 * and proceed to (pcb_cwp - pcb_canrestore) % NWINDOWS.  These should
 * be saved to their appropriate register windows.  The client routine
 * (trap handler) is responsible for saving pcb_cwp + 1 [%o1-%o7] in
 * the trap frame or on the stack.
 *
 *
 * Even more addendum:
 *
 * With the new system for keeping track of register windows we don't
 * care about anything other than pcb_uw which keeps track of how many
 * full windows we have.  As soon as a flush traps, we dump all user
 * windows to the pcb, handle the fault, then restore all user windows.
 *
 * XXX we are using pcb_nsaved as the counter.  pcb_uw is still a mask.  
 * change this as soon as the new scheme is debugged.
 */
struct pcb {
	u_int64_t	pcb_sp;		/* sp (%o6) when switch() was called */
	u_int64_t	pcb_pc;		/* pc (%o7) when switch() was called */
	caddr_t	pcb_onfault;	/* for copyin/out */
	short	pcb_pstate;	/* %pstate when switch() was called -- may be useful if we support multiple memory models */
	char	pcb_nsaved;	/* number of windows saved in pcb */

	/* The rest is probably not needed except for pcb_rw */
	char	pcb_cwp;	/* %cwp when switch() was called */
	char	pcb_pil;	/* %pil when switch() was called -- probably not needed */

	const char *lastcall;	/* DEBUG -- name of last system call */
	u_int64_t	pcb_wcookie;

	/* the following MUST be aligned on a 64-bit boundary */
	struct	rwindow pcb_rw[PCB_MAXWIN];	/* saved windows */
	u_int64_t	pcb_rwsp[PCB_MAXWIN];
};

#ifndef _KERNEL
/* Let gdb compile.  We need fancier macros to make these make sense. */
#define pcb_psr	pcb_pstate
#define pcb_wim	pcb_cwp
#endif /* _KERNEL */

#endif /* _MACHINE_PCB_H_ */
