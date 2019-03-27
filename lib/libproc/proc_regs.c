/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation.
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

#include <sys/types.h>
#include <sys/ptrace.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "_libproc.h"

int
proc_regget(struct proc_handle *phdl, proc_reg_t reg, unsigned long *regvalue)
{
	struct reg regs;

	if (phdl->status == PS_DEAD || phdl->status == PS_UNDEAD ||
	    phdl->status == PS_IDLE) {
		errno = ENOENT;
		return (-1);
	}
	memset(&regs, 0, sizeof(regs));
	if (ptrace(PT_GETREGS, proc_getpid(phdl), (caddr_t)&regs, 0) < 0)
		return (-1);
	switch (reg) {
	case REG_PC:
#if defined(__aarch64__)
		*regvalue = regs.elr;
#elif defined(__amd64__)
		*regvalue = regs.r_rip;
#elif defined(__arm__)
		*regvalue = regs.r_pc;
#elif defined(__i386__)
		*regvalue = regs.r_eip;
#elif defined(__mips__)
		*regvalue = regs.r_regs[PC];
#elif defined(__powerpc__)
		*regvalue = regs.pc;
#elif defined(__riscv)
		*regvalue = regs.sepc;
#endif
		break;
	case REG_SP:
#if defined(__aarch64__)
		*regvalue = regs.sp;
#elif defined(__amd64__)
		*regvalue = regs.r_rsp;
#elif defined(__arm__)
		*regvalue = regs.r_sp;
#elif defined(__i386__)
		*regvalue = regs.r_esp;
#elif defined(__mips__)
		*regvalue = regs.r_regs[SP];
#elif defined(__powerpc__)
		*regvalue = regs.fixreg[1];
#elif defined(__riscv)
		*regvalue = regs.sp;
#endif
		break;
	default:
		DPRINTFX("ERROR: no support for reg number %d", reg);
		return (-1);
	}

	return (0);
}

int
proc_regset(struct proc_handle *phdl, proc_reg_t reg, unsigned long regvalue)
{
	struct reg regs;

	if (phdl->status == PS_DEAD || phdl->status == PS_UNDEAD ||
	    phdl->status == PS_IDLE) {
		errno = ENOENT;
		return (-1);
	}
	if (ptrace(PT_GETREGS, proc_getpid(phdl), (caddr_t)&regs, 0) < 0)
		return (-1);
	switch (reg) {
	case REG_PC:
#if defined(__aarch64__)
		regs.elr = regvalue;
#elif defined(__amd64__)
		regs.r_rip = regvalue;
#elif defined(__arm__)
		regs.r_pc = regvalue;
#elif defined(__i386__)
		regs.r_eip = regvalue;
#elif defined(__mips__)
		regs.r_regs[PC] = regvalue;
#elif defined(__powerpc__)
		regs.pc = regvalue;
#elif defined(__riscv)
		regs.sepc = regvalue;
#endif
		break;
	case REG_SP:
#if defined(__aarch64__)
		regs.sp = regvalue;
#elif defined(__amd64__)
		regs.r_rsp = regvalue;
#elif defined(__arm__)
		regs.r_sp = regvalue;
#elif defined(__i386__)
		regs.r_esp = regvalue;
#elif defined(__mips__)
		regs.r_regs[PC] = regvalue;
#elif defined(__powerpc__)
		regs.fixreg[1] = regvalue;
#elif defined(__riscv)
		regs.sp = regvalue;
#endif
		break;
	default:
		DPRINTFX("ERROR: no support for reg number %d", reg);
		return (-1);
	}
	if (ptrace(PT_SETREGS, proc_getpid(phdl), (caddr_t)&regs, 0) < 0)
		return (-1);

	return (0);
}
