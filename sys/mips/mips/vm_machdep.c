/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 *	from: src/sys/i386/i386/vm_machdep.c,v 1.132.2.2 2000/08/26 04:19:26 yokota
 *	JNPR: vm_machdep.c,v 1.8.2.2 2007/08/16 15:59:17 girish
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <machine/abi.h>
#include <machine/cache.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/tls.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_param.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <sys/user.h>
#include <sys/mbuf.h>

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct pcb *pcb2;

	if ((flags & RFPROC) == 0)
		return;
	/* It is assumed that the vm_thread_alloc called
	 * cpu_thread_alloc() before cpu_fork is called.
	 */

	/* Point the pcb to the top of the stack */
	pcb2 = td2->td_pcb;

	/* Copy td1's pcb, note that in this case
	 * our pcb also includes the td_frame being copied
	 * too. The older mips2 code did an additional copy
	 * of the td_frame, for us that's not needed any
	 * longer (this copy does them both) 
	 */
	bcopy(td1->td_pcb, pcb2, sizeof(*pcb2));

	/* Point mdproc and then copy over td1's contents
	 * md_proc is empty for MIPS
	 */
	td2->td_md.md_flags = td1->td_md.md_flags & MDTD_FPUSED;

	/*
	 * Set up return-value registers as fork() libc stub expects.
	 */
	td2->td_frame->v0 = 0;
	td2->td_frame->v1 = 1;
	td2->td_frame->a3 = 0;

	if (td1 == PCPU_GET(fpcurthread))
		MipsSaveCurFPState(td1);

	pcb2->pcb_context[PCB_REG_RA] = (register_t)(intptr_t)fork_trampoline;
	/* Make sp 64-bit aligned */
	pcb2->pcb_context[PCB_REG_SP] = (register_t)(((vm_offset_t)td2->td_pcb &
	    ~(sizeof(__int64_t) - 1)) - CALLFRAME_SIZ);
	pcb2->pcb_context[PCB_REG_S0] = (register_t)(intptr_t)fork_return;
	pcb2->pcb_context[PCB_REG_S1] = (register_t)(intptr_t)td2;
	pcb2->pcb_context[PCB_REG_S2] = (register_t)(intptr_t)td2->td_frame;
	pcb2->pcb_context[PCB_REG_SR] = mips_rd_status() &
	    (MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_INT_MASK);
	/*
	 * FREEBSD_DEVELOPERS_FIXME:
	 * Setup any other CPU-Specific registers (Not MIPS Standard)
	 * and/or bits in other standard MIPS registers (if CPU-Specific)
	 *  that are needed.
	 */

	td2->td_md.md_tls = td1->td_md.md_tls;
	td2->td_md.md_tls_tcb_offset = td1->td_md.md_tls_tcb_offset;
	td2->td_md.md_saved_intr = MIPS_SR_INT_IE;
	td2->td_md.md_spinlock_count = 1;
#ifdef CPU_CNMIPS
	if (td1->td_md.md_flags & MDTD_COP2USED) {
		if (td1->td_md.md_cop2owner == COP2_OWNER_USERLAND) {
			if (td1->td_md.md_ucop2)
				octeon_cop2_save(td1->td_md.md_ucop2);
			else
				panic("cpu_fork: ucop2 is NULL but COP2 is enabled");
		}
		else {
			if (td1->td_md.md_cop2)
				octeon_cop2_save(td1->td_md.md_cop2);
			else
				panic("cpu_fork: cop2 is NULL but COP2 is enabled");
		}
	}

	if (td1->td_md.md_cop2) {
		td2->td_md.md_cop2 = octeon_cop2_alloc_ctx();
		memcpy(td2->td_md.md_cop2, td1->td_md.md_cop2, 
			sizeof(*td1->td_md.md_cop2));
	}
	if (td1->td_md.md_ucop2) {
		td2->td_md.md_ucop2 = octeon_cop2_alloc_ctx();
		memcpy(td2->td_md.md_ucop2, td1->td_md.md_ucop2, 
			sizeof(*td1->td_md.md_ucop2));
	}
	td2->td_md.md_cop2owner = td1->td_md.md_cop2owner;
	pcb2->pcb_context[PCB_REG_SR] |= MIPS_SR_PX | MIPS_SR_UX | MIPS_SR_KX | MIPS_SR_SX;
	/* Clear COP2 bits for userland & kernel */
	td2->td_frame->sr &= ~MIPS_SR_COP_2_BIT;
	pcb2->pcb_context[PCB_REG_SR] &= ~MIPS_SR_COP_2_BIT;
