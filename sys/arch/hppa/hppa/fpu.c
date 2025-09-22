/*	$OpenBSD: fpu.c,v 1.5 2010/08/07 03:50:01 krw Exp $	*/

/*
 * Copyright (c) 2010 Joel Sing <jsing@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/fpu.h>
#include <machine/intr.h>
#include <machine/pcb.h>
#include <machine/reg.h>

__inline void fpu_proc(struct proc *, int);

void
fpu_proc_flush(struct proc *p)
{
	fpu_proc(p, 0);
}

void
fpu_proc_save(struct proc *p)
{
	fpu_proc(p, 1);
}

__inline void
fpu_proc(struct proc *p, int save)
{
	struct cpu_info *ci = curcpu();
	struct hppa_fpstate *hfp;
	struct cpu_info *fpuci;
#ifdef MULTIPROCESSOR
	int s;
#endif

	hfp = (struct hppa_fpstate *)p->p_md.md_regs->tf_cr30;
	fpuci = (struct cpu_info *)hfp->hfp_cpu;

	if (fpuci == NULL)
		return;

#ifdef MULTIPROCESSOR
	if (fpuci != ci) {

		if (hppa_ipi_send(fpuci, HPPA_IPI_FPU_SAVE))
			panic("FPU shootdown failed!");

		/*
		 * The sync is essential here since the volatile on hfp_cpu
		 * is ignored by gcc. Without this we will deadlock since
		 * hfp_cpu is never reloaded within the loop.
		 */
		while (hfp->hfp_cpu != NULL)
			asm volatile ("sync" ::: "memory");

	} else if (p->p_md.md_regs->tf_cr30 == ci->ci_fpu_state) {

		s = splipi();
		fpu_cpu_save(save);
		splx(s);

	}
#else
	if (p->p_md.md_regs->tf_cr30 == ci->ci_fpu_state)
		fpu_cpu_save(save);
#endif
}

/*
 * Save or flush FPU state - note that this must be called at IPL IPI when
 * running on a MULTIPROCESSOR kernel.
 */
void
fpu_cpu_save(int save)
{
	struct cpu_info *ci = curcpu();
	struct hppa_fpstate *hfp;
	struct cpu_info *fpuci;
	extern u_int fpu_enable;

#ifdef MULTIPROCESSOR
	splassert(IPL_IPI);
#endif

	if (ci->ci_fpu_state == 0)
		return;

	hfp = (struct hppa_fpstate *)ci->ci_fpu_state;
	fpuci = (struct cpu_info *)hfp->hfp_cpu;

#ifdef DIAGNOSTIC
	if (fpuci != ci)
		panic("FPU context is not on this CPU (%p != %p)",
		    ci, hfp->hfp_cpu);
#endif

	if (save) {
		mtctl(fpu_enable, CR_CCR);
		fpu_save((paddr_t)&hfp->hfp_regs);
		mtctl(0, CR_CCR);
	} else
		fpu_exit();

	hfp->hfp_cpu = NULL;
	ci->ci_fpu_state = 0;
	asm volatile ("sync" ::: "memory");
}
