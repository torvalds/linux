/*-
 * Copyright (c) 2014 Justin Hibbits
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
#include <machine/altivec.h>
#include <machine/fpu.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/pcb.h>

int
cpu_ptrace(struct thread *td, int req, void *addr, int data)
{
	int error;
	struct pcb *pcb;
	struct vec vec;
	uint64_t vsr[32];
	uint64_t *vsr_dw1;
	int vsr_idx;

	pcb = td->td_pcb;

	bzero(&vec, sizeof(vec));
	bzero(vsr, sizeof(vsr));

	error = EINVAL;
	switch (req) {
	case PT_GETVRREGS:
		if (!(cpu_features & PPC_FEATURE_HAS_ALTIVEC))
			break;

		if (pcb->pcb_flags & PCB_VEC) {
			save_vec_nodrop(td);
			memcpy(&vec, &pcb->pcb_vec, sizeof(vec));
		}
		error = copyout(&vec, addr, sizeof(vec));
		break;
	case PT_SETVRREGS:
		if (!(cpu_features & PPC_FEATURE_HAS_ALTIVEC))
			break;
		error = copyin(addr, &vec, sizeof(vec));
		if (error == 0) {
			pcb->pcb_flags |= PCB_VEC;
			memcpy(&pcb->pcb_vec, &vec, sizeof(vec));
		}
		break;
	case PT_GETVSRREGS:
		if (!(cpu_features & PPC_FEATURE_HAS_VSX))
			break;

		if (pcb->pcb_flags & PCB_VSX) {
			save_fpu_nodrop(td);

			/*
			 * Doubleword 0 of VSR0-VSR31 overlap with FPR0-FPR31 and
			 * VSR32-VSR63 overlap with VR0-VR31, so we only copy
			 * the non-overlapping data, which is doubleword 1 of VSR0-VSR31.
			 */
			for (vsr_idx = 0; vsr_idx < nitems(vsr); vsr_idx++) {
				vsr_dw1 = (uint64_t *)&pcb->pcb_fpu.fpr[vsr_idx].vsr[2];
				vsr[vsr_idx] = *vsr_dw1;
			}
		}
		error = copyout(&vsr, addr, sizeof(vsr));
		break;
	case PT_SETVSRREGS:
		if (!(cpu_features & PPC_FEATURE_HAS_VSX))
			break;
		error = copyin(addr, &vsr, sizeof(vsr));
		if (error == 0) {
			pcb->pcb_flags |= PCB_VSX;

			/*
			 * Doubleword 0 of VSR0-VSR31 overlap with FPR0-FPR31 and
			 * VSR32-VSR63 overlap with VR0-VR31, so we only copy
			 * the non-overlapping data, which is doubleword 1 of VSR0-VSR31.
			 */
			for (vsr_idx = 0; vsr_idx < nitems(vsr); vsr_idx++) {
				vsr_dw1 = (uint64_t *)&pcb->pcb_fpu.fpr[vsr_idx].vsr[2];
				*vsr_dw1 = vsr[vsr_idx];
			}
		}
		break;

	default:
		break;
	}

	return (error);
}