#endif
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_fork_kthread_handler(struct thread *td, void (*func)(void *), void *arg)
{
	/*
	 * Note that the trap frame follows the args, so the function
	 * is really called like this:	func(arg, frame);
	 */
	td->td_pcb->pcb_context[PCB_REG_S0] = (register_t)(intptr_t)func;
	td->td_pcb->pcb_context[PCB_REG_S1] = (register_t)(intptr_t)arg;
}

void
cpu_exit(struct thread *td)
{
}

void
cpu_thread_exit(struct thread *td)
{

	if (PCPU_GET(fpcurthread) == td)
		PCPU_GET(fpcurthread) = (struct thread *)0;
#ifdef  CPU_CNMIPS
	if (td->td_md.md_cop2)
		memset(td->td_md.md_cop2, 0,
			sizeof(*td->td_md.md_cop2));
	if (td->td_md.md_ucop2)
		memset(td->td_md.md_ucop2, 0,
			sizeof(*td->td_md.md_ucop2));
#endif
}

void
cpu_thread_free(struct thread *td)
{
#ifdef  CPU_CNMIPS
	if (td->td_md.md_cop2)
		octeon_cop2_free_ctx(td->td_md.md_cop2);
	if (td->td_md.md_ucop2)
		octeon_cop2_free_ctx(td->td_md.md_ucop2);
	td->td_md.md_cop2 = NULL;
	td->td_md.md_ucop2 = NULL;
#endif
}

void
cpu_thread_clean(struct thread *td)
{
}

void
cpu_thread_swapin(struct thread *td)
{
	pt_entry_t *pte;
	int i;

	/*
	 * The kstack may be at a different physical address now.
	 * Cache the PTEs for the Kernel stack in the machine dependent
	 * part of the thread struct so cpu_switch() can quickly map in
	 * the pcb struct and kernel stack.
	 */
	for (i = 0; i < KSTACK_PAGES; i++) {
		pte = pmap_pte(kernel_pmap, td->td_kstack + i * PAGE_SIZE);
		td->td_md.md_upte[i] = *pte & ~TLBLO_SWBITS_MASK;
	}
}

void
cpu_thread_swapout(struct thread *td)
{
}

void
cpu_thread_alloc(struct thread *td)
{
	pt_entry_t *pte;
	int i;

	KASSERT((td->td_kstack & (1 << PAGE_SHIFT)) == 0, ("kernel stack must be aligned."));
	td->td_pcb = (struct pcb *)(td->td_kstack +
	    td->td_kstack_pages * PAGE_SIZE) - 1;
	td->td_frame = &td->td_pcb->pcb_regs;

	for (i = 0; i < KSTACK_PAGES; i++) {
		pte = pmap_pte(kernel_pmap, td->td_kstack + i * PAGE_SIZE);
		td->td_md.md_upte[i] = *pte & ~TLBLO_SWBITS_MASK;
	}
}

void
cpu_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *locr0 = td->td_frame;
	unsigned int code;
	int quad_syscall;

	code = locr0->v0;
	quad_syscall = 0;
#if defined(__mips_n32) || defined(__mips_n64)
#ifdef COMPAT_FREEBSD32
	if (code == SYS___syscall && SV_PROC_FLAG(td->td_proc, SV_ILP32))
		quad_syscall = 1;
