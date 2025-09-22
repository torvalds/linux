/*	$OpenBSD: process_machdep.c,v 1.18 2024/11/27 05:25:56 anton Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.1 2003/04/26 18:39:31 fvdl Exp $	*/

/*-
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file may seem a bit stylized, but that so that it's easier to port.
 * Functions to be implemented here are:
 *
 * process_read_regs(proc, regs)
 *	Get the current user-visible register set from the process
 *	and copy it into the regs structure (<machine/reg.h>).
 *	The process is stopped at the time read_regs is called.
 *
 * process_write_regs(proc, regs)
 *	Update the current register set from the passed in regs
 *	structure.  Take care to avoid clobbering special CPU
 *	registers or privileged bits in the PSL.
 *	The process is stopped at the time write_regs is called.
 *
 * process_sstep(proc)
 *	Arrange for the process to trap after executing a single instruction.
 *
 * process_set_pc(proc)
 *	Set the process's program counter.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#include <uvm/uvm_extern.h>

#include <machine/reg.h>
#include <machine/fpu.h>

static __inline struct trapframe *process_frame(struct proc *);
static __inline struct fxsave64 *process_fpframe(struct proc *);

static __inline struct trapframe *
process_frame(struct proc *p)
{

	return (p->p_md.md_regs);
}

static __inline struct fxsave64 *
process_fpframe(struct proc *p)
{

	return (&p->p_addr->u_pcb.pcb_savefpu.fp_fxsave);
}

int
process_read_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

        regs->r_rdi = tf->tf_rdi;
        regs->r_rsi = tf->tf_rsi;
        regs->r_rdx = tf->tf_rdx;
        regs->r_rcx = tf->tf_rcx;
        regs->r_r8  = tf->tf_r8;
        regs->r_r9  = tf->tf_r9;
        regs->r_r10 = tf->tf_r10;
        regs->r_r11 = tf->tf_r11;
        regs->r_r12 = tf->tf_r12;
        regs->r_r13 = tf->tf_r13;
        regs->r_r14 = tf->tf_r14;
        regs->r_r15 = tf->tf_r15;
        regs->r_rbp = tf->tf_rbp;
        regs->r_rbx = tf->tf_rbx;
        regs->r_rax = tf->tf_rax;
        regs->r_rsp = tf->tf_rsp;
        regs->r_rip = tf->tf_rip;
        regs->r_rflags = tf->tf_rflags;
        regs->r_cs  = tf->tf_cs;
        regs->r_ss  = tf->tf_ss;
        regs->r_ds  = GSEL(GUDATA_SEL, SEL_UPL);
        regs->r_es  = GSEL(GUDATA_SEL, SEL_UPL);
        regs->r_fs  = GSEL(GUDATA_SEL, SEL_UPL);
        regs->r_gs  = GSEL(GUDATA_SEL, SEL_UPL);

	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{
	struct fxsave64 *frame = process_fpframe(p);

	memcpy(&regs->fxstate, frame, sizeof(*regs));
	return (0);
}

#ifdef	PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{
	struct trapframe *tf = process_frame(p);

	/*
	 * Check for security violations.
	 */
	if (check_context(regs, tf))
		return (EINVAL);

        tf->tf_rdi = regs->r_rdi;
        tf->tf_rsi = regs->r_rsi;
        tf->tf_rdx = regs->r_rdx;
        tf->tf_rcx = regs->r_rcx;
        tf->tf_r8  = regs->r_r8;
        tf->tf_r9  = regs->r_r9;
        tf->tf_r10 = regs->r_r10;
        tf->tf_r11 = regs->r_r11;
        tf->tf_r12 = regs->r_r12;
        tf->tf_r13 = regs->r_r13;
        tf->tf_r14 = regs->r_r14;
        tf->tf_r15 = regs->r_r15;
        tf->tf_rbp = regs->r_rbp;
        tf->tf_rbx = regs->r_rbx;
        tf->tf_rax = regs->r_rax;
        tf->tf_rsp = regs->r_rsp;
        tf->tf_rip = regs->r_rip;
        tf->tf_rflags = regs->r_rflags;
        tf->tf_cs  = regs->r_cs;
        tf->tf_ss  = regs->r_ss;

	/* force target to return via iretq so all registers are updated */
	p->p_md.md_flags |= MDP_IRET;

	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{
	struct fxsave64 *frame = process_fpframe(p);

	memcpy(frame, &regs->fxstate, sizeof(*regs));
	frame->fx_mxcsr &= fpu_mxcsr_mask;

	/* force target to return via iretq so bogus xstate can be handled */
	p->p_md.md_flags |= MDP_IRET;
	return (0);
}

