/*	$OpenBSD: sig_machdep.c,v 1.32 2024/10/14 08:42:39 jsg Exp $	*/
/*
 * Copyright (c) 2014 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/errno.h>

#include <machine/reg.h>
#ifdef M88100
#include <machine/m88100.h>
#include <machine/trap.h>
#endif

#include <uvm/uvm_extern.h>

vaddr_t	local_stack_frame(struct trapframe *, vaddr_t, size_t);

/*
 * WARNING: sigcode() in subr.s assumes sf_scp is the first field of the
 * sigframe.
 */
struct sigframe {
	struct sigcontext	*sf_scp;	/* context ptr for handler */
	struct sigcontext	 sf_sc;		/* actual context */
	siginfo_t		 sf_si;
};

#ifdef DEBUG
int sigdebug = 0;
pid_t sigpid = 0;
#define SDB_FOLLOW	0x01
#define SDB_KSTACK	0x02
#endif

/*
 * Send an interrupt to process.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct trapframe *tf;
	struct sigframe *fp;
	size_t fsize;
	struct sigframe sf;
	vaddr_t addr;

	tf = p->p_md.md_tf;

	if (info)
		fsize = sizeof(struct sigframe);
	else
		fsize = offsetof(struct sigframe, sf_si);

	/*
	 * Allocate space for the signal handler context.
	 */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(tf->tf_r[31]) && onstack) {
		addr = local_stack_frame(tf,
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size),
		    fsize);
	} else
		addr = local_stack_frame(tf, tf->tf_r[31], fsize);

	fp = (struct sigframe *)addr;

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	bzero(&sf, fsize);
	sf.sf_scp = &fp->sf_sc;
	sf.sf_sc.sc_mask = mask;
	sf.sf_sc.sc_cookie = (long)sf.sf_scp ^ p->p_p->ps_sigcookie;

	if (info)
		sf.sf_si = *ksip;

	/*
	 * Copy the whole user context into signal context that we
	 * are building.
	 */
	bcopy((const void *)&tf->tf_regs, (void *)&sf.sf_sc.sc_regs,
	    sizeof(sf.sf_sc.sc_regs));

	if (copyout((caddr_t)&sf, (caddr_t)fp, fsize))
		return 1;

	/*
	 * Set up registers for the signal handler invocation.
	 */
	tf->tf_r[1] = p->p_p->ps_sigcode;	/* return to sigcode */
	tf->tf_r[2] = sig;			/* first arg is signo */
	tf->tf_r[3] = info ? (vaddr_t)&fp->sf_si : 0;
	tf->tf_r[4] = (vaddr_t)&fp->sf_sc;
	tf->tf_r[31] = (vaddr_t)fp;
	addr = (vaddr_t)catcher;		/* and resume in the handler */
#ifdef M88100
	if (CPU_IS88100) {
		tf->tf_snip = (addr & NIP_ADDR) | NIP_V;
		tf->tf_sfip = (tf->tf_snip + 4) | FIP_V;
	}
#endif
#ifdef M88110
	if (CPU_IS88110)
		tf->tf_exip = (addr & XIP_ADDR);
#endif

#ifdef DEBUG
	if ((sigdebug & SDB_FOLLOW) ||
	    ((sigdebug & SDB_KSTACK) && p->p_p->ps_pid == sigpid))
		printf("sendsig(%d): sig %d returns\n", p->p_p->ps_pid, sig);
#endif

	return 0;
}

/*
 * System call to cleanup state after a signal has been taken.  Reset signal
 * mask and stack state from context left by sendsig (above).  Return to
 * previous pc and psl as specified by context left by sendsig.  Check
 * carefully to make sure that the user has not modified the psl to gain
 * improper privileges or to cause a machine fault.
 */
int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
	   syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext ksc, *scp = SCARG(uap, sigcntxp);
	struct trapframe *tf;
	int error;
	vaddr_t pc;

	tf = p->p_md.md_tf;

	/*
	 * This is simpler than PROC_PC, assuming XIP is always valid
	 * on 88100, and doesn't have a delay slot on 88110
	 * (which is the status we expect from the signal code).
	 */ 
	pc = CPU_IS88110 ? tf->tf_regs.exip : tf->tf_regs.sxip ^ XIP_V;
	if (pc != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	if (((vaddr_t)scp & 3) != 0)
		return (EFAULT);

	if ((error = copyin((caddr_t)scp, (caddr_t)&ksc, sizeof(*scp))))
		return (error);

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	if ((((struct reg *)&ksc.sc_regs)->epsr ^ tf->tf_regs.epsr) &
	    PSR_USERSTATIC)
		return (EINVAL);

	bcopy((const void *)&ksc.sc_regs, (caddr_t)&tf->tf_regs,
	    sizeof(ksc.sc_regs));

	/*
	 * Restore the user supplied information
	 */
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

#ifdef M88100
	if (CPU_IS88100) {
		/*
		 * If we are returning from a signal handler triggered by
		 * a data access exception, the interrupted access has
		 * never been performed, and will not be reissued upon
		 * returning to userland.
		 *
		 * We can't simply call data_access_emulation(), for
		 * it might fault again. Instead, we invoke trap()
		 * again, which will either trigger another signal,
		 * or end up invoking data_access_emulation if safe.
		 */
		if (ISSET(tf->tf_dmt0, DMT_VALID))
			m88100_trap(T_DATAFLT, tf);
	}
#endif

	/*
	 * We really want to return to the instruction pointed to by the
	 * sigcontext.  However, due to the way exceptions work on 88110,
	 * returning EJUSTRETURN will cause m88110_syscall() to skip one
	 * instruction.  We avoid this by returning ERESTART, which will
	 * indeed cause the instruction pointed to by exip to be run
	 * again.
	 */
	return CPU_IS88100 ? EJUSTRETURN : ERESTART;
}

/*
 * Find out a safe place on the process' stack to put the sigframe struct.
 * While on 88110, this is straightforward, on 88100 we need to be
 * careful and not stomp over potential uncompleted data accesses, which
 * we will want to be able to perform upon sigreturn().
 */
vaddr_t
local_stack_frame(struct trapframe *tf, vaddr_t tos, size_t fsize)
{
	vaddr_t frame;

	frame = (tos - fsize) & ~_STACKALIGNBYTES;

#ifdef M88100
	if (CPU_IS88100 && ISSET(tf->tf_dmt0, DMT_VALID)) {
		for (;;) {
			tos = frame + fsize;
			if (/* ISSET(tf->tf_dmt0, DMT_VALID) && */
			    tf->tf_dma0 >= frame && tf->tf_dma0 < tos) {
				frame = (tf->tf_dma0 - fsize) &
				    ~_STACKALIGNBYTES;
				continue;
			}
			if (ISSET(tf->tf_dmt1, DMT_VALID) &&
			    tf->tf_dma1 >= frame && tf->tf_dma1 < tos) {
				frame = (tf->tf_dma1 - fsize) &
				    ~_STACKALIGNBYTES;
				continue;
			}
			if (ISSET(tf->tf_dmt2, DMT_VALID) &&
			    tf->tf_dma2 >= frame && tf->tf_dma2 < tos) {
				frame = (tf->tf_dma2 - fsize) &
				    ~_STACKALIGNBYTES;
				continue;
			}
			break;
		}
	}
#endif

	return frame;
}