#endif
#else
	if (code == SYS___syscall)
		quad_syscall = 1;
#endif

	if (code == SYS_syscall)
		code = locr0->a0;
	else if (code == SYS___syscall) {
		if (quad_syscall)
			code = _QUAD_LOWWORD ? locr0->a1 : locr0->a0;
		else
			code = locr0->a0;
	}

	switch (error) {
	case 0:
		if (quad_syscall && code != SYS_lseek) {
			/*
			 * System call invoked through the
			 * SYS___syscall interface but the
			 * return value is really just 32
			 * bits.
			 */
			locr0->v0 = td->td_retval[0];
			if (_QUAD_LOWWORD)
				locr0->v1 = td->td_retval[0];
			locr0->a3 = 0;
		} else {
			locr0->v0 = td->td_retval[0];
			locr0->v1 = td->td_retval[1];
			locr0->a3 = 0;
		}
		break;

	case ERESTART:
		locr0->pc = td->td_pcb->pcb_tpc;
		break;

	case EJUSTRETURN:
		break;	/* nothing to do */

	default:
		if (quad_syscall && code != SYS_lseek) {
			locr0->v0 = error;
			if (_QUAD_LOWWORD)
				locr0->v1 = error;
			locr0->a3 = 1;
		} else {
			locr0->v0 = error;
			locr0->a3 = 1;
		}
	}
}

/*
 * Initialize machine state, mostly pcb and trap frame for a new
 * thread, about to return to userspace.  Put enough state in the new
 * thread's PCB to get it to go back to the fork_return(), which
 * finalizes the thread state and handles peculiarities of the first
 * return to userspace for the new thread.
 */
void
cpu_copy_thread(struct thread *td, struct thread *td0)
{
	struct pcb *pcb2;

	/* Point the pcb to the top of the stack. */
	pcb2 = td->td_pcb;

	/*
	 * Copy the upcall pcb.  This loads kernel regs.
	 * Those not loaded individually below get their default
	 * values here.
	 *
	 * XXXKSE It might be a good idea to simply skip this as
	 * the values of the other registers may be unimportant.
	 * This would remove any requirement for knowing the KSE
	 * at this time (see the matching comment below for
	 * more analysis) (need a good safe default).
	 * In MIPS, the trapframe is the first element of the PCB
	 * and gets copied when we copy the PCB. No separate copy
	 * is needed.
	 */
	bcopy(td0->td_pcb, pcb2, sizeof(*pcb2));

	/*
	 * Set registers for trampoline to user mode.
	 */

	pcb2->pcb_context[PCB_REG_RA] = (register_t)(intptr_t)fork_trampoline;
	/* Make sp 64-bit aligned */
	pcb2->pcb_context[PCB_REG_SP] = (register_t)(((vm_offset_t)td->td_pcb &
	    ~(sizeof(__int64_t) - 1)) - CALLFRAME_SIZ);
	pcb2->pcb_context[PCB_REG_S0] = (register_t)(intptr_t)fork_return;
	pcb2->pcb_context[PCB_REG_S1] = (register_t)(intptr_t)td;
	pcb2->pcb_context[PCB_REG_S2] = (register_t)(intptr_t)td->td_frame;
	/* Dont set IE bit in SR. sched lock release will take care of it */
	pcb2->pcb_context[PCB_REG_SR] = mips_rd_status() &
	    (MIPS_SR_PX | MIPS_SR_KX | MIPS_SR_UX | MIPS_SR_INT_MASK);

	/*
	 * FREEBSD_DEVELOPERS_FIXME:
	 * Setup any other CPU-Specific registers (Not MIPS Standard)
	 * that are needed.
	 */

	/* Setup to release spin count in in fork_exit(). */
	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_intr = MIPS_SR_INT_IE;
#if 0
	    /* Maybe we need to fix this? */
	td->td_md.md_saved_sr = ( (MIPS_SR_COP_2_BIT | MIPS_SR_COP_0_BIT) |
	                          (MIPS_SR_PX | MIPS_SR_UX | MIPS_SR_KX | MIPS_SR_SX) |
	                          (MIPS_SR_INT_IE | MIPS_HARD_INT_MASK));
#endif
}

