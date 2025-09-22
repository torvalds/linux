/*	$OpenBSD: signal.h,v 1.11 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: signal.h,v 1.2 1995/02/16 03:08:08 cgd Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#ifndef _MACHINE_SIGNAL_H_
#define	_MACHINE_SIGNAL_H_

#include <sys/cdefs.h>

typedef int	sig_atomic_t;

#if __BSD_VISIBLE || __XPG_VISIBLE >= 420
/*
 * Information pushed on stack when a signal is delivered.
 * This is used by the kernel to restore state following
 * execution of the signal handler.  It is also made available
 * to the handler to allow it to restore state properly if
 * a non-standard exit is performed.
 *
 * Note that sc_regs[] and sc_fpregs[]+sc_fpcr are inline
 * representations of 'struct reg' and 'struct fpreg', respectively.
 */
struct  sigcontext {
	long	sc_cookie;
	long	sc_mask;		/* signal mask to restore XXX should be int */
	long	sc_pc;			/* pc to restore */
	long	sc_ps;			/* ps to restore */
	unsigned long sc_regs[32];	/* integer register set (see above) */
#define	sc_sp	sc_regs[R_SP]
	long	sc_ownedfp;		/* fp has been used */
	unsigned long sc_fpregs[32];	/* FP register set (see above) */
	unsigned long sc_fpcr;		/* FP control register (see above) */
	unsigned long sc_fp_control;	/* FP software control word */
	long	sc_reserved[2];		/* XXX */
	long	sc_xxx[8];		/* XXX */
};
#endif /* __BSD_VISIBLE || __XPG_VISIBLE >= 420 */
#endif /* !_MACHINE_SIGNAL_H_*/
