/*	$OpenBSD: sys_machdep.c,v 1.41 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: sys_machdep.c,v 1.28 1996/05/03 19:42:29 christos Exp $	*/

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
 *	@(#)sys_machdep.c	5.5 (Berkeley) 1/19/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <machine/psl.h>
#include <machine/sysarch.h>

extern struct vm_map *kernel_map;

int i386_iopl(struct proc *, void *, register_t *);

#ifdef APERTURE
extern int allowaperture;
#endif

int
i386_iopl(struct proc *p, void *args, register_t *retval)
{
	int error;
	struct trapframe *tf = p->p_md.md_regs;
	struct i386_iopl_args ua;

	if ((error = suser(p)) != 0)
		return error;
#ifdef APERTURE
	if (!allowaperture && securelevel > 0)
		return EPERM;
#else
	if (securelevel > 0)
		return EPERM;
#endif

	if ((error = copyin(args, &ua, sizeof(ua))) != 0)
		return error;

	if (ua.iopl)
		tf->tf_eflags |= PSL_IOPL;
	else
		tf->tf_eflags &= ~PSL_IOPL;

	return 0;
}

uint32_t
i386_get_threadbase(struct proc *p, int which)
{
	struct segment_descriptor *sdp =
	    &p->p_addr->u_pcb.pcb_threadsegs[which];
	return sdp->sd_hibase << 24 | sdp->sd_lobase;
}

int
i386_set_threadbase(struct proc *p, uint32_t base, int which)
{
	struct segment_descriptor *sdp;

	/*
	 * We can't place a limit on the segment used by the library
	 * thread register (%gs) because the ELF ABI for i386 places
	 * data structures both before and after base pointer, using
	 * negative offsets for some bits (the static (load-time)
	 * TLS slots) and non-negative for others (the TCB block,
	 * including the pointer to the TLS dynamic thread vector).
	 * Protection must be provided by the paging subsystem.
	 */
	sdp = &p->p_addr->u_pcb.pcb_threadsegs[which];
	setsegment(sdp, (void *)base, 0xfffff, SDT_MEMRWA, SEL_UPL, 1, 1);

	if (p == curproc) {
		curcpu()->ci_gdt[which == TSEG_FS ? GUFS_SEL : GUGS_SEL].sd
		    = *sdp;
	}
	return 0;
}

int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysarch_args /* {
		syscallarg(int) op;
		syscallarg(void *) parms;
	} */ *uap = v;
	int error = 0;

	switch(SCARG(uap, op)) {
	case I386_IOPL:
		error = i386_iopl(p, SCARG(uap, parms), retval);
		break;

	case I386_GET_FSBASE:
	      {
		uint32_t base = i386_get_threadbase(p, TSEG_FS);

		error = copyout(&base, SCARG(uap, parms), sizeof(base));
		break;
	      }

	case I386_SET_FSBASE:
	      {
		uint32_t base;

		if ((error = copyin(SCARG(uap, parms), &base, sizeof(base))))
			break;
		error = i386_set_threadbase(p, base, TSEG_FS);
		break;
	      }

	case I386_GET_GSBASE:
	      {
		uint32_t base = i386_get_threadbase(p, TSEG_GS);

		error = copyout(&base, SCARG(uap, parms), sizeof(base));
		break;
	      }

	case I386_SET_GSBASE:
	      {
		uint32_t base;

		if ((error = copyin(SCARG(uap, parms), &base, sizeof(base))))
			break;
		error = i386_set_threadbase(p, base, TSEG_GS);
		break;
	      }

	default:
		error = EINVAL;
		break;
	}
	return (error);
}