/*
 * Set that machine state for performing an upcall that starts
 * the entry function with the given argument.
 */
void
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
    stack_t *stack)
{
	struct trapframe *tf;
	register_t sp;

	sp = (((intptr_t)stack->ss_sp + stack->ss_size) & ~(STACK_ALIGN - 1)) -
	    CALLFRAME_SIZ;

	/*
	 * Set the trap frame to point at the beginning of the uts
	 * function.
	 */
	tf = td->td_frame;
	bzero(tf, sizeof(struct trapframe));
	tf->sp = sp;
	tf->pc = (register_t)(intptr_t)entry;
	/* 
	 * MIPS ABI requires T9 to be the same as PC 
	 * in subroutine entry point
	 */
	tf->t9 = (register_t)(intptr_t)entry; 
	tf->a0 = (register_t)(intptr_t)arg;

	/*
	 * Keep interrupt mask
	 */
	td->td_frame->sr = MIPS_SR_KSU_USER | MIPS_SR_EXL | MIPS_SR_INT_IE |
	    (mips_rd_status() & MIPS_SR_INT_MASK);
#if defined(__mips_n32) 
	td->td_frame->sr |= MIPS_SR_PX;
#elif  defined(__mips_n64)
	td->td_frame->sr |= MIPS_SR_PX | MIPS_SR_UX | MIPS_SR_KX;
#endif
/*	tf->sr |= (ALL_INT_MASK & idle_mask) | SR_INT_ENAB; */
	/**XXX the above may now be wrong -- mips2 implements this as panic */
	/*
	 * FREEBSD_DEVELOPERS_FIXME:
	 * Setup any other CPU-Specific registers (Not MIPS Standard)
	 * that are needed.
	 */
}

bool
cpu_exec_vmspace_reuse(struct proc *p __unused, vm_map_t map __unused)
{

	return (true);
}

int
cpu_procctl(struct thread *td __unused, int idtype __unused, id_t id __unused,
    int com __unused, void *data __unused)
{

	return (EINVAL);
}

/*
 * Software interrupt handler for queued VM system processing.
 */
void
swi_vm(void *dummy)
{

	if (busdma_swi_pending)
		busdma_swi();
}

int
cpu_set_user_tls(struct thread *td, void *tls_base)
{

#if defined(__mips_n64) && defined(COMPAT_FREEBSD32)
	if (td->td_proc && SV_PROC_FLAG(td->td_proc, SV_ILP32))
		td->td_md.md_tls_tcb_offset = TLS_TP_OFFSET + TLS_TCB_SIZE32;
	else
#endif
	td->td_md.md_tls_tcb_offset = TLS_TP_OFFSET + TLS_TCB_SIZE;
	td->td_md.md_tls = (char*)tls_base;
	if (td == curthread && cpuinfo.userlocal_reg == true) {
		mips_wr_userlocal((unsigned long)tls_base +
		    td->td_md.md_tls_tcb_offset);
	}

	return (0);
}

#ifdef DDB
#include <ddb/ddb.h>

