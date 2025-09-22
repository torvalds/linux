/*	$OpenBSD: fpu.c,v 1.45 2025/07/02 14:51:31 kettenis Exp $	*/
/*	$NetBSD: fpu.c,v 1.1 2003/04/26 18:39:28 fvdl Exp $	*/

/*-
 * Copyright (c) 1994, 1995, 1998 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1990 William Jolitz.
 * Copyright (c) 1991 The Regents of the University of California.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/cpufunc.h>
#include <machine/pcb.h>
#include <machine/trap.h>
#include <machine/specialreg.h>
#include <machine/fpu.h>


/*
 * The mask of enabled XSAVE features.
 */
uint64_t	xsave_mask;

static int x86fpflags_to_siginfo(u_int32_t);

/*
 * Size of the area needed to save the FPU state and other
 * XSAVE-supported state components.
 */
size_t		fpu_save_len = sizeof(struct fxsave64);

/*
 * The mxcsr_mask for this host, taken from fxsave() on the primary CPU
 */
uint32_t	fpu_mxcsr_mask;

/*
 * Init the FPU.
 */
void
fpuinit(struct cpu_info *ci)
{
	fninit();
	if (fpu_mxcsr_mask == 0) {
		struct fxsave64 fx __attribute__((aligned(16)));

		bzero(&fx, sizeof(fx));
		fxsave(&fx);
		if (fx.fx_mxcsr_mask)
			fpu_mxcsr_mask = fx.fx_mxcsr_mask;
		else
			fpu_mxcsr_mask = __INITIAL_MXCSR_MASK__;
	}
}

/*
 * Record the FPU state and reinitialize it all except for the control word.
 * Returns the code to include in an SIGFPE.
 *
 * Reinitializing the state allows naive SIGFPE handlers to longjmp without
 * doing any fixups.
 */
int
fputrap(int type)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = curproc;
	struct savefpu *sfp = &p->p_addr->u_pcb.pcb_savefpu;
	u_int32_t mxcsr, statbits;
	u_int16_t cw;

	KASSERT(ci->ci_pflags & CPUPF_USERXSTATE);
	ci->ci_pflags &= ~CPUPF_USERXSTATE;
	fpusavereset(sfp);

	if (type == T_XMM) {
		mxcsr = sfp->fp_fxsave.fx_mxcsr;
	  	statbits = mxcsr;
		mxcsr &= ~0x3f;
		ldmxcsr(&mxcsr);
	} else {
		fninit();
		fwait();
		cw = sfp->fp_fxsave.fx_fcw;
		fldcw(&cw);
		fwait();
		statbits = sfp->fp_fxsave.fx_fsw;
	}
	return x86fpflags_to_siginfo(statbits);
}

static int
x86fpflags_to_siginfo(u_int32_t flags)
{
        int i;
        static int x86fp_siginfo_table[] = {
                FPE_FLTINV, /* bit 0 - invalid operation */
                FPE_FLTRES, /* bit 1 - denormal operand */
                FPE_FLTDIV, /* bit 2 - divide by zero   */
                FPE_FLTOVF, /* bit 3 - fp overflow      */
                FPE_FLTUND, /* bit 4 - fp underflow     */
                FPE_FLTRES, /* bit 5 - fp precision     */
                FPE_FLTINV, /* bit 6 - stack fault      */
        };

        for (i = 0; i < nitems(x86fp_siginfo_table); i++) {
                if (flags & (1 << i))
                        return (x86fp_siginfo_table[i]);
        }
        /* punt if flags not set */
        return (FPE_FLTINV);
}

void
fpu_kernel_enter(void)
{
	struct cpu_info *ci = curcpu();

	splassert(IPL_NONE);

	/* save curproc's FPU state if we haven't already */
	if (ci->ci_pflags & CPUPF_USERXSTATE) {
		ci->ci_pflags &= ~CPUPF_USERXSTATE;
		fpusavereset(&curproc->p_addr->u_pcb.pcb_savefpu);
	} else {
		fpureset();
	}
}

void
fpu_kernel_exit(void)
{
	/* make sure we don't leave anything in the registers */
	fpureset();
}
