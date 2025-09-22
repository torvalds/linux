/*	$OpenBSD: fault.c,v 1.48 2024/04/29 12:33:17 jsg Exp $	*/
/*	$NetBSD: fault.c,v 1.46 2004/01/21 15:39:21 skrll Exp $	*/

/*
 * Copyright 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Steve C. Woodford for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1994-1997 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
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
 * fault.c
 *
 * Fault handlers
 *
 * Created      : 28/11/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>

#include <machine/frame.h>
#include <machine/cpu.h>
#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <arm/db_machdep.h>
#include <arm/machdep.h>
#include <arm/vfp.h>

struct sigdata {
	int signo;
	int code;
	vaddr_t addr;
	int trap;
};

struct data_abort {
	int (*func)(trapframe_t *, u_int, u_int, struct proc *,
	    struct sigdata *);
	const char *desc;
};

static int dab_fatal(trapframe_t *, u_int, u_int, struct proc *,
    struct sigdata *sd);
static int dab_align(trapframe_t *, u_int, u_int, struct proc *,
    struct sigdata *sd);
static int dab_buserr(trapframe_t *, u_int, u_int, struct proc *,
    struct sigdata *sd);
extern int dab_access(trapframe_t *, u_int, u_int, struct proc *,
    struct sigdata *sd);

static const struct data_abort data_aborts[] = {
	{dab_fatal,	"V7 fault 00000"},
	{dab_align,	"Alignment fault"},
	{dab_fatal,	"Debug event"},
	{dab_fatal,	"Access flag fault (L1)"},
	{dab_buserr,	"Fault on instruction cache maintenance"},
	{NULL,		"Translation fault (L1)"},
	{dab_access,	"Access flag fault (L2)"},
	{NULL,		"Translation fault (L2)"},
	{dab_buserr,	"Synchronous external abort"},
	{NULL,		"Domain fault (L1)"},
	{dab_fatal,	"V7 fault 01010"},
	{NULL,		"Domain fault (L2)"},
	{dab_buserr,	"Synchronous external abort on translation table walk (L1)"},
	{NULL,		"Permission fault (L1)"},
	{dab_buserr,	"Synchronous external abort on translation table walk (L2)"},
	{NULL,		"Permission fault (L2)"},
	{dab_fatal,	"TLB conflict abort"},
	{dab_fatal,	"V7 fault 10001"},
	{dab_fatal,	"V7 fault 10010"},
	{dab_fatal,	"V7 fault 10011"},
	{dab_fatal,	"Lockdown"},
	{dab_fatal,	"V7 fault 10101"},
	{dab_fatal,	"Asynchronous external abort"},
	{dab_fatal,	"V7 fault 10111"},
	{dab_fatal,	"Asynchronous parity error on memory access"},
	{dab_fatal,	"Synchronous parity error on memory access"},
	{dab_fatal,	"Coprocessor Abort"},
	{dab_fatal,	"V7 fault 11011"},
	{dab_buserr,	"Synchronous parity error on translation table walk (L1)"},
	{dab_fatal,	"V7 fault 11101"},
	{dab_buserr,	"Synchronous parity error on translation table walk (L2)"},
	{NULL,		"V7 fault 11111"},
};

/* Determine if 'ftyp' is a permission fault */
#define	IS_PERMISSION_FAULT(ftyp)				\
	(((1 << (ftyp)) &					\
	  ((1 << FAULT_PERM_P) | (1 << FAULT_PERM_S))) != 0)

