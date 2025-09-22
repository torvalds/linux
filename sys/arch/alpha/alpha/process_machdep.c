/*	$OpenBSD: process_machdep.c,v 1.14 2025/06/28 16:04:09 miod Exp $	*/
/*	$NetBSD: process_machdep.c,v 1.7 1996/07/11 20:14:21 cgd Exp $	*/

/*-
 * Copyright (c) 1998 Doug Rabson
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
 */
/*
 * Copyright (c) 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/ptrace.h>
#include <machine/reg.h>
#include <machine/frame.h>

#include <alpha/alpha/db_instruction.h>

#define	process_frame(p)	((p)->p_md.md_tf)
#define	process_pcb(p)		(&(p)->p_addr->u_pcb)
#define	process_fpframe(p)	(&(process_pcb(p)->pcb_fp))

int
process_read_regs(struct proc *p, struct reg *regs)
{

	frametoreg(process_frame(p), regs);
	regs->r_regs[R_ZERO] = process_frame(p)->tf_regs[FRAME_PC];
	regs->r_regs[R_SP] = process_pcb(p)->pcb_hw.apcb_usp;
	return (0);
}

int
process_read_fpregs(struct proc *p, struct fpreg *regs)
{

	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 1);

	bcopy(process_fpframe(p), regs, sizeof(struct fpreg));
	return (0);
}

#ifdef PTRACE

int
process_write_regs(struct proc *p, struct reg *regs)
{

	regtoframe(regs, process_frame(p));
	process_frame(p)->tf_regs[FRAME_PC] = regs->r_regs[R_ZERO];
	process_pcb(p)->pcb_hw.apcb_usp = regs->r_regs[R_SP];
	return (0);
}

int
process_set_pc(struct proc *p, caddr_t addr)
{
	struct trapframe *frame = process_frame(p);

	frame->tf_regs[FRAME_PC] = (u_int64_t)addr;
	return (0);
}

int
process_write_fpregs(struct proc *p, struct fpreg *regs)
{

	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);

	bcopy(regs, process_fpframe(p), sizeof(struct fpreg));
	return (0);
}

/*
 * Single stepping infrastructure.
 */
int ptrace_set_bpt(struct proc *p, struct mdbpt *bpt);
int ptrace_clear_bpt(struct proc *p, struct mdbpt *bpt);
int ptrace_read_int(struct proc *, vaddr_t, u_int32_t *);
int ptrace_write_int(struct proc *, vaddr_t, u_int32_t);
u_int64_t ptrace_read_register(struct proc *p, int regno);

int
ptrace_read_int(struct proc *p, vaddr_t addr, u_int32_t *v)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = (caddr_t) v;
	iov.iov_len = sizeof(u_int32_t);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)addr;
	uio.uio_resid = sizeof(u_int32_t);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = curproc;
	return process_domem(curproc, p->p_p, &uio, PT_READ_I);
}

int
ptrace_write_int(struct proc *p, vaddr_t addr, u_int32_t v)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = (caddr_t) &v;
	iov.iov_len = sizeof(u_int32_t);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = (off_t)addr;
	uio.uio_resid = sizeof(u_int32_t);
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_procp = curproc;
	return process_domem(curproc, p->p_p, &uio, PT_WRITE_I);
}

u_int64_t
ptrace_read_register(struct proc *p, int regno)
{
	static int reg_to_frame[32] = {
		FRAME_V0,
		FRAME_T0,
		FRAME_T1,
		FRAME_T2,
		FRAME_T3,
		FRAME_T4,
		FRAME_T5,
		FRAME_T6,
		FRAME_T7,

		FRAME_S0,
		FRAME_S1,
		FRAME_S2,
		FRAME_S3,
		FRAME_S4,
		FRAME_S5,
		FRAME_S6,

		FRAME_A0,
		FRAME_A1,
		FRAME_A2,
		FRAME_A3,
		FRAME_A4,
		FRAME_A5,

		FRAME_T8,
		FRAME_T9,
		FRAME_T10,
		FRAME_T11,
		FRAME_RA,
		FRAME_T12,
		FRAME_AT,
		FRAME_GP,
		FRAME_SP,
		-1,		/* zero */
	};

	if (regno == R_ZERO)
		return 0;

	return p->p_md.md_tf->tf_regs[reg_to_frame[regno]];
}

int
ptrace_clear_bpt(struct proc *p, struct mdbpt *bpt)
{
	return ptrace_write_int(p, bpt->addr, bpt->contents);
}

int
ptrace_set_bpt(struct proc *p, struct mdbpt *bpt)
{
	int error;
	u_int32_t bpins = 0x00000080;
	error = ptrace_read_int(p, bpt->addr, &bpt->contents);
	if (error)
		return error;
	return ptrace_write_int(p, bpt->addr, bpins);
}

int
process_sstep(struct proc *p, int sstep)
{
	int error;
	vaddr_t pc = p->p_md.md_tf->tf_regs[FRAME_PC];
	alpha_instruction ins;
	vaddr_t addr[2];
	int count = 0;

	if (sstep == 0) {
		/* clearing the breakpoint */
		if (p->p_md.md_flags & MDP_STEP2) {
			ptrace_clear_bpt(p, &p->p_md.md_sstep[1]);
			ptrace_clear_bpt(p, &p->p_md.md_sstep[0]);
			p->p_md.md_flags &= ~MDP_STEP2;
		} else if (p->p_md.md_flags & MDP_STEP1) {
			ptrace_clear_bpt(p, &p->p_md.md_sstep[0]);
			p->p_md.md_flags &= ~MDP_STEP1;
		}
		return (0);
	}
#ifdef DIAGNOSTIC
	if (p->p_md.md_flags & (MDP_STEP1|MDP_STEP2))
		panic("process_sstep: step breakpoints not removed");
#endif
	error = ptrace_read_int(p, pc, &ins.bits);
	if (error)
		return (error);

	switch (ins.branch_format.opcode) {
	case op_j:
		/* Jump: target is register value */
		addr[0] = ptrace_read_register(p, ins.jump_format.rb) & ~3;
		count = 1;
		break;

	case op_br:
	case op_fbeq:
	case op_fblt:
	case op_fble:
	case op_bsr:
	case op_fbne:
	case op_fbge:
	case op_fbgt:
	case op_blbc:
	case op_beq:
	case op_blt:
	case op_ble:
	case op_blbs:
	case op_bne:
	case op_bge:
	case op_bgt:
		/* Branch: target is pc+4+4*displacement */
		addr[0] = pc + 4;
		addr[1] = pc + 4 + 4 * ins.branch_format.displacement;
		count = 2;
		break;

	default:
		addr[0] = pc + 4;
		count = 1;
	}

	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		fpusave_proc(p, 0);
	p->p_md.md_sstep[0].addr = addr[0];
	error = ptrace_set_bpt(p, &p->p_md.md_sstep[0]);
	if (error)
		return (error);
	if (count == 2) {
		p->p_md.md_sstep[1].addr = addr[1];
		error = ptrace_set_bpt(p, &p->p_md.md_sstep[1]);
		if (error) {
			ptrace_clear_bpt(p, &p->p_md.md_sstep[0]);
			return (error);
		}
		p->p_md.md_flags |= MDP_STEP2;
	} else
		p->p_md.md_flags |= MDP_STEP1;

	return (0);
}

#endif	/* PTRACE */
