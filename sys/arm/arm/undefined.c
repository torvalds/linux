/*	$NetBSD: undefined.c,v 1.22 2003/11/29 22:21:29 bjh21 Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Ben Harris.
 * Copyright (c) 1995 Mark Brinicombe.
 * Copyright (c) 1995 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * undefined.c
 *
 * Fault handler
 *
 * Created      : 06/01/95
 */


#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/vmmeter.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/signalvar.h>
#include <sys/ptrace.h>
#include <sys/vmmeter.h>
#ifdef KDB
#include <sys/kdb.h>
#endif

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <machine/armreg.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/undefined.h>
#include <machine/trap.h>

#include <machine/disassem.h>

#ifdef DDB
#include <ddb/db_output.h>
#endif

#ifdef KDB
#include <machine/db_machdep.h>
#endif

#define	ARM_COPROC_INSN(insn)	(((insn) & (1 << 27)) != 0)
#define	ARM_VFP_INSN(insn)	((((insn) & 0xfe000000) == 0xf2000000) || \
    (((insn) & 0xff100000) == 0xf4000000))
#define	ARM_COPROC(insn)	(((insn) >> 8) & 0xf)

#define	THUMB_32BIT_INSN(insn)	((insn) >= 0xe800)
#define	THUMB_COPROC_INSN(insn)	(((insn) & (3 << 26)) == (3 << 26))
#define	THUMB_COPROC_UNDEFINED(insn) (((insn) & 0x3e << 20) == 0)
#define	THUMB_VFP_INSN(insn)	(((insn) & (3 << 24)) == (3 << 24))
#define	THUMB_COPROC(insn)	(((insn) >> 8) & 0xf)

#define	COPROC_VFP	10

static int gdb_trapper(u_int, u_int, struct trapframe *, int);

LIST_HEAD(, undefined_handler) undefined_handlers[MAX_COPROCS];


void *
install_coproc_handler(int coproc, undef_handler_t handler)
{
	struct undefined_handler *uh;

	KASSERT(coproc >= 0 && coproc < MAX_COPROCS, ("bad coproc"));
	KASSERT(handler != NULL, ("handler is NULL")); /* Used to be legal. */

	/* XXX: M_TEMP??? */
	uh = malloc(sizeof(*uh), M_TEMP, M_WAITOK);
	uh->uh_handler = handler;
	install_coproc_handler_static(coproc, uh);
	return uh;
}

void
install_coproc_handler_static(int coproc, struct undefined_handler *uh)
{

	LIST_INSERT_HEAD(&undefined_handlers[coproc], uh, uh_link);
}

void
remove_coproc_handler(void *cookie)
{
	struct undefined_handler *uh = cookie;

	LIST_REMOVE(uh, uh_link);
	free(uh, M_TEMP);
}


static int
gdb_trapper(u_int addr, u_int insn, struct trapframe *frame, int code)
{
	struct thread *td;
	ksiginfo_t ksi;
	int error;

	td = (curthread == NULL) ? &thread0 : curthread;

	if (insn == GDB_BREAKPOINT || insn == GDB5_BREAKPOINT) {
		if (code == FAULT_USER) {
			ksiginfo_init_trap(&ksi);
			ksi.ksi_signo = SIGTRAP;
			ksi.ksi_code = TRAP_BRKPT;
			ksi.ksi_addr = (u_int32_t *)addr;
			trapsignal(td, &ksi);
			return 0;
		}
#if 0
#ifdef KGDB
		return !kgdb_trap(T_BREAKPOINT, frame);
#endif
#endif
	}

	if (code == FAULT_USER) {
		/* TODO: No support for ptrace from Thumb-2 */
		if ((frame->tf_spsr & PSR_T) == 0 &&
		    insn == PTRACE_BREAKPOINT) {
			PROC_LOCK(td->td_proc);
			_PHOLD(td->td_proc);
			error = ptrace_clear_single_step(td);
			_PRELE(td->td_proc);
			PROC_UNLOCK(td->td_proc);
			if (error == 0) {
				ksiginfo_init_trap(&ksi);
				ksi.ksi_signo = SIGTRAP;
				ksi.ksi_code = TRAP_TRACE;
				ksi.ksi_addr = (u_int32_t *)addr;
				trapsignal(td, &ksi);
				return (0);
			}
		}
	}
	
	return 1;
}

static struct undefined_handler gdb_uh;

void
undefined_init(void)
{
	int loop;

	/* Not actually necessary -- the initialiser is just NULL */
	for (loop = 0; loop < MAX_COPROCS; ++loop)
		LIST_INIT(&undefined_handlers[loop]);

	/* Install handler for GDB breakpoints */
	gdb_uh.uh_handler = gdb_trapper;
	install_coproc_handler_static(0, &gdb_uh);
}


