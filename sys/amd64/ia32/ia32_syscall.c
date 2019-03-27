/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, David Greenman
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the University of Utah, and William Jolitz.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * 386 Trap and System call handling
 */

#include "opt_clock.h"
#include "opt_cpu.h"
#include "opt_isa.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/pioctl.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>
#include <security/audit/audit.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>

#include <machine/cpu.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>

#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/ia32/ia32_signal.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/specialreg.h>
#include <machine/sysarch.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/cpufunc.h>

#define	IDTVEC(name)	__CONCAT(X,name)

extern inthand_t IDTVEC(int0x80_syscall), IDTVEC(int0x80_syscall_pti),
    IDTVEC(rsvd), IDTVEC(rsvd_pti);

void ia32_syscall(struct trapframe *frame);	/* Called from asm code */

void
ia32_set_syscall_retval(struct thread *td, int error)
{

	cpu_set_syscall_retval(td, error);
}

int
ia32_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	struct trapframe *frame;
	struct syscall_args *sa;
	caddr_t params;
	u_int32_t args[8], tmp;
	int error, i;
#ifdef COMPAT_43
	u_int32_t eip;
	int cs;
#endif

	p = td->td_proc;
	frame = td->td_frame;
	sa = &td->td_sa;

#ifdef COMPAT_43
	if (__predict_false(frame->tf_cs == 7 && frame->tf_rip == 2)) {
		/*
		 * In lcall $7,$0 after int $0x80.  Convert the user
		 * frame to what it would be for a direct int 0x80 instead
		 * of lcall $7,$0, by popping the lcall return address.
		 */
		error = fueword32((void *)frame->tf_rsp, &eip);
		if (error == -1)
			return (EFAULT);
		cs = fuword16((void *)(frame->tf_rsp + sizeof(u_int32_t)));
		if (cs == -1)
			return (EFAULT);

		/*
		 * Unwind in-kernel frame after all stack frame pieces
		 * were successfully read.
		 */
		frame->tf_rip = eip;
		frame->tf_cs = cs;
		frame->tf_rsp += 2 * sizeof(u_int32_t);
		frame->tf_err = 7;		/* size of lcall $7,$0 */
	}
#endif

	params = (caddr_t)frame->tf_rsp + sizeof(u_int32_t);
	sa->code = frame->tf_rax;

	/*
	 * Need to check if this is a 32 bit or 64 bit syscall.
	 */
	if (sa->code == SYS_syscall) {
		/*
		 * Code is first argument, followed by actual args.
		 */
		error = fueword32(params, &tmp);
		if (error == -1)
			return (EFAULT);
		sa->code = tmp;
		params += sizeof(int);
	} else if (sa->code == SYS___syscall) {
		/*
		 * Like syscall, but code is a quad, so as to maintain
		 * quad alignment for the rest of the arguments.
		 * We use a 32-bit fetch in case params is not
		 * aligned.
		 */
		error = fueword32(params, &tmp);
		if (error == -1)
			return (EFAULT);
		sa->code = tmp;
		params += sizeof(quad_t);
	}
 	if (sa->code >= p->p_sysent->sv_size)
 		sa->callp = &p->p_sysent->sv_table[0];
  	else
 		sa->callp = &p->p_sysent->sv_table[sa->code];
	sa->narg = sa->callp->sy_narg;

	if (params != NULL && sa->narg != 0)
		error = copyin(params, (caddr_t)args,
		    (u_int)(sa->narg * sizeof(int)));
	else
		error = 0;

	for (i = 0; i < sa->narg; i++)
		sa->args[i] = args[i];

	if (error == 0) {
		td->td_retval[0] = 0;
		td->td_retval[1] = frame->tf_rdx;
	}

	return (error);
}

#include "../../kern/subr_syscall.c"

void
ia32_syscall(struct trapframe *frame)
{
	struct thread *td;
	register_t orig_tf_rflags;
	int error;
	ksiginfo_t ksi;

	orig_tf_rflags = frame->tf_rflags;
	td = curthread;
	td->td_frame = frame;

	error = syscallenter(td);

	/*
	 * Traced syscall.
	 */
	if (orig_tf_rflags & PSL_T) {
		frame->tf_rflags &= ~PSL_T;
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGTRAP;
		ksi.ksi_code = TRAP_TRACE;
		ksi.ksi_addr = (void *)frame->tf_rip;
		trapsignal(td, &ksi);
	}

	syscallret(td, error);
	amd64_syscall_ret_flush_l1d(error);
}

static void
ia32_syscall_enable(void *dummy)
{

 	setidt(IDT_SYSCALL, pti ? &IDTVEC(int0x80_syscall_pti) :
	    &IDTVEC(int0x80_syscall), SDT_SYSIGT, SEL_UPL, 0);
}

static void
ia32_syscall_disable(void *dummy)
{

 	setidt(IDT_SYSCALL, pti ? &IDTVEC(rsvd_pti) : &IDTVEC(rsvd),
	    SDT_SYSIGT, SEL_KPL, 0);
}

SYSINIT(ia32_syscall, SI_SUB_EXEC, SI_ORDER_ANY, ia32_syscall_enable, NULL);
SYSUNINIT(ia32_syscall, SI_SUB_EXEC, SI_ORDER_ANY, ia32_syscall_disable, NULL);

#ifdef COMPAT_43
int
setup_lcall_gate(void)
{
	struct i386_ldt_args uap;
	struct user_segment_descriptor desc;
	uint32_t lcall_addr;
	int error;

	bzero(&uap, sizeof(uap));
	uap.start = 0;
	uap.num = 1;
	lcall_addr = curproc->p_sysent->sv_psstrings - sz_lcall_tramp;
	bzero(&desc, sizeof(desc));
	desc.sd_type = SDT_MEMERA;
	desc.sd_dpl = SEL_UPL;
	desc.sd_p = 1;
	desc.sd_def32 = 1;
	desc.sd_gran = 1;
	desc.sd_lolimit = 0xffff;
	desc.sd_hilimit = 0xf;
	desc.sd_lobase = lcall_addr;
	desc.sd_hibase = lcall_addr >> 24;
	error = amd64_set_ldt(curthread, &uap, &desc);
	if (error != 0)
		return (error);

	return (0);
}
#endif
