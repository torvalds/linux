/*-
 * Copyright (c) 2004 Olivier Houchard
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/machdep.h>
#include <machine/db_machdep.h>

static int
ptrace_read_int(struct thread *td, vm_offset_t addr, uint32_t *v)
{

	if (proc_readmem(td, td->td_proc, addr, v, sizeof(*v)) != sizeof(*v))
		return (ENOMEM);
	return (0);
}

static int
ptrace_write_int(struct thread *td, vm_offset_t addr, uint32_t v)
{

	if (proc_writemem(td, td->td_proc, addr, &v, sizeof(v)) != sizeof(v))
		return (ENOMEM);
	return (0);
}

static u_int
ptrace_get_usr_reg(void *cookie, int reg)
{
	int ret;
	struct thread *td = cookie;

	KASSERT(((reg >= 0) && (reg <= ARM_REG_NUM_PC)),
	 ("reg is outside range"));

	switch(reg) {
	case ARM_REG_NUM_PC:
		ret = td->td_frame->tf_pc;
		break;
	case ARM_REG_NUM_LR:
		ret = td->td_frame->tf_usr_lr;
		break;
	case ARM_REG_NUM_SP:
		ret = td->td_frame->tf_usr_sp;
		break;
	default:
		ret = *((register_t*)&td->td_frame->tf_r0 + reg);
		break;
	}

	return (ret);
}

static u_int
ptrace_get_usr_int(void* cookie, vm_offset_t offset, u_int* val)
{
	struct thread *td = cookie;
	u_int error;

	error = ptrace_read_int(td, offset, val);

	return (error);
}

/**
 * This function parses current instruction opcode and decodes
 * any possible jump (change in PC) which might occur after
 * the instruction is executed.
 *
 * @param     td                Thread structure of analysed task
 * @param     cur_instr         Currently executed instruction
 * @param     alt_next_address  Pointer to the variable where
 *                              the destination address of the
 *                              jump instruction shall be stored.
 *
 * @return    <0>               when jump is possible
 *            <EINVAL>          otherwise
 */
static int
ptrace_get_alternative_next(struct thread *td, uint32_t cur_instr,
    uint32_t *alt_next_address)
{
	int error;

	if (inst_branch(cur_instr) || inst_call(cur_instr) ||
	    inst_return(cur_instr)) {
		error = arm_predict_branch(td, cur_instr, td->td_frame->tf_pc,
		    alt_next_address, ptrace_get_usr_reg, ptrace_get_usr_int);

		return (error);
	}

	return (EINVAL);
}

int
ptrace_single_step(struct thread *td)
{
	struct proc *p;
	int error, error_alt;
	uint32_t cur_instr, alt_next = 0;

	/* TODO: This needs to be updated for Thumb-2 */
	if ((td->td_frame->tf_spsr & PSR_T) != 0)
		return (EINVAL);

	KASSERT(td->td_md.md_ptrace_instr == 0,
	 ("Didn't clear single step"));
	KASSERT(td->td_md.md_ptrace_instr_alt == 0,
	 ("Didn't clear alternative single step"));
	p = td->td_proc;
	PROC_UNLOCK(p);

	error = ptrace_read_int(td, td->td_frame->tf_pc,
	    &cur_instr);
	if (error)
		goto out;

	error = ptrace_read_int(td, td->td_frame->tf_pc + INSN_SIZE,
	    &td->td_md.md_ptrace_instr);
	if (error == 0) {
		error = ptrace_write_int(td, td->td_frame->tf_pc + INSN_SIZE,
		    PTRACE_BREAKPOINT);
		if (error) {
			td->td_md.md_ptrace_instr = 0;
		} else {
			td->td_md.md_ptrace_addr = td->td_frame->tf_pc +
			    INSN_SIZE;
		}
	}

	error_alt = ptrace_get_alternative_next(td, cur_instr, &alt_next);
	if (error_alt == 0) {
		error_alt = ptrace_read_int(td, alt_next,
		    &td->td_md.md_ptrace_instr_alt);
		if (error_alt) {
			td->td_md.md_ptrace_instr_alt = 0;
		} else {
			error_alt = ptrace_write_int(td, alt_next,
			    PTRACE_BREAKPOINT);
			if (error_alt)
				td->td_md.md_ptrace_instr_alt = 0;
			else
				td->td_md.md_ptrace_addr_alt = alt_next;
		}
	}

out:
	PROC_LOCK(p);
	return ((error != 0) && (error_alt != 0));
}