void
data_abort_handler(trapframe_t *tf)
{
	struct vm_map *map;
	struct pcb *pcb;
	struct proc *p;
	u_int user, far, fsr, ftyp;
	vm_prot_t ftype;
	void *onfault;
	vaddr_t va;
	int error;
	union sigval sv;
	struct sigdata sd;

	/* Grab FAR/FSR before enabling interrupts */
	far = cpu_dfar();
	fsr = cpu_dfsr();
	ftyp = FAULT_TYPE_V7(fsr);

	/* Update vmmeter statistics */
	uvmexp.traps++;

	/* Before enabling interrupts, save FPU state */
	vfp_save();

	/* Re-enable interrupts if they were enabled previously */
	if (__predict_true((tf->tf_spsr & PSR_I) == 0))
		enable_interrupts(PSR_I);

	/* Get the current proc structure or proc0 if there is none */
	p = (curproc != NULL) ? curproc : &proc0;

	/* Data abort came from user mode? */
	user = TRAP_USERMODE(tf);

	/* Grab the current pcb */
	pcb = &p->p_addr->u_pcb;

	if (user) {
		pcb->pcb_tf = tf;
		refreshcreds(p);
	}

	/* Invoke the appropriate handler, if necessary */
	if (__predict_false(data_aborts[ftyp].func != NULL)) {
		if ((data_aborts[ftyp].func)(tf, fsr, far, p, &sd)) {
			goto do_trapsignal;
		}
		goto out;
	}

	va = trunc_page((vaddr_t)far);

	/*
	 * Flush BP cache on processors that are vulnerable to branch
	 * target injection attacks if access is outside user space.
	 */
	if (va < VM_MIN_ADDRESS || va >= VM_MAX_ADDRESS)
		curcpu()->ci_flush_bp();

	if (user) {
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;
	}

	/*
	 * At this point, we're dealing with one of the following data aborts:
	 *
	 *  FAULT_TRANS_S  - Translation -- Section
	 *  FAULT_TRANS_P  - Translation -- Page
	 *  FAULT_DOMAIN_S - Domain -- Section
	 *  FAULT_DOMAIN_P - Domain -- Page
	 *  FAULT_PERM_S   - Permission -- Section
	 *  FAULT_PERM_P   - Permission -- Page
	 *
	 * These are the main virtual memory-related faults signalled by
	 * the MMU.
	 */

	/*
	 * Make sure the Program Counter is sane. We could fall foul of
	 * someone executing Thumb code, in which case the PC might not
	 * be word-aligned. This would cause a kernel alignment fault
	 * further down if we have to decode the current instruction.
	 * XXX: It would be nice to be able to support Thumb at some point.
	 */
	if (__predict_false((tf->tf_pc & 3) != 0)) {
		if (user) {
			/*
			 * Give the user an illegal instruction signal.
			 */
			/* Deliver a SIGILL to the process */
			sd.signo = SIGILL;
			sd.code = ILL_ILLOPC;
			sd.addr = far;
			sd.trap = fsr;
			goto do_trapsignal;
		}

		/*
		 * The kernel never executes Thumb code.
		 */
		printf("\ndata_abort_fault: Misaligned Kernel-mode "
		    "Program Counter\n");
		dab_fatal(tf, fsr, far, p, NULL);
	}

	/*
	 * It is only a kernel address space fault iff:
	 *	1. user == 0  and
	 *	2. pcb_onfault not set or
	 *	3. pcb_onfault set and not LDRT/LDRBT/STRT/STRBT instruction.
	 */
	if (user == 0 && (va >= VM_MIN_KERNEL_ADDRESS ||
	    (va < VM_MIN_ADDRESS && vector_page == ARM_VECTORS_LOW)) &&
	    __predict_true((pcb->pcb_onfault == NULL ||
	     ((*(u_int *)tf->tf_pc) & 0x05200000) != 0x04200000))) {
		map = kernel_map;

		/* Was the fault due to the FPE/IPKDB ? */
		if (__predict_false((tf->tf_spsr & PSR_MODE)==PSR_UND32_MODE)) {
			sd.signo = SIGSEGV;
			sd.code = SEGV_ACCERR;
			sd.addr = far;
			sd.trap = fsr;

			/*
			 * Force exit via userret()
			 * This is necessary as the FPE is an extension to
			 * userland that actually runs in a privileged mode
			 * but uses USR mode permissions for its accesses.
			 */
			user = 1;
			goto do_trapsignal;
		}
	} else {
		map = &p->p_vmspace->vm_map;
#if 0
		if (l->l_flag & L_SA) {
			KDASSERT(l->l_proc->p_sa != NULL);
			l->l_proc->p_sa->sa_vp_faultaddr = (vaddr_t)far;
			l->l_flag |= L_SA_PAGEFAULT;
		}
#endif
	}

	ftype = fsr & FAULT_WNR ? PROT_WRITE : PROT_READ;

	if (__predict_false(curcpu()->ci_idepth > 0)) {
		if (pcb->pcb_onfault) {
			tf->tf_r0 = EINVAL;
			tf->tf_pc = (register_t) pcb->pcb_onfault;
			return;
		}
		printf("\nNon-emulated page fault with intr_depth > 0\n");
		dab_fatal(tf, fsr, far, p, NULL);
	}

	onfault = pcb->pcb_onfault;
	pcb->pcb_onfault = NULL;
	KERNEL_LOCK();
	error = uvm_fault(map, va, 0, ftype);
	KERNEL_UNLOCK();
	pcb->pcb_onfault = onfault;

#if 0
	if (map != kernel_map)
		p->p_flag &= ~L_SA_PAGEFAULT;
#endif

	if (error == 0) {
		if (map != kernel_map)
			uvm_grow(p, va);
		goto out;
	}

	if (user == 0) {
		if (pcb->pcb_onfault) {
			tf->tf_r0 = EFAULT;
			tf->tf_pc = (register_t) pcb->pcb_onfault;
			return;
		}

		printf("\nuvm_fault(%p, %lx, %x, 0) -> %x\n", map, va, ftype,
		    error);
		dab_fatal(tf, fsr, far, p, NULL);
	}

	sd.signo = SIGSEGV;
	sd.code = SEGV_MAPERR;
	if (error == ENOMEM) {
		printf("UVM: pid %d (%s), uid %d killed: "
		    "out of swap\n", p->p_p->ps_pid, p->p_p->ps_comm,
		    p->p_ucred ? (int)p->p_ucred->cr_uid : -1);
		sd.signo = SIGKILL;
		sd.code = 0;
	} else if (error == EACCES) 
		sd.code = SEGV_ACCERR;
	else if (error == EIO) {
		sd.signo = SIGBUS;
		sd.code = BUS_OBJERR;
	}
	sd.addr = far;
	sd.trap = fsr;
do_trapsignal:
	sv.sival_int = sd.addr;
	trapsignal(p, sd.signo, sd.trap, sd.code, sv);
out:
	/* If returning to user mode, make sure to invoke userret() */
	if (user)
		userret(p);
}

