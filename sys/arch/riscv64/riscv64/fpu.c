/*	$OpenBSD: fpu.c,v 1.12 2024/10/17 01:57:18 jsg Exp $	*/

/*
 * Copyright (c) 2020 Dale Rahn <drahn@openbsd.org>
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

void
fpu_disable(void)
{
	__asm volatile ("csrc sstatus, %0" :: "r"(SSTATUS_FS_MASK));
}

void
fpu_enable_clean(void)
{
	__asm volatile ("csrc sstatus, %0" :: "r"(SSTATUS_FS_MASK));
	__asm volatile ("csrs sstatus, %0" :: "r"(SSTATUS_FS_CLEAN));
}

void
fpu_save(struct proc *p, struct trapframe *tf)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;

	if ((tf->tf_sstatus & SSTATUS_FS_MASK) == SSTATUS_FS_OFF ||
	    (tf->tf_sstatus & SSTATUS_FS_MASK) == SSTATUS_FS_CLEAN)
		return;

	fpu_enable_clean();

	__asm volatile("frcsr	%0" : "=r"(fp->fp_fcsr));

#define STFx(x) \
	__asm volatile ("fsd f" #x ", %1(%0)" : : "r"(fp->fp_f), "i"(x * 8))

	STFx(0);
	STFx(1);
	STFx(2);
	STFx(3);
	STFx(4);
	STFx(5);
	STFx(6);
	STFx(7);
	STFx(8);
	STFx(9);
	STFx(10);
	STFx(11);
	STFx(12);
	STFx(13);
	STFx(14);
	STFx(15);
	STFx(16);
	STFx(17);
	STFx(18);
	STFx(19);
	STFx(20);
	STFx(21);
	STFx(22);
	STFx(23);
	STFx(24);
	STFx(25);
	STFx(26);
	STFx(27);
	STFx(28);
	STFx(29);
	STFx(30);
	STFx(31);

	fpu_disable();

	/* mark FPU as clean */
	p->p_addr->u_pcb.pcb_tf->tf_sstatus &= ~SSTATUS_FS_MASK;
	p->p_addr->u_pcb.pcb_tf->tf_sstatus |= SSTATUS_FS_CLEAN;
}

void
fpu_load(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;

	KASSERT((pcb->pcb_tf->tf_sstatus & SSTATUS_FS_MASK) == SSTATUS_FS_OFF);

	if ((pcb->pcb_flags & PCB_FPU) == 0) {
		memset(fp, 0, sizeof(*fp));
		pcb->pcb_flags |= PCB_FPU;
	}

	fpu_enable_clean();

	__asm volatile("fscsr %0" : : "r"(fp->fp_fcsr));

#define RDFx(x) \
	__asm volatile ("fld f" #x ", %1(%0)" : : "r"(fp->fp_f), "i"(x * 8))

	RDFx(0);
	RDFx(1);
	RDFx(2);
	RDFx(3);
	RDFx(4);
	RDFx(5);
	RDFx(6);
	RDFx(7);
	RDFx(8);
	RDFx(9);
	RDFx(10);
	RDFx(11);
	RDFx(12);
	RDFx(13);
	RDFx(14);
	RDFx(15);
	RDFx(16);
	RDFx(17);
	RDFx(18);
	RDFx(19);
	RDFx(20);
	RDFx(21);
	RDFx(22);
	RDFx(23);
	RDFx(24);
	RDFx(25);
	RDFx(26);
	RDFx(27);
	RDFx(28);
	RDFx(29);
	RDFx(30);
	RDFx(31);

	fpu_disable();

	/* mark FPU as clean */
	p->p_addr->u_pcb.pcb_tf->tf_sstatus &= ~SSTATUS_FS_MASK;
	p->p_addr->u_pcb.pcb_tf->tf_sstatus |= SSTATUS_FS_CLEAN;
}
