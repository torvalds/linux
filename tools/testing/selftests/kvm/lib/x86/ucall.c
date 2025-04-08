// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include "kvm_util.h"

#define UCALL_PIO_PORT ((uint16_t)0x1000)

void ucall_arch_do_ucall(vm_vaddr_t uc)
{
	/*
	 * FIXME: Revert this hack (the entire commit that added it) once nVMX
	 * preserves L2 GPRs across a nested VM-Exit.  If a ucall from L2, e.g.
	 * to do a GUEST_SYNC(), lands the vCPU in L1, any and all GPRs can be
	 * clobbered by L1.  Save and restore non-volatile GPRs (clobbering RBP
	 * in particular is problematic) along with RDX and RDI (which are
	 * inputs), and clobber volatile GPRs. *sigh*
	 */
#define HORRIFIC_L2_UCALL_CLOBBER_HACK	\
	"rcx", "rsi", "r8", "r9", "r10", "r11"

	asm volatile("push %%rbp\n\t"
		     "push %%r15\n\t"
		     "push %%r14\n\t"
		     "push %%r13\n\t"
		     "push %%r12\n\t"
		     "push %%rbx\n\t"
		     "push %%rdx\n\t"
		     "push %%rdi\n\t"
		     "in %[port], %%al\n\t"
		     "pop %%rdi\n\t"
		     "pop %%rdx\n\t"
		     "pop %%rbx\n\t"
		     "pop %%r12\n\t"
		     "pop %%r13\n\t"
		     "pop %%r14\n\t"
		     "pop %%r15\n\t"
		     "pop %%rbp\n\t"
		: : [port] "d" (UCALL_PIO_PORT), "D" (uc) : "rax", "memory",
		     HORRIFIC_L2_UCALL_CLOBBER_HACK);
}

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == KVM_EXIT_IO && run->io.port == UCALL_PIO_PORT) {
		struct kvm_regs regs;

		vcpu_regs_get(vcpu, &regs);
		return (void *)regs.rdi;
	}
	return NULL;
}
