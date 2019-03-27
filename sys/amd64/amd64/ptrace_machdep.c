/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Konstantin Belousov <kib@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/sysent.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/vmparam.h>

#ifdef COMPAT_FREEBSD32
struct ptrace_xstate_info32 {
	uint32_t	xsave_mask1, xsave_mask2;
	uint32_t	xsave_len;
};
#endif

static int
cpu_ptrace_xstate(struct thread *td, int req, void *addr, int data)
{
	struct ptrace_xstate_info info;
#ifdef COMPAT_FREEBSD32
	struct ptrace_xstate_info32 info32;
#endif
	char *savefpu;
	int error;

	if (!use_xsave)
		return (EOPNOTSUPP);

	switch (req) {
	case PT_GETXSTATE_OLD:
		fpugetregs(td);
		savefpu = (char *)(get_pcb_user_save_td(td) + 1);
		error = copyout(savefpu, addr,
		    cpu_max_ext_state_size - sizeof(struct savefpu));
		break;

	case PT_SETXSTATE_OLD:
		if (data > cpu_max_ext_state_size - sizeof(struct savefpu)) {
			error = EINVAL;
			break;
		}
		savefpu = malloc(data, M_TEMP, M_WAITOK);
		error = copyin(addr, savefpu, data);
		if (error == 0) {
			fpugetregs(td);
			error = fpusetxstate(td, savefpu, data);
		}
		free(savefpu, M_TEMP);
		break;

	case PT_GETXSTATE_INFO:
#ifdef COMPAT_FREEBSD32
		if (SV_CURPROC_FLAG(SV_ILP32)) {
			if (data != sizeof(info32)) {
				error = EINVAL;
			} else {
				info32.xsave_len = cpu_max_ext_state_size;
				info32.xsave_mask1 = xsave_mask;
				info32.xsave_mask2 = xsave_mask >> 32;
				error = copyout(&info32, addr, data);
			}
		} else
#endif
		{
			if (data != sizeof(info)) {
				error  = EINVAL;
			} else {
				bzero(&info, sizeof(info));
				info.xsave_len = cpu_max_ext_state_size;
				info.xsave_mask = xsave_mask;
				error = copyout(&info, addr, data);
			}
		}
		break;

	case PT_GETXSTATE:
		fpugetregs(td);
		savefpu = (char *)(get_pcb_user_save_td(td));
		error = copyout(savefpu, addr, cpu_max_ext_state_size);
		break;

	case PT_SETXSTATE:
		if (data < sizeof(struct savefpu) ||
		    data > cpu_max_ext_state_size) {
			error = EINVAL;
			break;
		}
		savefpu = malloc(data, M_TEMP, M_WAITOK);
		error = copyin(addr, savefpu, data);
		if (error == 0)
			error = fpusetregs(td, (struct savefpu *)savefpu,
			    savefpu + sizeof(struct savefpu), data -
			    sizeof(struct savefpu));
		free(savefpu, M_TEMP);
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
}

static void
cpu_ptrace_setbase(struct thread *td, int req, register_t r)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	set_pcb_flags(pcb, PCB_FULL_IRET);
	if (req == PT_SETFSBASE) {
		pcb->pcb_fsbase = r;
		td->td_frame->tf_fs = _ufssel;
	} else {
		pcb->pcb_gsbase = r;
		td->td_frame->tf_gs = _ugssel;
	}
}

#ifdef COMPAT_FREEBSD32
#define PT_I386_GETXMMREGS	(PT_FIRSTMACH + 0)
#define PT_I386_SETXMMREGS	(PT_FIRSTMACH + 1)

static int
cpu32_ptrace(struct thread *td, int req, void *addr, int data)
{
	struct savefpu *fpstate;
	struct pcb *pcb;
	uint32_t r;
	int error;

	switch (req) {
	case PT_I386_GETXMMREGS:
		fpugetregs(td);
		error = copyout(get_pcb_user_save_td(td), addr,
		    sizeof(*fpstate));
		break;

	case PT_I386_SETXMMREGS:
		fpugetregs(td);
		fpstate = get_pcb_user_save_td(td);
		error = copyin(addr, fpstate, sizeof(*fpstate));
		fpstate->sv_env.en_mxcsr &= cpu_mxcsr_mask;
		break;

	case PT_GETXSTATE_OLD:
	case PT_SETXSTATE_OLD:
	case PT_GETXSTATE_INFO:
	case PT_GETXSTATE:
	case PT_SETXSTATE:
		error = cpu_ptrace_xstate(td, req, addr, data);
		break;

	case PT_GETFSBASE:
	case PT_GETGSBASE:
		if (!SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			error = EINVAL;
			break;
		}
		pcb = td->td_pcb;
		if (td == curthread)
			update_pcb_bases(pcb);
		r = req == PT_GETFSBASE ? pcb->pcb_fsbase : pcb->pcb_gsbase;
		error = copyout(&r, addr, sizeof(r));
		break;

	case PT_SETFSBASE:
	case PT_SETGSBASE:
		if (!SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			error = EINVAL;
			break;
		}
		error = copyin(addr, &r, sizeof(r));
		if (error != 0)
			break;
		cpu_ptrace_setbase(td, req, r);
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
}
#endif

int
cpu_ptrace(struct thread *td, int req, void *addr, int data)
{
	register_t *r, rv;
	struct pcb *pcb;
	int error;

#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return (cpu32_ptrace(td, req, addr, data));
#endif

	/* Support old values of PT_GETXSTATE_OLD and PT_SETXSTATE_OLD. */
	if (req == PT_FIRSTMACH + 0)
		req = PT_GETXSTATE_OLD;
	if (req == PT_FIRSTMACH + 1)
		req = PT_SETXSTATE_OLD;

	switch (req) {
	case PT_GETXSTATE_OLD:
	case PT_SETXSTATE_OLD:
	case PT_GETXSTATE_INFO:
	case PT_GETXSTATE:
	case PT_SETXSTATE:
		error = cpu_ptrace_xstate(td, req, addr, data);
		break;

	case PT_GETFSBASE:
	case PT_GETGSBASE:
		pcb = td->td_pcb;
		if (td == curthread)
			update_pcb_bases(pcb);
		r = req == PT_GETFSBASE ? &pcb->pcb_fsbase : &pcb->pcb_gsbase;
		error = copyout(r, addr, sizeof(*r));
		break;

	case PT_SETFSBASE:
	case PT_SETGSBASE:
		error = copyin(addr, &rv, sizeof(rv));
		if (error != 0)
			break;
		if (rv >= td->td_proc->p_sysent->sv_maxuser) {
			error = EINVAL;
			break;
		}
		cpu_ptrace_setbase(td, req, rv);
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
}