void
undefinedinstruction(struct trapframe *frame)
{
	struct thread *td;
	u_int fault_pc;
	int fault_instruction;
	int fault_code;
	int coprocessor;
	struct undefined_handler *uh;
#ifdef VERBOSE_ARM32
	int s;
#endif
	ksiginfo_t ksi;

	/* Enable interrupts if they were enabled before the exception. */
	if (__predict_true(frame->tf_spsr & PSR_I) == 0)
		enable_interrupts(PSR_I);
	if (__predict_true(frame->tf_spsr & PSR_F) == 0)
		enable_interrupts(PSR_F);

	VM_CNT_INC(v_trap);

	fault_pc = frame->tf_pc;

	/*
	 * Get the current thread/proc structure or thread0/proc0 if there is
	 * none.
	 */
	td = curthread == NULL ? &thread0 : curthread;

	coprocessor = 0;
	if ((frame->tf_spsr & PSR_T) == 0) {
		/*
		 * Make sure the program counter is correctly aligned so we
		 * don't take an alignment fault trying to read the opcode.
		 */
		if (__predict_false((fault_pc & 3) != 0)) {
			ksiginfo_init_trap(&ksi);
			ksi.ksi_signo = SIGILL;
			ksi.ksi_code = ILL_ILLADR;
			ksi.ksi_addr = (u_int32_t *)(intptr_t) fault_pc;
			trapsignal(td, &ksi);
			userret(td, frame);
			return;
		}

		/*
		 * Should use fuword() here .. but in the interests of
		 * squeezing every bit of speed we will just use ReadWord().
		 * We know the instruction can be read as was just executed
		 * so this will never fail unless the kernel is screwed up
		 * in which case it does not really matter does it ?
		 */

		fault_instruction = *(u_int32_t *)fault_pc;

		/* Check for coprocessor instruction */

		/*
		 * According to the datasheets you only need to look at bit
		 * 27 of the instruction to tell the difference between and
		 * undefined instruction and a coprocessor instruction
		 * following an undefined instruction trap.
		 */

		if (ARM_COPROC_INSN(fault_instruction))
			coprocessor = ARM_COPROC(fault_instruction);
		else {          /* check for special instructions */
			if (ARM_VFP_INSN(fault_instruction))
				coprocessor = COPROC_VFP; /* vfp / simd */
		}
	} else {
#if __ARM_ARCH >= 7
		fault_instruction = *(uint16_t *)fault_pc;
		if (THUMB_32BIT_INSN(fault_instruction)) {
			fault_instruction <<= 16;
			fault_instruction |= *(uint16_t *)(fault_pc + 2);

			/*
			 * Is it a Coprocessor, Advanced SIMD, or
			 * Floating-point instruction.
			 */
			if (THUMB_COPROC_INSN(fault_instruction)) {
				if (THUMB_COPROC_UNDEFINED(fault_instruction)) {
					/* undefined insn */
				} else if (THUMB_VFP_INSN(fault_instruction))
					coprocessor = COPROC_VFP;
				else
					coprocessor =
					    THUMB_COPROC(fault_instruction);
			}
		}
#else
		/*
		 * No support for Thumb-2 on this cpu
		 */
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGILL;
		ksi.ksi_code = ILL_ILLADR;
		ksi.ksi_addr = (u_int32_t *)(intptr_t) fault_pc;
		trapsignal(td, &ksi);
		userret(td, frame);
		return;
#endif
	}

	if ((frame->tf_spsr & PSR_MODE) == PSR_USR32_MODE) {
		/*
		 * Modify the fault_code to reflect the USR/SVC state at
		 * time of fault.
		 */
		fault_code = FAULT_USER;
		td->td_frame = frame;
	} else
		fault_code = 0;

	/* OK this is were we do something about the instruction. */
	LIST_FOREACH(uh, &undefined_handlers[coprocessor], uh_link)
	    if (uh->uh_handler(fault_pc, fault_instruction, frame,
			       fault_code) == 0)
		    break;

	if (uh == NULL && (fault_code & FAULT_USER)) {
		/* Fault has not been handled */
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGILL;
		ksi.ksi_code = ILL_ILLOPC;
		ksi.ksi_addr = (u_int32_t *)(intptr_t) fault_pc;
		trapsignal(td, &ksi);
	}

	if ((fault_code & FAULT_USER) == 0) {
		if (fault_instruction == KERNEL_BREAKPOINT) {
#ifdef KDB
			kdb_trap(T_BREAKPOINT, 0, frame);
#else
			printf("No debugger in kernel.\n");
#endif
			return;
		}
		else
			panic("Undefined instruction in kernel.\n");
	}

	userret(td, frame);
}
