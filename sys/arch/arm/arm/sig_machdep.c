/*	$OpenBSD: sig_machdep.c,v 1.21 2021/10/06 15:46:03 claudio Exp $	*/
/*	$NetBSD: sig_machdep.c,v 1.22 2003/10/08 00:28:41 thorpej Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Machine dependant functions for kernel setup
 *
 * Created      : 17/09/94
 */

#include <sys/param.h>

#include <sys/mount.h>		/* XXX only needed by syscallargs.h */
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/syscallargs.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/pcb.h>

#include <arm/armreg.h>

#include <uvm/uvm_extern.h>

static __inline struct trapframe *
process_frame(struct proc *p)
{
	return p->p_addr->u_pcb.pcb_tf;
}

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode to call routine, followed by
 * syscall to sigreturn routine below.  After sigreturn resets the
 * signal mask, the stack, and the frame pointer, it returns to the
 * user specified pc.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf;
	struct sigframe *fp, frame;

	tf = process_frame(p);

	/* Allocate space for the signal handler context. */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(tf->tf_usr_sp) && onstack)
		fp = (struct sigframe *)
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		fp = (struct sigframe *)tf->tf_usr_sp;

	/* make room on the stack */
	fp--;

	/* make the stack aligned */
	fp = (struct sigframe *)STACKALIGN(fp);

	/* Build stack frame for signal trampoline. */
	bzero(&frame, sizeof(frame));
	frame.sf_signum = sig;
	frame.sf_sip = NULL;
	frame.sf_scp = &fp->sf_sc;
	frame.sf_handler = catcher;

	/* Save register context. */
	frame.sf_sc.sc_r0     = tf->tf_r0;
	frame.sf_sc.sc_r1     = tf->tf_r1;
	frame.sf_sc.sc_r2     = tf->tf_r2;
	frame.sf_sc.sc_r3     = tf->tf_r3;
	frame.sf_sc.sc_r4     = tf->tf_r4;
	frame.sf_sc.sc_r5     = tf->tf_r5;
	frame.sf_sc.sc_r6     = tf->tf_r6;
	frame.sf_sc.sc_r7     = tf->tf_r7;
	frame.sf_sc.sc_r8     = tf->tf_r8;
	frame.sf_sc.sc_r9     = tf->tf_r9;
	frame.sf_sc.sc_r10    = tf->tf_r10;
	frame.sf_sc.sc_r11    = tf->tf_r11;
	frame.sf_sc.sc_r12    = tf->tf_r12;
	frame.sf_sc.sc_usr_sp = tf->tf_usr_sp;
	frame.sf_sc.sc_usr_lr = tf->tf_usr_lr;
	frame.sf_sc.sc_svc_lr = tf->tf_svc_lr;
	frame.sf_sc.sc_pc     = tf->tf_pc;
	frame.sf_sc.sc_spsr   = tf->tf_spsr;

	/* Save signal mask. */
	frame.sf_sc.sc_mask = mask;

	/* Save FPU registers. */
	frame.sf_sc.sc_fpused = pcb->pcb_flags & PCB_FPU;
	if (frame.sf_sc.sc_fpused) {
		frame.sf_sc.sc_fpscr = pcb->pcb_fpstate.fp_scr;
		memcpy(&frame.sf_sc.sc_fpreg, &pcb->pcb_fpstate.fp_reg,
		   sizeof(pcb->pcb_fpstate.fp_reg));
		pcb->pcb_flags &= ~PCB_FPU;
		pcb->pcb_fpcpu = NULL;
	}

	if (info) {
		frame.sf_sip = &fp->sf_si;
		frame.sf_si = *ksip;
	}

	frame.sf_sc.sc_cookie = (long)&fp->sf_sc ^ p->p_p->ps_sigcookie;
	if (copyout(&frame, fp, sizeof(frame)) != 0)
		return 1;

	/*
	 * Build context to run handler in.  We invoke the handler
	 * directly, only returning via the trampoline.
	 */
	tf->tf_r0 = sig;
	tf->tf_r1 = (register_t)frame.sf_sip;
	tf->tf_r2 = (register_t)frame.sf_scp;
	tf->tf_pc = (register_t)frame.sf_handler;
	tf->tf_usr_sp = (register_t)fp;

	tf->tf_usr_lr = p->p_p->ps_sigcode;

	return 0;
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psr to gain improper privileges or to cause
 * a machine fault.
 */

int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext ksc, *scp = SCARG(uap, sigcntxp);
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	if (copyin(scp, &ksc, sizeof(*scp)) != 0)
		return (EFAULT);

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	/*
	 * Make sure the processor mode has not been tampered with and
	 * interrupts have not been disabled.
	 */
	if ((ksc.sc_spsr & PSR_MODE) != PSR_USR32_MODE ||
	    (ksc.sc_spsr & (PSR_I | PSR_F)) != 0)
		return (EINVAL);

	/* Restore register context. */
	tf = process_frame(p);
	tf->tf_r0    = ksc.sc_r0;
	tf->tf_r1    = ksc.sc_r1;
	tf->tf_r2    = ksc.sc_r2;
	tf->tf_r3    = ksc.sc_r3;
	tf->tf_r4    = ksc.sc_r4;
	tf->tf_r5    = ksc.sc_r5;
	tf->tf_r6    = ksc.sc_r6;
	tf->tf_r7    = ksc.sc_r7;
	tf->tf_r8    = ksc.sc_r8;
	tf->tf_r9    = ksc.sc_r9;
	tf->tf_r10   = ksc.sc_r10;
	tf->tf_r11   = ksc.sc_r11;
	tf->tf_r12   = ksc.sc_r12;
	tf->tf_usr_sp = ksc.sc_usr_sp;
	tf->tf_usr_lr = ksc.sc_usr_lr;
	tf->tf_svc_lr = ksc.sc_svc_lr;
	tf->tf_pc    = ksc.sc_pc;
	tf->tf_spsr  = ksc.sc_spsr;

	/* Restore signal mask. */
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

	/* Restore FPU registers. */
	if (ksc.sc_fpused) {
		pcb->pcb_fpstate.fp_scr = ksc.sc_fpscr;
		memcpy(&pcb->pcb_fpstate.fp_reg, &ksc.sc_fpreg,
		    sizeof(pcb->pcb_fpstate.fp_reg));
		pcb->pcb_flags |= PCB_FPU;
		pcb->pcb_fpcpu = NULL;
	} else {
		pcb->pcb_flags &= ~PCB_FPU;
		pcb->pcb_fpcpu = NULL;
	}

	return (EJUSTRETURN);
}