#define DB_PRINT_REG(ptr, regname)			\
	db_printf("  %-12s %p\n", #regname, (void *)(intptr_t)((ptr)->regname))

#define DB_PRINT_REG_ARRAY(ptr, arrname, regname)	\
	db_printf("  %-12s %p\n", #regname, (void *)(intptr_t)((ptr)->arrname[regname]))

static void
dump_trapframe(struct trapframe *trapframe)
{

	db_printf("Trapframe at %p\n", trapframe);

	DB_PRINT_REG(trapframe, zero);
	DB_PRINT_REG(trapframe, ast);
	DB_PRINT_REG(trapframe, v0);
	DB_PRINT_REG(trapframe, v1);
	DB_PRINT_REG(trapframe, a0);
	DB_PRINT_REG(trapframe, a1);
	DB_PRINT_REG(trapframe, a2);
	DB_PRINT_REG(trapframe, a3);
#if defined(__mips_n32) || defined(__mips_n64)
	DB_PRINT_REG(trapframe, a4);
	DB_PRINT_REG(trapframe, a5);
	DB_PRINT_REG(trapframe, a6);
	DB_PRINT_REG(trapframe, a7);
	DB_PRINT_REG(trapframe, t0);
	DB_PRINT_REG(trapframe, t1);
	DB_PRINT_REG(trapframe, t2);
	DB_PRINT_REG(trapframe, t3);
#else
	DB_PRINT_REG(trapframe, t0);
	DB_PRINT_REG(trapframe, t1);
	DB_PRINT_REG(trapframe, t2);
	DB_PRINT_REG(trapframe, t3);
	DB_PRINT_REG(trapframe, t4);
	DB_PRINT_REG(trapframe, t5);
	DB_PRINT_REG(trapframe, t6);
	DB_PRINT_REG(trapframe, t7);
#endif
	DB_PRINT_REG(trapframe, s0);
	DB_PRINT_REG(trapframe, s1);
	DB_PRINT_REG(trapframe, s2);
	DB_PRINT_REG(trapframe, s3);
	DB_PRINT_REG(trapframe, s4);
	DB_PRINT_REG(trapframe, s5);
	DB_PRINT_REG(trapframe, s6);
	DB_PRINT_REG(trapframe, s7);
	DB_PRINT_REG(trapframe, t8);
	DB_PRINT_REG(trapframe, t9);
	DB_PRINT_REG(trapframe, k0);
	DB_PRINT_REG(trapframe, k1);
	DB_PRINT_REG(trapframe, gp);
	DB_PRINT_REG(trapframe, sp);
	DB_PRINT_REG(trapframe, s8);
	DB_PRINT_REG(trapframe, ra);
	DB_PRINT_REG(trapframe, sr);
	DB_PRINT_REG(trapframe, mullo);
	DB_PRINT_REG(trapframe, mulhi);
	DB_PRINT_REG(trapframe, badvaddr);
	DB_PRINT_REG(trapframe, cause);
	DB_PRINT_REG(trapframe, pc);
}

DB_SHOW_COMMAND(pcb, ddb_dump_pcb)
{
	struct thread *td;
	struct pcb *pcb;
	struct trapframe *trapframe;

	/* Determine which thread to examine. */
	if (have_addr)
		td = db_lookup_thread(addr, true);
	else
		td = curthread;
	
	pcb = td->td_pcb;

	db_printf("Thread %d at %p\n", td->td_tid, td);

	db_printf("PCB at %p\n", pcb);

	trapframe = &pcb->pcb_regs;
	dump_trapframe(trapframe);

	db_printf("PCB Context:\n");
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S0);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S1);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S2);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S3);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S4);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S5);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S6);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S7);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_SP);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_S8);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_RA);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_SR);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_GP);
	DB_PRINT_REG_ARRAY(pcb, pcb_context, PCB_REG_PC);

	db_printf("PCB onfault = %p\n", pcb->pcb_onfault);
	db_printf("md_saved_intr = 0x%0lx\n", (long)td->td_md.md_saved_intr);
	db_printf("md_spinlock_count = %d\n", td->td_md.md_spinlock_count);

	if (td->td_frame != trapframe) {
		db_printf("td->td_frame %p is not the same as pcb_regs %p\n",
			  td->td_frame, trapframe);
	}
}

/*
 * Dump the trapframe beginning at address specified by first argument.
 */
DB_SHOW_COMMAND(trapframe, ddb_dump_trapframe)
{
	
	if (!have_addr)
		return;

	dump_trapframe((struct trapframe *)addr);
}

#endif	/* DDB */
