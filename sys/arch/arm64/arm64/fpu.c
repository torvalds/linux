/*	$OpenBSD: fpu.c,v 1.4 2025/02/18 09:18:57 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/armreg.h>
#include <machine/fpu.h>

/*
 * Fool the compiler into accessing these registers without enabling
 * FP code generation.  OpenBSD assumes the CPU has FP support.
 */
#define fpcr		s3_3_c4_c4_0
#define fpsr		s3_3_c4_c4_1

void	sve_save(struct proc *p);

void
fpu_save(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;
	uint64_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	if ((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_ALL1)
		return;
	KASSERT((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_NONE);

	if ((cpacr & CPACR_ZEN_MASK) == CPACR_ZEN_TRAP_NONE) {
		sve_save(p);
		return;
	}

#define STRQx(x) \
    __asm volatile (".arch armv8-a+fp; str q" #x ", [%0, %1]" :: \
	"r"(fp->fp_reg), "i"(x * 16))

	STRQx(0);
	STRQx(1);
	STRQx(2);
	STRQx(3);
	STRQx(4);
	STRQx(5);
	STRQx(6);
	STRQx(7);
	STRQx(8);
	STRQx(9);
	STRQx(10);
	STRQx(11);
	STRQx(12);
	STRQx(13);
	STRQx(14);
	STRQx(15);
	STRQx(16);
	STRQx(17);
	STRQx(18);
	STRQx(19);
	STRQx(20);
	STRQx(21);
	STRQx(22);
	STRQx(23);
	STRQx(24);
	STRQx(25);
	STRQx(26);
	STRQx(27);
	STRQx(28);
	STRQx(29);
	STRQx(30);
	STRQx(31);

	fp->fp_sr = READ_SPECIALREG(fpsr);
	fp->fp_cr = READ_SPECIALREG(fpcr);
}

void
fpu_load(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;
	uint64_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	KASSERT((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_ALL1);
	KASSERT((cpacr & CPACR_ZEN_MASK) == CPACR_ZEN_TRAP_ALL1);

	if ((pcb->pcb_flags & PCB_FPU) == 0) {
		memset(fp, 0, sizeof(*fp));
		pcb->pcb_flags |= PCB_FPU;
	}

	/* Enable FPU. */
	cpacr &= ~CPACR_FPEN_MASK;
	cpacr |= CPACR_FPEN_TRAP_NONE;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
	__asm volatile ("isb");

#define LDRQx(x) \
    __asm volatile (".arch armv8-a+fp; ldr q" #x ", [%0, %1]" :: \
	"r"(fp->fp_reg), "i"(x * 16))

	LDRQx(0);
	LDRQx(1);
	LDRQx(2);
	LDRQx(3);
	LDRQx(4);
	LDRQx(5);
	LDRQx(6);
	LDRQx(7);
	LDRQx(8);
	LDRQx(9);
	LDRQx(10);
	LDRQx(11);
	LDRQx(12);
	LDRQx(13);
	LDRQx(14);
	LDRQx(15);
	LDRQx(16);
	LDRQx(17);
	LDRQx(18);
	LDRQx(19);
	LDRQx(20);
	LDRQx(21);
	LDRQx(22);
	LDRQx(23);
	LDRQx(24);
	LDRQx(25);
	LDRQx(26);
	LDRQx(27);
	LDRQx(28);
	LDRQx(29);
	LDRQx(30);
	LDRQx(31);

	WRITE_SPECIALREG(fpsr, fp->fp_sr);
	WRITE_SPECIALREG(fpcr, fp->fp_cr);
}

void
fpu_drop(void)
{
	uint64_t cpacr;

	/* Disable FPU and SVE. */
	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr &= ~(CPACR_FPEN_MASK | CPACR_ZEN_MASK);
	cpacr |= CPACR_FPEN_TRAP_ALL1 | CPACR_ZEN_TRAP_ALL1;
	WRITE_SPECIALREG(cpacr_el1, cpacr);

	/*
	 * No ISB instruction needed here, as returning to EL0 is a
	 * context synchronization event.
	 */
}

void
fpu_kernel_enter(void)
{
	struct pcb *pcb = &curproc->p_addr->u_pcb;
	uint64_t cpacr;

	if (pcb->pcb_flags & PCB_FPU)
		fpu_save(curproc);

	/* Enable FPU (kernel only). */
	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr &= ~(CPACR_FPEN_MASK | CPACR_ZEN_MASK);
	cpacr |= CPACR_FPEN_TRAP_EL0 | CPACR_ZEN_TRAP_ALL1;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
	__asm volatile ("isb");
}

void
fpu_kernel_exit(void)
{
	uint64_t cpacr;

	/* Disable FPU. */
	cpacr = READ_SPECIALREG(cpacr_el1);
	cpacr &= ~(CPACR_FPEN_MASK | CPACR_ZEN_MASK);
	cpacr |= CPACR_FPEN_TRAP_ALL1 | CPACR_ZEN_TRAP_ALL1;
	WRITE_SPECIALREG(cpacr_el1, cpacr);

	/*
	 * No ISB instruction needed here, as returning to EL0 is a
	 * context synchronization event.
	 */
}

void
sve_save(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;
	uint64_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	if ((cpacr & CPACR_ZEN_MASK) == CPACR_ZEN_TRAP_ALL1)
		return;
	KASSERT((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_NONE);
	KASSERT((cpacr & CPACR_ZEN_MASK) == CPACR_ZEN_TRAP_NONE);

#define STRZx(x) \
    __asm volatile (".arch armv8-a+sve; str z" #x ", [%0, %1, mul vl]" :: \
        "r"(fp->fp_reg), "i"(x))
#define STRPx(x) \
    __asm volatile (".arch armv8-a+sve; str p" #x ", [%0, %1, mul vl]" :: \
	"r"(pcb->pcb_sve_p), "i"(x))
#define STRFFR() \
    __asm volatile (".arch armv8-a+sve; rdffr p0.b; str p0, [%0]" :: \
	"r"(&pcb->pcb_sve_ffr));

	STRZx(0);
	STRZx(1);
	STRZx(2);
	STRZx(3);
	STRZx(4);
	STRZx(5);
	STRZx(6);
	STRZx(7);
	STRZx(8);
	STRZx(9);
	STRZx(10);
	STRZx(11);
	STRZx(12);
	STRZx(13);
	STRZx(14);
	STRZx(15);
	STRZx(16);
	STRZx(17);
	STRZx(18);
	STRZx(19);
	STRZx(20);
	STRZx(21);
	STRZx(22);
	STRZx(23);
	STRZx(24);
	STRZx(25);
	STRZx(26);
	STRZx(27);
	STRZx(28);
	STRZx(29);
	STRZx(30);
	STRZx(31);

	STRPx(0);
	STRPx(1);
	STRPx(2);
	STRPx(3);
	STRPx(4);
	STRPx(5);
	STRPx(6);
	STRPx(7);
	STRPx(8);
	STRPx(9);
	STRPx(10);
	STRPx(11);
	STRPx(12);
	STRPx(13);
	STRPx(14);
	STRPx(15);

	STRFFR();

	fp->fp_sr = READ_SPECIALREG(fpsr);
	fp->fp_cr = READ_SPECIALREG(fpcr);
}

void
sve_load(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;
	int fpu_enabled = 0;
	uint64_t cpacr;

	cpacr = READ_SPECIALREG(cpacr_el1);
	KASSERT((cpacr & CPACR_ZEN_MASK) == CPACR_ZEN_TRAP_ALL1);

	if ((cpacr & CPACR_FPEN_MASK) == CPACR_FPEN_TRAP_NONE)
		fpu_enabled = 1;

	if ((pcb->pcb_flags & PCB_FPU) == 0) {
		memset(fp, 0, sizeof(*fp));
		pcb->pcb_flags |= PCB_FPU;
	}
	if ((pcb->pcb_flags & PCB_SVE) == 0) {
		memset(pcb->pcb_sve_p, 0, sizeof(pcb->pcb_sve_p));
		pcb->pcb_sve_ffr = 0;
		pcb->pcb_flags |= PCB_SVE;
	}

	/* Enable FPU and SVE. */
	cpacr &= ~(CPACR_FPEN_MASK | CPACR_ZEN_MASK);
	cpacr |= CPACR_FPEN_TRAP_NONE | CPACR_ZEN_TRAP_NONE;
	WRITE_SPECIALREG(cpacr_el1, cpacr);
	__asm volatile ("isb");

#define LDRZx(x) \
    __asm volatile (".arch armv8-a+sve; ldr z" #x ", [%0, %1, mul vl]" :: \
	"r"(fp->fp_reg), "i"(x))
#define LDRPx(x) \
    __asm volatile (".arch armv8-a+sve; ldr p" #x ", [%0, %1, mul vl]" :: \
	"r"(pcb->pcb_sve_p), "i"(x))
#define LDRFFR() \
    __asm volatile (".arch armv8-a+sve; ldr p0, [%0]; wrffr p0.b" :: \
	"r"(&pcb->pcb_sve_ffr));

	LDRFFR();

	LDRPx(0);
	LDRPx(1);
	LDRPx(2);
	LDRPx(3);
	LDRPx(4);
	LDRPx(5);
	LDRPx(6);
	LDRPx(7);
	LDRPx(8);
	LDRPx(9);
	LDRPx(10);
	LDRPx(11);
	LDRPx(12);
	LDRPx(13);
	LDRPx(14);
	LDRPx(15);

	if (!fpu_enabled) {
		LDRZx(0);
		LDRZx(1);
		LDRZx(2);
		LDRZx(3);
		LDRZx(4);
		LDRZx(5);
		LDRZx(6);
		LDRZx(7);
		LDRZx(8);
		LDRZx(9);
		LDRZx(10);
		LDRZx(11);
		LDRZx(12);
		LDRZx(13);
		LDRZx(14);
		LDRZx(15);
		LDRZx(16);
		LDRZx(17);
		LDRZx(18);
		LDRZx(19);
		LDRZx(20);
		LDRZx(21);
		LDRZx(22);
		LDRZx(23);
		LDRZx(24);
		LDRZx(25);
		LDRZx(26);
		LDRZx(27);
		LDRZx(28);
		LDRZx(29);
		LDRZx(30);
		LDRZx(31);

		WRITE_SPECIALREG(fpsr, fp->fp_sr);
		WRITE_SPECIALREG(fpcr, fp->fp_cr);
	}
}
