/*	$OpenBSD: fpu.c,v 1.4 2021/04/14 18:35:14 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/user.h>

#include <machine/cpufunc.h>
#include <machine/fenv.h>

void
save_vsx(struct proc *p)
{
	struct fpreg *fp = &p->p_addr->u_pcb.pcb_fpstate;

	mtmsr(mfmsr() | (PSL_FP|PSL_VEC|PSL_VSX));

	isync();

#define STXVVSR(n) \
	__asm volatile ("stxvd2x %%vs" #n ", 0, %0" :: "b"(&fp->fp_vsx[(n)]));

	STXVVSR(0);	STXVVSR(1);	STXVVSR(2);	STXVVSR(3);
	STXVVSR(4);	STXVVSR(5);	STXVVSR(6);	STXVVSR(7);
	STXVVSR(8);	STXVVSR(9);	STXVVSR(10);	STXVVSR(11);
	STXVVSR(12);	STXVVSR(13);	STXVVSR(14);	STXVVSR(15);
	STXVVSR(16);	STXVVSR(17);	STXVVSR(18);	STXVVSR(19);
	STXVVSR(20);	STXVVSR(21);	STXVVSR(22);	STXVVSR(23);
	STXVVSR(24);	STXVVSR(25);	STXVVSR(26);	STXVVSR(27);
	STXVVSR(28);	STXVVSR(29);	STXVVSR(30);	STXVVSR(31);
	STXVVSR(32);	STXVVSR(33);	STXVVSR(34);	STXVVSR(35);
	STXVVSR(36);	STXVVSR(37);	STXVVSR(38);	STXVVSR(39);
	STXVVSR(40);	STXVVSR(41);	STXVVSR(42);	STXVVSR(43);
	STXVVSR(44);	STXVVSR(45);	STXVVSR(46);	STXVVSR(47);
	STXVVSR(48);	STXVVSR(49);	STXVVSR(50);	STXVVSR(51);
	STXVVSR(52);	STXVVSR(53);	STXVVSR(54);	STXVVSR(55);
	STXVVSR(56);	STXVVSR(57);	STXVVSR(58);	STXVVSR(59);
	STXVVSR(60);	STXVVSR(61);	STXVVSR(62);	STXVVSR(63);

	__asm volatile ("mffs %%f0; stfd %%f0, 0(%0)"
	    :: "b"(&fp->fp_fpscr));
	__asm volatile ("mfvscr %%v0; stvewx %%v0, 0, %0"
	    :: "b"(&fp->fp_vscr));

	isync();

	mtmsr(mfmsr() & ~(PSL_FP|PSL_VEC|PSL_VSX));
}

void
restore_vsx(struct proc *p)
{
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct fpreg *fp = &pcb->pcb_fpstate;

	if ((pcb->pcb_flags & (PCB_FPU|PCB_VEC|PCB_VSX)) == 0)
		memset(fp, 0, sizeof(*fp));

	mtmsr(mfmsr() | (PSL_FP|PSL_VEC|PSL_VSX));

	isync();

	__asm volatile ("lfd %%f0, 0(%0); mtfsf 0xff,%%f0"
	    :: "b"(&fp->fp_fpscr));
	__asm volatile ("vxor %%v0, %%v0, %%v0; lvewx %%v0, 0, %0; mtvscr %%v0"
	    :: "b"(&fp->fp_vscr));

#define LXVVSR(n) \
	__asm volatile ("lxvd2x %%vs" #n ", 0, %0" :: "b"(&fp->fp_vsx[(n)]));

	LXVVSR(0);	LXVVSR(1);	LXVVSR(2);	LXVVSR(3);
	LXVVSR(4);	LXVVSR(5);	LXVVSR(6);	LXVVSR(7);
	LXVVSR(8);	LXVVSR(9);	LXVVSR(10);	LXVVSR(11);
	LXVVSR(12);	LXVVSR(13);	LXVVSR(14);	LXVVSR(15);
	LXVVSR(16);	LXVVSR(17);	LXVVSR(18);	LXVVSR(19);
	LXVVSR(20);	LXVVSR(21);	LXVVSR(22);	LXVVSR(23);
	LXVVSR(24);	LXVVSR(25);	LXVVSR(26);	LXVVSR(27);
	LXVVSR(28);	LXVVSR(29);	LXVVSR(30);	LXVVSR(31);
	LXVVSR(32);	LXVVSR(33);	LXVVSR(34);	LXVVSR(35);
	LXVVSR(36);	LXVVSR(37);	LXVVSR(38);	LXVVSR(39);
	LXVVSR(40);	LXVVSR(41);	LXVVSR(42);	LXVVSR(43);
	LXVVSR(44);	LXVVSR(45);	LXVVSR(46);	LXVVSR(47);
	LXVVSR(48);	LXVVSR(49);	LXVVSR(50);	LXVVSR(51);
	LXVVSR(52);	LXVVSR(53);	LXVVSR(54);	LXVVSR(55);
	LXVVSR(56);	LXVVSR(57);	LXVVSR(58);	LXVVSR(59);
	LXVVSR(60);	LXVVSR(61);	LXVVSR(62);	LXVVSR(63);

	isync();

	mtmsr(mfmsr() & ~(PSL_FP|PSL_VEC|PSL_VSX));
}

int
fpu_sigcode(struct proc *p)
{
	struct trapframe *tf = p->p_md.md_regs;
	struct fpreg *fp = &p->p_addr->u_pcb.pcb_fpstate;
	int code = FPE_FLTINV;

	KASSERT(tf->srr1 & PSL_FP);
	tf->srr1 &= ~(PSL_FPU|PSL_VEC|PSL_VSX);
	save_vsx(p);

	if (fp->fp_fpscr & FE_INVALID)
		code = FPE_FLTINV;
	else if (fp->fp_fpscr & FE_DIVBYZERO)
		code = FPE_FLTDIV;
	else if (fp->fp_fpscr & FE_OVERFLOW)
		code = FPE_FLTOVF;
	else if (fp->fp_fpscr & FE_UNDERFLOW)
		code = FPE_FLTUND;
	else if (fp->fp_fpscr & FE_INEXACT)
		code = FPE_FLTRES;

	return code;
}