/*
 * dab_fatal() handles the following data aborts:
 *
 *  FAULT_WRTBUF_0 - Vector Exception
 *  FAULT_WRTBUF_1 - Terminal Exception
 *
 * We should never see these on a properly functioning system.
 *
 * This function is also called by the other handlers if they
 * detect a fatal problem.
 *
 * Note: If 'p' is NULL, we assume we're dealing with a prefetch abort.
 */
static int
dab_fatal(trapframe_t *tf, u_int fsr, u_int far, struct proc *p,
    struct sigdata *sd)
{
	const char *mode;
	uint ftyp;

	mode = TRAP_USERMODE(tf) ? "user" : "kernel";

	if (p != NULL) {
		ftyp = FAULT_TYPE_V7(fsr);
		printf("Fatal %s mode data abort: '%s'\n", mode,
		    data_aborts[ftyp].desc);
		printf("trapframe: %p\nDFSR=%08x, DFAR=%08x", tf, fsr, far);
		printf(", spsr=%08lx\n", tf->tf_spsr);
	} else {
		printf("Fatal %s mode prefetch abort at 0x%08lx\n",
		    mode, tf->tf_pc);
		printf("trapframe: %p\nIFSR=%08x, IFAR=%08x, spsr=%08lx\n",
		    tf, fsr, far, tf->tf_spsr);
	}

	printf("r0 =%08lx, r1 =%08lx, r2 =%08lx, r3 =%08lx\n",
	    tf->tf_r0, tf->tf_r1, tf->tf_r2, tf->tf_r3);
	printf("r4 =%08lx, r5 =%08lx, r6 =%08lx, r7 =%08lx\n",
	    tf->tf_r4, tf->tf_r5, tf->tf_r6, tf->tf_r7);
	printf("r8 =%08lx, r9 =%08lx, r10=%08lx, r11=%08lx\n",
	    tf->tf_r8, tf->tf_r9, tf->tf_r10, tf->tf_r11);
	printf("r12=%08lx, ", tf->tf_r12);

