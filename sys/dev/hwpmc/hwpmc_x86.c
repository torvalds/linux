/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005,2008 Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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
#include <sys/bus.h>
#include <sys/pmc.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/intr_machdep.h>
#if (__FreeBSD_version >= 1100000)
#include <x86/apicvar.h>
#else
#include <machine/apicvar.h>
#endif
#include <machine/pmc_mdep.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include "hwpmc_soft.h"

/*
 * Attempt to walk a user call stack using a too-simple algorithm.
 * In the general case we need unwind information associated with
 * the executable to be able to walk the user stack.
 *
 * We are handed a trap frame laid down at the time the PMC interrupt
 * was taken.  If the application is using frame pointers, the saved
 * PC value could be:
 * a. at the beginning of a function before the stack frame is laid
 *    down,
 * b. just before a 'ret', after the stack frame has been taken off,
 * c. somewhere else in the function with a valid stack frame being
 *    present,
 *
 * If the application is not using frame pointers, this algorithm will
 * fail to yield an interesting call chain.
 *
 * TODO: figure out a way to use unwind information.
 */

int
pmc_save_user_callchain(uintptr_t *cc, int nframes, struct trapframe *tf)
{
	int n;
	uint32_t instr;
	uintptr_t fp, oldfp, pc, r, sp;

	KASSERT(TRAPF_USERMODE(tf), ("[x86,%d] Not a user trap frame tf=%p",
	    __LINE__, (void *) tf));

	pc = PMC_TRAPFRAME_TO_PC(tf);
	oldfp = fp = PMC_TRAPFRAME_TO_FP(tf);
	sp = PMC_TRAPFRAME_TO_USER_SP(tf);

	*cc++ = pc; n = 1;

	r = fp + sizeof(uintptr_t); /* points to return address */

	if (!PMC_IN_USERSPACE(pc))
		return (n);

	if (copyin((void *) pc, &instr, sizeof(instr)) != 0)
		return (n);

	if (PMC_AT_FUNCTION_PROLOGUE_PUSH_BP(instr) ||
	    PMC_AT_FUNCTION_EPILOGUE_RET(instr)) { /* ret */
		if (copyin((void *) sp, &pc, sizeof(pc)) != 0)
			return (n);
	} else if (PMC_AT_FUNCTION_PROLOGUE_MOV_SP_BP(instr)) {
		sp += sizeof(uintptr_t);
		if (copyin((void *) sp, &pc, sizeof(pc)) != 0)
			return (n);
	} else if (copyin((void *) r, &pc, sizeof(pc)) != 0 ||
	    copyin((void *) fp, &fp, sizeof(fp)) != 0)
		return (n);

	for (; n < nframes;) {
		if (pc == 0 || !PMC_IN_USERSPACE(pc))
			break;

		*cc++ = pc; n++;

		if (fp < oldfp)
			break;

		r = fp + sizeof(uintptr_t); /* address of return address */
		oldfp = fp;

		if (copyin((void *) r, &pc, sizeof(pc)) != 0 ||
		    copyin((void *) fp, &fp, sizeof(fp)) != 0)
			break;
	}

	return (n);
}

/*
 * Walking the kernel call stack.
 *
 * We are handed the trap frame laid down at the time the PMC
 * interrupt was taken.  The saved PC could be:
 * a. in the lowlevel trap handler, meaning that there isn't a C stack
 *    to traverse,
 * b. at the beginning of a function before the stack frame is laid
 *    down,
 * c. just before a 'ret', after the stack frame has been taken off,
 * d. somewhere else in a function with a valid stack frame being
 *    present.
 *
 * In case (d), the previous frame pointer is at [%ebp]/[%rbp] and
 * the return address is at [%ebp+4]/[%rbp+8].
 *
 * For cases (b) and (c), the return address is at [%esp]/[%rsp] and
 * the frame pointer doesn't need to be changed when going up one
 * level in the stack.
 *
 * For case (a), we check if the PC lies in low-level trap handling
 * code, and if so we terminate our trace.
 */