int
ptrace_clear_single_step(struct thread *td)
{
	struct proc *p;

	/* TODO: This needs to be updated for Thumb-2 */
	if ((td->td_frame->tf_spsr & PSR_T) != 0)
		return (EINVAL);

	if (td->td_md.md_ptrace_instr != 0) {
		p = td->td_proc;
		PROC_UNLOCK(p);
		ptrace_write_int(td, td->td_md.md_ptrace_addr,
		    td->td_md.md_ptrace_instr);
		PROC_LOCK(p);
		td->td_md.md_ptrace_instr = 0;
	}

	if (td->td_md.md_ptrace_instr_alt != 0) {
		p = td->td_proc;
		PROC_UNLOCK(p);
		ptrace_write_int(td, td->td_md.md_ptrace_addr_alt,
		    td->td_md.md_ptrace_instr_alt);
		PROC_LOCK(p);
		td->td_md.md_ptrace_instr_alt = 0;
	}

	return (0);
}

int
ptrace_set_pc(struct thread *td, unsigned long addr)
{
	td->td_frame->tf_pc = addr;
	return (0);
}

int
arm_predict_branch(void *cookie, u_int insn, register_t pc, register_t *new_pc,
    u_int (*fetch_reg)(void*, int),
    u_int (*read_int)(void*, vm_offset_t, u_int*))
{
	u_int addr, nregs, offset = 0;
	int error = 0;

	switch ((insn >> 24) & 0xf) {
	case 0x2:	/* add pc, reg1, #value */
	case 0x0:	/* add pc, reg1, reg2, lsl #offset */
		addr = fetch_reg(cookie, (insn >> 16) & 0xf);
		if (((insn >> 16) & 0xf) == 15)
			addr += 8;
		if (insn & 0x0200000) {
			offset = (insn >> 7) & 0x1e;
			offset = (insn & 0xff) << (32 - offset) |
			    (insn & 0xff) >> offset;
		} else {

			offset = fetch_reg(cookie, insn & 0x0f);
			if ((insn & 0x0000ff0) != 0x00000000) {
				if (insn & 0x10)
					nregs = fetch_reg(cookie,
					    (insn >> 8) & 0xf);
				else
					nregs = (insn >> 7) & 0x1f;
				switch ((insn >> 5) & 3) {
				case 0:
					/* lsl */
					offset = offset << nregs;
					break;
				case 1:
					/* lsr */
					offset = offset >> nregs;
					break;
				default:
					break; /* XXX */
				}

			}
			*new_pc = addr + offset;
			return (0);

		}

	case 0xa:	/* b ... */
	case 0xb:	/* bl ... */
		addr = ((insn << 2) & 0x03ffffff);
		if (addr & 0x02000000)
			addr |= 0xfc000000;
		*new_pc = (pc + 8 + addr);
		return (0);
	case 0x7:	/* ldr pc, [pc, reg, lsl #2] */
		addr = fetch_reg(cookie, insn & 0xf);
		addr = pc + 8 + (addr << 2);
		error = read_int(cookie, addr, &addr);
		*new_pc = addr;
		return (error);
	case 0x1:	/* mov pc, reg */
		*new_pc = fetch_reg(cookie, insn & 0xf);
		return (0);
	case 0x4:
	case 0x5:	/* ldr pc, [reg] */
		addr = fetch_reg(cookie, (insn >> 16) & 0xf);
		/* ldr pc, [reg, #offset] */
		if (insn & (1 << 24))
			offset = insn & 0xfff;
		if (insn & 0x00800000)
			addr += offset;
		else
			addr -= offset;
		error = read_int(cookie, addr, &addr);
		*new_pc = addr;

		return (error);
	case 0x8:	/* ldmxx reg, {..., pc} */
	case 0x9:
		addr = fetch_reg(cookie, (insn >> 16) & 0xf);
		nregs = (insn  & 0x5555) + ((insn  >> 1) & 0x5555);
		nregs = (nregs & 0x3333) + ((nregs >> 2) & 0x3333);
		nregs = (nregs + (nregs >> 4)) & 0x0f0f;
		nregs = (nregs + (nregs >> 8)) & 0x001f;
		switch ((insn >> 23) & 0x3) {
		case 0x0:	/* ldmda */
			addr = addr - 0;
			break;
		case 0x1:	/* ldmia */
			addr = addr + 0 + ((nregs - 1) << 2);
			break;
		case 0x2:	/* ldmdb */
			addr = addr - 4;
			break;
		case 0x3:	/* ldmib */
			addr = addr + 4 + ((nregs - 1) << 2);
			break;
		}
		error = read_int(cookie, addr, &addr);
		*new_pc = addr;

		return (error);
	default:
		return (EINVAL);
	}
}
