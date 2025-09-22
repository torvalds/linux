/*	$OpenBSD: trap.c,v 1.20 2025/03/01 07:42:09 miod Exp $	*/

/*
 * Copyright (c) 2020 Shivam Waghela <shivamwaghela@gmail.com>
 * Copyright (c) 2020 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2020 Mengshi Li <mengshi.li.mars@gmail.com>
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
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
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/signalvar.h>
#include <sys/siginfo.h>

#include <machine/riscvreg.h>
#include <machine/syscall.h>
#include <machine/db_machdep.h>

/* Called from exception.S */
void do_trap_supervisor(struct trapframe *);
void do_trap_user(struct trapframe *);

static void udata_abort(struct trapframe *);
static void kdata_abort(struct trapframe *);

static void
dump_regs(struct trapframe *frame)
{
	int n;
	int i;

	n = (sizeof(frame->tf_t) / sizeof(frame->tf_t[0]));
	for (i = 0; i < n; i++)
		printf("t[%d] == 0x%016lx\n", i, frame->tf_t[i]);

	n = (sizeof(frame->tf_s) / sizeof(frame->tf_s[0]));
	for (i = 0; i < n; i++)
		printf("s[%d] == 0x%016lx\n", i, frame->tf_s[i]);

	n = (sizeof(frame->tf_a) / sizeof(frame->tf_a[0]));
	for (i = 0; i < n; i++)
		printf("a[%d] == 0x%016lx\n", i, frame->tf_a[i]);

	printf("sepc == 0x%016lx\n", frame->tf_sepc);
	printf("sstatus == 0x%016lx\n", frame->tf_sstatus);
	printf("stval == 0x%016lx\n", frame->tf_stval);
	printf("scause == 0x%016lx\n", frame->tf_scause);
}

void
do_trap_supervisor(struct trapframe *frame)
{
	uint64_t exception;

	/* Ensure we came from supervisor mode, interrupts disabled */
	KASSERTMSG((csr_read(sstatus) & (SSTATUS_SPP | SSTATUS_SIE)) ==
	    SSTATUS_SPP, "Came from S mode with interrupts enabled");

	KASSERTMSG((csr_read(sstatus) & (SSTATUS_SUM)) == 0,
	    "Came from S mode with SUM enabled");

	if (frame->tf_scause & EXCP_INTR) {
		/* Interrupt */
		riscv_cpu_intr(frame);
		return;
	}

	intr_enable();
	uvmexp.traps++;

	exception = (frame->tf_scause & EXCP_MASK);
	switch (exception) {
	case EXCP_FAULT_LOAD:
	case EXCP_FAULT_STORE:
	case EXCP_FAULT_FETCH:
	case EXCP_STORE_PAGE_FAULT:
	case EXCP_LOAD_PAGE_FAULT:
		kdata_abort(frame);
		break;
	case EXCP_BREAKPOINT:
#ifdef DDB
		db_trapper(frame->tf_sepc,0/*XXX*/, frame, exception);
#else
		dump_regs(frame);
		panic("No debugger in kernel.");
#endif
		break;
	case EXCP_ILLEGAL_INSTRUCTION:
		dump_regs(frame);
		panic("Illegal instruction at 0x%016lx", frame->tf_sepc);
		break;
	default:
		dump_regs(frame);
		panic("Unknown kernel exception %llx trap value pc 0x%lx stval %lx",
		    exception, frame->tf_sepc, frame->tf_stval);
	}
}

