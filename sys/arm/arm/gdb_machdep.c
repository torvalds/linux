/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Olivier Houchard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <machine/gdb_machdep.h>
#include <machine/db_machdep.h>
#include <machine/vmparam.h>
#include <machine/pcb.h>
#include <machine/trap.h>
#include <machine/frame.h>
#include <machine/endian.h>

#include <gdb/gdb.h>

static register_t stacktest;

void *
gdb_cpu_getreg(int regnum, size_t *regsz)
{

	*regsz = gdb_cpu_regsz(regnum);

	if (kdb_thread == curthread) {
		if (regnum < 13)
			return (&kdb_frame->tf_r0 + regnum);
		if (regnum == 13)
			return (&kdb_frame->tf_svc_sp);
		if (regnum == 14)
			return (&kdb_frame->tf_svc_lr);
		if (regnum == 15)
			return (&kdb_frame->tf_pc);
		if (regnum == 25)
			return (&kdb_frame->tf_spsr);
	}

	switch (regnum) {
	case 4:  return (&kdb_thrctx->pcb_regs.sf_r4);
	case 5:  return (&kdb_thrctx->pcb_regs.sf_r5);
	case 6:  return (&kdb_thrctx->pcb_regs.sf_r6);
	case 7:  return (&kdb_thrctx->pcb_regs.sf_r7);
	case 8:  return (&kdb_thrctx->pcb_regs.sf_r8);
	case 9:  return (&kdb_thrctx->pcb_regs.sf_r9);
	case 10:  return (&kdb_thrctx->pcb_regs.sf_r10);
	case 11:  return (&kdb_thrctx->pcb_regs.sf_r11);
	case 12:  return (&kdb_thrctx->pcb_regs.sf_r12);
	case 13:  stacktest = kdb_thrctx->pcb_regs.sf_sp + 5 * 4;
		  return (&stacktest);
	case 15:
		  /*
		   * On context switch, the PC is not put in the PCB, but
		   * we can retrieve it from the stack.
		   */
		  if (kdb_thrctx->pcb_regs.sf_sp > KERNBASE) {
			  kdb_thrctx->pcb_regs.sf_pc = *(register_t *)
			      (kdb_thrctx->pcb_regs.sf_sp + 4 * 4);
			  return (&kdb_thrctx->pcb_regs.sf_pc);
		  }
	}

	return (NULL);
}

void
gdb_cpu_setreg(int regnum, void *val)
{

	switch (regnum) {
	case GDB_REG_PC:
		if (kdb_thread  == curthread)
			kdb_frame->tf_pc = *(register_t *)val;
	}
}

int
gdb_cpu_signal(int type, int code)
{

	switch (type) {
	case T_BREAKPOINT: return (SIGTRAP);
	}
	return (SIGEMT);
}