int
pmc_save_kernel_callchain(uintptr_t *cc, int nframes, struct trapframe *tf)
{
	int n;
	uint32_t instr;
	uintptr_t fp, pc, r, sp, stackstart, stackend;
	struct thread *td;

	KASSERT(TRAPF_USERMODE(tf) == 0,("[x86,%d] not a kernel backtrace",
	    __LINE__));

	td = curthread;
	pc = PMC_TRAPFRAME_TO_PC(tf);
	fp = PMC_TRAPFRAME_TO_FP(tf);
	sp = PMC_TRAPFRAME_TO_KERNEL_SP(tf);

	*cc++ = pc;
	r = fp + sizeof(uintptr_t); /* points to return address */

	if (nframes <= 1)
		return (1);

	stackstart = (uintptr_t) td->td_kstack;
	stackend = (uintptr_t) td->td_kstack + td->td_kstack_pages * PAGE_SIZE;

	if (PMC_IN_TRAP_HANDLER(pc) ||
	    !PMC_IN_KERNEL(pc) ||
	    !PMC_IN_KERNEL_STACK(r, stackstart, stackend) ||
	    !PMC_IN_KERNEL_STACK(sp, stackstart, stackend) ||
	    !PMC_IN_KERNEL_STACK(fp, stackstart, stackend))
		return (1);

	instr = *(uint32_t *) pc;

	/*
	 * Determine whether the interrupted function was in the
	 * processing of either laying down its stack frame or taking
	 * it off.
	 *
	 * If we haven't started laying down a stack frame, or are
	 * just about to return, then our caller's address is at
	 * *sp, and we don't have a frame to unwind.
	 */
	if (PMC_AT_FUNCTION_PROLOGUE_PUSH_BP(instr) ||
	    PMC_AT_FUNCTION_EPILOGUE_RET(instr))
		pc = *(uintptr_t *) sp;
	else if (PMC_AT_FUNCTION_PROLOGUE_MOV_SP_BP(instr)) {
		/*
		 * The code was midway through laying down a frame.
		 * At this point sp[0] has a frame back pointer,
		 * and the caller's address is therefore at sp[1].
		 */
		sp += sizeof(uintptr_t);
		if (!PMC_IN_KERNEL_STACK(sp, stackstart, stackend))
			return (1);
		pc = *(uintptr_t *) sp;
	} else {
		/*
		 * Not in the function prologue or epilogue.
		 */
		pc = *(uintptr_t *) r;
		fp = *(uintptr_t *) fp;
	}

	for (n = 1; n < nframes; n++) {
		*cc++ = pc;

		if (PMC_IN_TRAP_HANDLER(pc))
			break;

		r = fp + sizeof(uintptr_t);
		if (!PMC_IN_KERNEL_STACK(fp, stackstart, stackend) ||
		    !PMC_IN_KERNEL_STACK(r, stackstart, stackend))
			break;
		pc = *(uintptr_t *) r;
		fp = *(uintptr_t *) fp;
	}

	return (n);
}

/*
 * Machine dependent initialization for x86 class platforms.
 */

struct pmc_mdep *
pmc_md_initialize()
{
	int i;
	struct pmc_mdep *md;

	/* determine the CPU kind */
	if (cpu_vendor_id == CPU_VENDOR_AMD)
		md = pmc_amd_initialize();
	else if (cpu_vendor_id == CPU_VENDOR_INTEL)
		md = pmc_intel_initialize();
	else
		return (NULL);

	/* disallow sampling if we do not have an LAPIC */
	if (md != NULL && !lapic_enable_pmc())
		for (i = 0; i < md->pmd_nclass; i++) {
			if (i == PMC_CLASS_INDEX_SOFT)
				continue;
			md->pmd_classdep[i].pcd_caps &= ~PMC_CAP_INTERRUPT;
		}

	return (md);
}

void
pmc_md_finalize(struct pmc_mdep *md)
{

	lapic_disable_pmc();
	if (cpu_vendor_id == CPU_VENDOR_AMD)
		pmc_amd_finalize(md);
	else if (cpu_vendor_id == CPU_VENDOR_INTEL)
		pmc_intel_finalize(md);
	else
		KASSERT(0, ("[x86,%d] Unknown vendor", __LINE__));
}