	if (TRAP_USERMODE(tf))
		printf("usp=%08lx, ulr=%08lx",
		    tf->tf_usr_sp, tf->tf_usr_lr);
	else
		printf("ssp=%08lx, slr=%08lx",
		    tf->tf_svc_sp, tf->tf_svc_lr);
	printf(", pc =%08lx\n\n", tf->tf_pc);

#ifdef DDB
	db_ktrap(T_FAULT, tf);
#endif
	panic("Fatal abort");
	/*NOTREACHED*/
}

/*
 * dab_align() handles the following data aborts:
 *
 *  FAULT_ALIGN_0 - Alignment fault
 *  FAULT_ALIGN_0 - Alignment fault
 *
 * These faults are fatal if they happen in kernel mode. Otherwise, we
 * deliver a bus error to the process.
 */
static int
dab_align(trapframe_t *tf, u_int fsr, u_int far, struct proc *p,
    struct sigdata *sd)
{
	/* Alignment faults are always fatal if they occur in kernel mode */
	if (!TRAP_USERMODE(tf))
		dab_fatal(tf, fsr, far, p, NULL);

	/* pcb_onfault *must* be NULL at this point */
	KDASSERT(p->p_addr->u_pcb.pcb_onfault == NULL);

	/* Deliver a bus error signal to the process */
	sd->signo = SIGBUS;
	sd->code = BUS_ADRALN;
	sd->addr = far;
	sd->trap = fsr;

	return (1);
}

/*
 * dab_buserr() handles the following data aborts:
 *
 *  FAULT_BUSERR_0 - External Abort on Linefetch -- Section
 *  FAULT_BUSERR_1 - External Abort on Linefetch -- Page
 *  FAULT_BUSERR_2 - External Abort on Non-linefetch -- Section
 *  FAULT_BUSERR_3 - External Abort on Non-linefetch -- Page
 *  FAULT_BUSTRNL1 - External abort on Translation -- Level 1
 *  FAULT_BUSTRNL2 - External abort on Translation -- Level 2
 *
 * If pcb_onfault is set, flag the fault and return to the handler.
 * If the fault occurred in user mode, give the process a SIGBUS.
 *
 * Note: On XScale, FAULT_BUSERR_0, FAULT_BUSERR_1, and FAULT_BUSERR_2
 * can be flagged as imprecise in the FSR. This causes a real headache
 * since some of the machine state is lost. In this case, tf->tf_pc
 * may not actually point to the offending instruction. In fact, if
 * we've taken a double abort fault, it generally points somewhere near
 * the top of "data_abort_entry" in exception.S.
 *
 * In all other cases, these data aborts are considered fatal.
 */
static int
dab_buserr(trapframe_t *tf, u_int fsr, u_int far, struct proc *p,
    struct sigdata *sd)
{
	struct pcb *pcb = &p->p_addr->u_pcb;

	if (pcb->pcb_onfault) {
		KDASSERT(TRAP_USERMODE(tf) == 0);
		tf->tf_r0 = EFAULT;
		tf->tf_pc = (register_t) pcb->pcb_onfault;
		return (0);
	}

	/*
	 * At this point, if the fault happened in kernel mode or user mode,
	 * we're toast
	 */
	dab_fatal(tf, fsr, far, p, NULL);

	return (1);
}

/*
 * void prefetch_abort_handler(trapframe_t *tf)
 *
 * Abort handler called when instruction execution occurs at
 * a non existent or restricted (access permissions) memory page.
 * If the address is invalid and we were in SVC mode then panic as
 * the kernel should never prefetch abort.
 * If the address is invalid and the page is mapped then the user process
 * does no have read or execute permission so send it a signal.
 * Otherwise fault the page in and try again.
 */
