/*	$OpenBSD: trap.c,v 1.55 2025/07/16 07:15:42 jsg Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/syscall_mi.h>

#include <uvm/uvm_extern.h>

#include <machine/fpu.h>
#include <machine/pte.h>
#include <machine/trap.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_output.h>
#endif

void	decr_intr(struct trapframe *); /* clock.c */
void	exi_intr(struct trapframe *);  /* intr.c */
void	hvi_intr(struct trapframe *);  /* intr.c */
void	syscall(struct trapframe *);   /* syscall.c */

#ifdef TRAP_DEBUG
void	dumpframe(struct trapframe *);
#endif

void
trap(struct trapframe *frame)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = curproc;
	int type = frame->exc;
	union sigval sv;
	struct vm_map *map;
	struct vm_map_entry *entry;
	pmap_t pm;
	vaddr_t va;
	int access_type;
	int error, sig, code;

	/* Disable access to floating-point and vector registers. */
	mtmsr(mfmsr() & ~(PSL_FPU|PSL_VEC|PSL_VSX));

	switch (type) {
	case EXC_DECR:
		uvmexp.intrs++;
		ci->ci_idepth++;
		decr_intr(frame);
		ci->ci_idepth--;
		return;
	case EXC_EXI:
		uvmexp.intrs++;
		ci->ci_idepth++;
		exi_intr(frame);
		ci->ci_idepth--;
		return;
	case EXC_HVI:
		uvmexp.intrs++;
		ci->ci_idepth++;
		hvi_intr(frame);
		ci->ci_idepth--;
		return;
	case EXC_SC:
		uvmexp.syscalls++;
		break;
	default:
		uvmexp.traps++;
		break;
	}

	if (frame->srr1 & PSL_EE)
		intr_enable();

	if (frame->srr1 & PSL_PR) {
		type |= EXC_USER;
		p->p_md.md_regs = frame;
		refreshcreds(p);
	}

	switch (type) {
#ifdef DDB
	case EXC_PGM:
		/* At a trap instruction, enter the debugger. */
		if (frame->srr1 & EXC_PGM_TRAP) {
			/* Return from db_enter(). */
			if (frame->srr0 == (register_t)db_enter)
				frame->srr0 = frame->lr;
			db_ktrap(T_BREAKPOINT, frame);
			return;
		}
		goto fatal;
	case EXC_TRC:
		db_ktrap(T_BREAKPOINT, frame); /* single-stepping */
		return;
#endif

	case EXC_DSI:
		map = kernel_map;
		va = frame->dar;
		if (curpcb->pcb_onfault &&
		    (va >> ADDR_ESID_SHIFT) == USER_ESID) {
			map = &p->p_vmspace->vm_map;
			va = curpcb->pcb_userva | (va & SEGMENT_MASK);
		}
		if (frame->dsisr & DSISR_STORE)
			access_type = PROT_WRITE;
		else
			access_type = PROT_READ;
		error = uvm_fault(map, trunc_page(va), 0, access_type);
		if (error == 0)
			return;

		if (curpcb->pcb_onfault) {
			frame->srr0 = curpcb->pcb_onfault;
			return;
		}

		printf("dar 0x%lx dsisr 0x%lx\n", frame->dar, frame->dsisr);
		goto fatal;

	case EXC_DSE:
		/*
		 * If we sleep while handling a fault, we may lose our
		 * SLB entry.  Enter it again.
		 */
		va = frame->dar;
		if (curpcb->pcb_onfault &&
		    (va >> ADDR_ESID_SHIFT) == USER_ESID) {
			map = &p->p_vmspace->vm_map;
			va = curpcb->pcb_userva | (va & SEGMENT_MASK);
			if (pmap_set_user_slb(map->pmap, va, NULL, NULL) == 0)
				return;
		}

		if (curpcb->pcb_onfault) {
			frame->srr0 = curpcb->pcb_onfault;
			return;
		}

		printf("dar 0x%lx dsisr 0x%lx\n", frame->dar, frame->dsisr);
		goto fatal;

	case EXC_ALI:
	{
		/*
		 * In general POWER allows unaligned loads and stores
		 * and executes those instructions in an efficient
		 * way.  As a result compilers may combine word-sized
		 * stores into a single doubleword store instruction
		 * even if the address is not guaranteed to be
		 * doubleword aligned.  Such unaligned stores are not
		 * supported in storage that is Caching Inhibited.
		 * Access to such storage should be done through
		 * volatile pointers which inhibit the aforementioned
		 * optimizations.  Unfortunately code in the amdgpu(4)
		 * and radeondrm(4) drivers happens to run into such
		 * unaligned access because pointers aren't always
		 * marked as volatile.  For that reason we emulate
		 * certain store instructions here.
		 */
		uint32_t insn = *(uint32_t *)frame->srr0;

		/* std and stdu */
		if ((insn & 0xfc000002) == 0xf8000000) {
			uint32_t rs = (insn >> 21) & 0x1f;
			uint32_t ra = (insn >> 16) & 0x1f;
			uint64_t ds = insn & 0xfffc;
			uint64_t ea;

			if ((insn & 0x00000001) == 0 && ra == 0)
				panic("invalid stdu instruction form");
			
			if (ds & 0x8000)
				ds |= ~0x7fff; /* sign extend */
			if (ra == 0)
				ea = ds;
			else
				ea = frame->fixreg[ra] + ds;

			/*
			 * If the effective address isn't 32-bit
			 * aligned, or if data access cannot be
			 * performed because of the access violates
			 * storage protection, this will trigger
			 * another trap, which we can handle.
			 */
			*(volatile uint32_t *)ea = frame->fixreg[rs] >> 32;
			*(volatile uint32_t *)(ea + 4) = frame->fixreg[rs];
			if (insn & 0x00000001)
				frame->fixreg[ra] = ea;
			frame->srr0 += 4;
			return;
		}
		printf("dar 0x%lx dsisr 0x%lx\n", frame->dar, frame->dsisr);
		goto fatal;
	}

	case EXC_DSE|EXC_USER:
		pm = p->p_vmspace->vm_map.pmap;
		error = pmap_slbd_fault(pm, frame->dar);
		if (error == 0)
			break;

		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;

		/*
		 * Unfortunately, the hardware doesn't tell us whether
		 * this was a read or a write fault.  So we check
		 * whether there is a mapping at the fault address and
		 * insert a new SLB entry.  Executing the faulting
		 * instruction again should result in a Data Storage
		 * Interrupt that does indicate whether we're dealing
		 * with a read or a write fault.
		 */
		map = &p->p_vmspace->vm_map;
		vm_map_lock_read(map);
		if (uvm_map_lookup_entry(map, frame->dar, &entry))
			error = pmap_slbd_enter(pm, frame->dar);
		else
			error = EFAULT;
		vm_map_unlock_read(map);
		if (error) {
			sv.sival_ptr = (void *)frame->dar;
			trapsignal(p, SIGSEGV, 0, SEGV_MAPERR, sv);
		}
		break;

	case EXC_DSI|EXC_USER:
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;

		map = &p->p_vmspace->vm_map;
		va = frame->dar;
		if (frame->dsisr & DSISR_STORE)
			access_type = PROT_WRITE;
		else
			access_type = PROT_READ;
		error = uvm_fault(map, trunc_page(va), 0, access_type);
		if (error == 0)
			uvm_grow(p, va);

		if (error) {
#ifdef TRAP_DEBUG
			printf("type %x dar 0x%lx dsisr 0x%lx %s\r\n",
			    type, frame->dar, frame->dsisr, p->p_p->ps_comm);
			dumpframe(frame);
#endif

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
			sv.sival_ptr = (void *)va;
			trapsignal(p, sig, 0, code, sv);
		}
		break;

	case EXC_ISE|EXC_USER:
		pm = p->p_vmspace->vm_map.pmap;
		error = pmap_slbd_fault(pm, frame->srr0);
		if (error == 0)
			break;
		/* FALLTHROUGH */

	case EXC_ISI|EXC_USER:
		if (!uvm_map_inentry(p, &p->p_spinentry, PROC_STACK(p),
		    "[%s]%d/%d sp=%lx inside %lx-%lx: not MAP_STACK\n",
		    uvm_map_inentry_sp, p->p_vmspace->vm_map.sserial))
			goto out;

		map = &p->p_vmspace->vm_map;
		va = frame->srr0;
		access_type = PROT_EXEC;
		error = uvm_fault(map, trunc_page(va), 0, access_type);
		if (error == 0)
			uvm_grow(p, va);

		if (error) {
#ifdef TRAP_DEBUG
			printf("type %x srr0 0x%lx %s\r\n",
			    type, frame->srr0, p->p_p->ps_comm);
			dumpframe(frame);
#endif

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
			sv.sival_ptr = (void *)va;
			trapsignal(p, sig, 0, code, sv);
		}
		break;

	case EXC_SC|EXC_USER:
		syscall(frame);
		return;

	case EXC_AST|EXC_USER:
		p->p_md.md_astpending = 0;
		uvmexp.softs++;
		mi_ast(p, curcpu()->ci_want_resched);
		break;

	case EXC_ALI|EXC_USER:
		sv.sival_ptr = (void *)frame->dar;
		trapsignal(p, SIGBUS, 0, BUS_ADRALN, sv);
		break;

	case EXC_PGM|EXC_USER:
		sv.sival_ptr = (void *)frame->srr0;
		if (frame->srr1 & EXC_PGM_FPENABLED)
			trapsignal(p, SIGFPE, 0, fpu_sigcode(p), sv);
		else if (frame->srr1 & EXC_PGM_TRAP)
			trapsignal(p, SIGTRAP, 0, TRAP_BRKPT, sv);
		else
			trapsignal(p, SIGILL, 0, ILL_PRVOPC, sv);
		break;

	case EXC_FPU|EXC_USER:
		if ((frame->srr1 & (PSL_FP|PSL_VEC|PSL_VSX)) == 0)
			restore_vsx(p);
		curpcb->pcb_flags |= PCB_FPU;
		frame->srr1 |= PSL_FPU;
		break;

	case EXC_TRC|EXC_USER:
		sv.sival_ptr = (void *)frame->srr0;
		trapsignal(p, SIGTRAP, 0, TRAP_TRACE, sv);
		break;

	case EXC_HEA|EXC_USER:
		sv.sival_ptr = (void *)frame->srr0;
		trapsignal(p, SIGILL, 0, ILL_ILLOPC, sv);
		break;

	case EXC_VEC|EXC_USER:
		if ((frame->srr1 & (PSL_FP|PSL_VEC|PSL_VSX)) == 0)
			restore_vsx(p);
		curpcb->pcb_flags |= PCB_VEC;
		frame->srr1 |= PSL_VEC;
		break;

	case EXC_VSX|EXC_USER:
		if ((frame->srr1 & (PSL_FP|PSL_VEC|PSL_VSX)) == 0)
			restore_vsx(p);
		curpcb->pcb_flags |= PCB_VSX;
		frame->srr1 |= PSL_VSX;
		break;

	case EXC_FAC|EXC_USER:
		sv.sival_ptr = (void *)frame->srr0;
		trapsignal(p, SIGILL, 0, ILL_PRVOPC, sv);
		break;

	default:
	fatal:
#ifdef DDB
		db_printf("trap type %x srr1 %lx at %lx lr %lx\n",
		    type, frame->srr1, frame->srr0, frame->lr);
		db_ktrap(0, frame);
#endif
		panic("trap type %x srr1 %lx at %lx lr %lx",
		    type, frame->srr1, frame->srr0, frame->lr);
	}
out:
	userret(p);
}

#ifdef TRAP_DEBUG

#include <machine/opal.h>

void
dumpframe(struct trapframe *frame)
{
	int i;

	for (i = 0; i < 32; i++)
		opal_printf("r%d 0x%lx\r\n", i, frame->fixreg[i]);
	opal_printf("ctr 0x%lx\r\n", frame->ctr);
	opal_printf("xer 0x%lx\r\n", frame->xer);
	opal_printf("cr 0x%lx\r\n", frame->cr);
	opal_printf("lr 0x%lx\r\n", frame->lr);
	opal_printf("srr0 0x%lx\r\n", frame->srr0);
	opal_printf("srr1 0x%lx\r\n", frame->srr1);
}

#endif