int
process_sstep(struct proc *p, int sstep)
{
	struct trapframe *tf = process_frame(p);

	if (sstep)
		tf->tf_rflags |= PSL_T;
	else
		tf->tf_rflags &= ~PSL_T;
	
	return (0);
}

int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *tf = process_frame(p);

	if ((u_int64_t)addr > VM_MAXUSER_ADDRESS)
		return EINVAL;
	tf->tf_rip = (u_int64_t)addr;

	return (0);
}

int
process_read_xstate_info(struct proc *p, void *addr)
{
	struct ptrace_xstate_info *info = addr;

	if (xsave_mask == 0)
		return (ENOTSUP);

	info->xsave_mask = xsave_mask;
	info->xsave_len = fpu_save_len;
	return (0);
}

struct xsave_area {
	uint8_t	legacy_region[512];

	struct xsave_header {
		uint64_t	xstate_bv;
		uint64_t	xcomp_bv;
		uint8_t		rsvd0[48];
	} xsave_header;

	uint8_t	extended_region[];
} __attribute__((packed));

#define XSTATE_COMPONENT_X87	(1ULL << 0)
#define XSTATE_COMPONENT_SSE	(1ULL << 1)
#define XSTATE_COMPONENT_AVX	(1ULL << 2)

int
process_read_xstate(struct proc *p, void *addr)
{
	struct xsave_area *area =
	    (struct xsave_area *)&p->p_addr->u_pcb.pcb_savefpu;

	if (xsave_mask == 0)
		return (ENOTSUP);

	memcpy(addr, area, fpu_save_len);
	return (0);
}

int
process_write_xstate(struct proc *p, void *addr)
{
	struct xsave_area *area =
	    (struct xsave_area *)&p->p_addr->u_pcb.pcb_savefpu;
	struct xsave_area *new_area = (struct xsave_area *)addr;
	uint32_t offset_extended_region = offsetof(struct xsave_area,
	    extended_region);
	uint32_t a, b, c, d;

	if (xsave_mask == 0)
		return (ENOTSUP);

	/*
	 * Honor changes to x87, SSE and AVX components and mark them as in use.
	 * Required to ensure any changes are restored once the traced process
	 * continues execution.
	 */
	if ((xsave_mask & XSTATE_COMPONENT_X87) ||
	    (xsave_mask & XSTATE_COMPONENT_SSE)) {
		memcpy(area->legacy_region, new_area->legacy_region,
		    sizeof(area->legacy_region));
		area->xsave_header.xstate_bv |= xsave_mask &
		    (XSTATE_COMPONENT_X87 | XSTATE_COMPONENT_SSE);
	}
	if (xsave_mask & XSTATE_COMPONENT_AVX) {
		CPUID_LEAF(0xd, 2, a, b, c, d);
		if (offset_extended_region == b &&
		    offset_extended_region + a <= fpu_save_len) {
			memcpy(area->extended_region,
			    new_area->extended_region, a);
			area->xsave_header.xstate_bv |= XSTATE_COMPONENT_AVX;
		}
	}

	return (0);
}

#endif	/* PTRACE */
