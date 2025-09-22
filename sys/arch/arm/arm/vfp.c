/*	$OpenBSD: vfp.c,v 1.5 2022/08/29 02:01:18 jsg Exp $	*/

/*
 * Copyright (c) 2011 Dale Rahn <drahn@openbsd.org>
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

#include <arm/include/cpufunc.h>
#include <arm/include/vfp.h>
#include <arm/include/undefined.h>

static inline void
set_vfp_fpexc(uint32_t val)
{
	__asm volatile(
	    ".fpu vfpv3\n"
	    "vmsr fpexc, %0" :: "r" (val));
}

static inline uint32_t
get_vfp_fpexc(void)
{
	uint32_t val;
	__asm volatile(
	    ".fpu vfpv3\n"
	    "vmrs %0, fpexc" : "=r" (val));
	return val;
}

int vfp_fault(unsigned int, unsigned int, trapframe_t *, int, uint32_t);
void vfp_load(struct proc *p);
void vfp_store(struct fpreg *vfpsave);

void
vfp_init(void)
{
	uint32_t val;

	install_coproc_handler(10, vfp_fault);
	install_coproc_handler(11, vfp_fault);

	__asm volatile("mrc p15, 0, %0, c1, c0, 2" : "=r" (val));
	val |= COPROC10 | COPROC11;
	__asm volatile("mcr p15, 0, %0, c1, c0, 2" :: "r" (val));
	__asm volatile("isb");
}

void
vfp_store(struct fpreg *vfpsave)
{
	uint32_t scratch;

	if (get_vfp_fpexc() & VFPEXC_EN) {
		__asm volatile(
		    ".fpu vfpv3\n"
		    "vstmia	%1!, {d0-d15}\n"	/* d0-d15 */
		    "vstmia	%1!, {d16-d31}\n"	/* d16-d31 */
		    "vmrs	%0, fpscr\n"
		    "str	%0, [%1]\n"		/* save vfpscr */
		: "=&r" (scratch) : "r" (vfpsave));
	}

	/* disable FPU */
	set_vfp_fpexc(0);
}

uint32_t
vfp_save(void)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = curpcb;
	struct proc *p = curproc;
	uint32_t fpexc;

	if (ci->ci_fpuproc == 0)
		return 0;

	fpexc = get_vfp_fpexc();

	if ((fpexc & VFPEXC_EN) == 0)
		return fpexc;	/* not enabled, nothing to do */

	if (fpexc & VFPEXC_EX)
		panic("vfp exceptional data fault, time to write more code");

	if (pcb->pcb_fpcpu == NULL || ci->ci_fpuproc == NULL ||
	    !(pcb->pcb_fpcpu == ci && ci->ci_fpuproc == p)) {
		/* disable fpu before panic, otherwise recurse */
		set_vfp_fpexc(0);

		panic("FPU unit enabled when curproc and curcpu dont agree %p %p %p %p", pcb->pcb_fpcpu, ci, ci->ci_fpuproc, p);
	}

	vfp_store(&p->p_addr->u_pcb.pcb_fpstate);

	/*
	 * NOTE: fpu state is saved but remains 'valid', as long as
	 * curpcb()->pcb_fpucpu == ci && ci->ci_fpuproc == curproc()
	 * is true FPU state is valid and can just be enabled without reload.
	 */
	return fpexc;
}

void
vfp_enable(void)
{
	struct cpu_info *ci = curcpu();

	if (curproc->p_addr->u_pcb.pcb_fpcpu == ci &&
	    ci->ci_fpuproc == curproc) {
		disable_interrupts(PSR_I|PSR_F);

		/* FPU state is still valid, just enable and go */
		set_vfp_fpexc(VFPEXC_EN);
	}
}

void
vfp_load(struct proc *p)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = &p->p_addr->u_pcb;
	uint32_t scratch = 0;
	int psw;

	/* do not allow a partially synced state here */
	psw = disable_interrupts(PSR_I|PSR_F);

	/*
	 * p->p_pcb->pcb_fpucpu _may_ not be NULL here, but the FPU state
	 * was synced on kernel entry, so we can steal the FPU state
	 * instead of signalling and waiting for it to save
	 */

	/* enable to be able to load ctx */
	set_vfp_fpexc(VFPEXC_EN);

	__asm volatile(
	    ".fpu vfpv3\n"
	    "vldmia	%1!, {d0-d15}\n"		/* d0-d15 */
	    "vldmia	%1!, {d16-d31}\n"		/* d16-d31 */
	    "ldr	%0, [%1]\n"			/* set old vfpscr */
	    "vmsr	fpscr, %0\n"
	    : "=&r" (scratch) : "r" (&pcb->pcb_fpstate));

	ci->ci_fpuproc = p;
	pcb->pcb_fpcpu = ci;

	/* disable until return to userland */
	set_vfp_fpexc(0);

	restore_interrupts(psw);
}

int
vfp_fault(unsigned int pc, unsigned int insn, trapframe_t *tf, int fault_code,
    uint32_t fpexc)
{
	struct proc *p = curproc;
	struct pcb *pcb = &p->p_addr->u_pcb;

	if ((fpexc & VFPEXC_EN) != 0) {
		/*
		 * We probably ran into an unsupported instruction,
		 * like NEON on a non-NEON system. Let the process know.
		 */
		return 1;
	}

	/* we should be able to ignore old state of pcb_fpcpu ci_fpuproc */
	if ((pcb->pcb_flags & PCB_FPU) == 0) {
		pcb->pcb_flags |= PCB_FPU;
		memset(&pcb->pcb_fpstate, 0, sizeof(pcb->pcb_fpstate));
	}
	vfp_load(p);

	return 0;
}

void
vfp_discard(struct proc *p)
{
	struct cpu_info *ci = curcpu();

	if (curpcb->pcb_fpcpu == ci && ci->ci_fpuproc == p) {
		ci->ci_fpuproc = NULL;
		curpcb->pcb_fpcpu = NULL;
	}
}