void
do_trap_user(struct trapframe *frame)
{
	uint64_t exception;
	union sigval sv;
	struct proc *p = curcpu()->ci_curproc;

	p->p_addr->u_pcb.pcb_tf = frame;

	/* Ensure we came from usermode, interrupts disabled */
	KASSERTMSG((csr_read(sstatus) & (SSTATUS_SPP | SSTATUS_SIE)) == 0,
	    "Came from U mode with interrupts enabled");

	KASSERTMSG((csr_read(sstatus) & (SSTATUS_SUM)) == 0,
	    "Came from U mode with SUM enabled");

	if (frame->tf_scause & EXCP_INTR) {
		/* Interrupt */
		riscv_cpu_intr(frame);
		return;
	}

	intr_enable();
	uvmexp.traps++;
	refreshcreds(p);

	exception = (frame->tf_scause & EXCP_MASK);
	switch (exception) {
	case EXCP_FAULT_LOAD:
	case EXCP_FAULT_STORE:
	case EXCP_FAULT_FETCH:
	case EXCP_STORE_PAGE_FAULT:
	case EXCP_LOAD_PAGE_FAULT:
	case EXCP_INST_PAGE_FAULT:
		udata_abort(frame);
		break;
	case EXCP_USER_ECALL:
		frame->tf_sepc += 4;	/* Next instruction */
		svc_handler(frame);
		break;
	case EXCP_ILLEGAL_INSTRUCTION:
		if ((frame->tf_sstatus & SSTATUS_FS_MASK) == SSTATUS_FS_OFF) {
			fpu_load(p);
			break;
		}
		sv.sival_ptr = (void *)frame->tf_stval;
		trapsignal(p, SIGILL, 0, ILL_ILLTRP, sv);
		break;
	case EXCP_BREAKPOINT:
		sv.sival_ptr = (void *)frame->tf_stval;
		trapsignal(p, SIGTRAP, 0, TRAP_BRKPT, sv);
		break;
	default:
		dump_regs(frame);
		panic("Unknown userland exception %llx, trap value %lx",
		    exception, frame->tf_stval);
	}

	userret(p);
}

static inline vm_prot_t
accesstype(struct trapframe *frame)
{
	vm_prot_t access_type;

	if ((frame->tf_scause == EXCP_FAULT_STORE) ||
	    (frame->tf_scause == EXCP_STORE_PAGE_FAULT)) {
		access_type = PROT_WRITE;
	} else if (frame->tf_scause == EXCP_INST_PAGE_FAULT) {
		access_type = PROT_EXEC;
	} else {
		access_type = PROT_READ;
	}
	return access_type;
}

static void
udata_abort(struct trapframe *frame)
{
	struct vm_map *map;
	uint64_t stval = frame->tf_stval;
	union sigval sv;
	vm_prot_t access_type = accesstype(frame);
	vaddr_t va;
	struct proc *p;
	int error, sig, code;

	p = curcpu()->ci_curproc;

	va = trunc_page(stval);

	map = &p->p_vmspace->vm_map;

	if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
	    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
	    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
		return;

	/* Handle referenced/modified emulation */
	if (pmap_fault_fixup(map->pmap, va, access_type))
		return;

	error = uvm_fault(map, va, 0, access_type);

	if (error == 0) {
		uvm_grow(p, va);
		return;
	}

	if (error == ENOMEM) {
		sig = SIGKILL;
		code = 0;
	} else if (error == EIO) {
		sig = SIGBUS;
		code = BUS_OBJERR;
	} else if (error == EACCES) {
		sig = SIGSEGV;
		code = SEGV_ACCERR;
	} else {
		sig = SIGSEGV;
		code = SEGV_MAPERR;
	}
	sv.sival_ptr = (void *)stval;
	trapsignal(p, sig, 0, code, sv);
}

static void
kdata_abort(struct trapframe *frame)
{
	struct vm_map *map;
	uint64_t stval = frame->tf_stval;
	struct pcb *pcb;
	vm_prot_t access_type = accesstype(frame);
	vaddr_t va;
	struct proc *p;
	int error = 0;

	pcb = curcpu()->ci_curpcb;
	p = curcpu()->ci_curproc;

	va = trunc_page(stval);

	if (stval >= VM_MAXUSER_ADDRESS)
		map = kernel_map;
	else {
		map = &p->p_vmspace->vm_map;
	}

	/* Handle referenced/modified emulation */
	if (!pmap_fault_fixup(map->pmap, va, access_type)) {
		error = uvm_fault(map, va, 0, access_type);

		if (error == 0 && map != kernel_map)
			uvm_grow(p, va);
	}

	if (error != 0) {
		if (curcpu()->ci_idepth == 0 &&
		    pcb->pcb_onfault != 0) {
			frame->tf_a[0] = error;
			frame->tf_sepc = (register_t)pcb->pcb_onfault;
			return;
		}
		dump_regs(frame);
		panic("Fatal page fault at %#lx: %#08lx", frame->tf_sepc,
		    (vaddr_t)stval);
	}
}