void
prefetch_abort_handler(trapframe_t *tf)
{
	struct proc *p = curproc;
	struct vm_map *map;
	vaddr_t va;
	int error;
	union sigval sv;
	uint fsr, far;

	/* Update vmmeter statistics */
	uvmexp.traps++;

	/* Grab FAR/FSR before enabling interrupts */
	far = cpu_ifar();
	fsr = cpu_ifsr();

	/* Prefetch aborts cannot happen in kernel mode */
	if (__predict_false(!TRAP_USERMODE(tf)))
		dab_fatal(tf, fsr, far, NULL, NULL);

	/* Before enabling interrupts, save FPU state */
	vfp_save();

	/*
	 * Enable IRQ's (disabled by the abort) This always comes
	 * from user mode so we know interrupts were not disabled.
	 * But we check anyway.
	 */
	if (__predict_true((tf->tf_spsr & PSR_I) == 0))
		enable_interrupts(PSR_I);

	p->p_addr->u_pcb.pcb_tf = tf;

	/* Invoke access fault handler if appropriate */
	if (FAULT_TYPE_V7(fsr) == FAULT_ACCESS_2) {
		dab_access(tf, fsr, far, p, NULL);
		goto out;
	}

	/* Ok validate the address, can only execute in USER space */
	if (__predict_false(far >= VM_MAXUSER_ADDRESS ||
	    (far < VM_MIN_ADDRESS && vector_page == ARM_VECTORS_LOW))) {
		sv.sival_ptr = (u_int32_t *)far;
		trapsignal(p, SIGSEGV, 0, SEGV_ACCERR, sv);
		goto out;
	}

	map = &p->p_vmspace->vm_map;
	va = trunc_page(far);

#ifdef DIAGNOSTIC
	if (__predict_false(curcpu()->ci_idepth > 0)) {
		printf("\nNon-emulated prefetch abort with intr_depth > 0\n");
		dab_fatal(tf, fsr, far, NULL, NULL);
	}
#endif

	KERNEL_LOCK();
	error = uvm_fault(map, va, 0, PROT_EXEC);
	KERNEL_UNLOCK();

	if (error == 0) {
		uvm_grow(p, va);
		goto out;
	}

	sv.sival_ptr = (u_int32_t *)far;
	if (error == ENOMEM) {
		printf("UVM: pid %d (%s), uid %d killed: "
		    "out of swap\n", p->p_p->ps_pid, p->p_p->ps_comm,
		    p->p_ucred ? (int)p->p_ucred->cr_uid : -1);
		trapsignal(p, SIGKILL, 0, SEGV_MAPERR, sv);
	} else {
		trapsignal(p, SIGSEGV, 0, SEGV_MAPERR, sv);
	}

out:
	userret(p);
}

/*
 * Tentatively read an 8, 16, or 32-bit value from 'addr'.
 * If the read succeeds, the value is written to 'rptr' and zero is returned.
 * Else, return EFAULT.
 */
int
badaddr_read(void *addr, size_t size, void *rptr)
{
	extern int badaddr_read_1(const uint8_t *, uint8_t *);
	extern int badaddr_read_2(const uint16_t *, uint16_t *);
	extern int badaddr_read_4(const uint32_t *, uint32_t *);
	union {
		uint8_t v1;
		uint16_t v2;
		uint32_t v4;
	} u;
	int rv;

	cpu_drain_writebuf();

	/* Read from the test address. */
	switch (size) {
	case sizeof(uint8_t):
		rv = badaddr_read_1(addr, &u.v1);
		if (rv == 0 && rptr)
			*(uint8_t *) rptr = u.v1;
		break;

	case sizeof(uint16_t):
		rv = badaddr_read_2(addr, &u.v2);
		if (rv == 0 && rptr)
			*(uint16_t *) rptr = u.v2;
		break;

	case sizeof(uint32_t):
		rv = badaddr_read_4(addr, &u.v4);
		if (rv == 0 && rptr)
			*(uint32_t *) rptr = u.v4;
		break;

	default:
		panic("badaddr: invalid size (%lu)", (u_long) size);
	}

	/* Return EFAULT if the address was invalid, else zero */
	return (rv);
}
