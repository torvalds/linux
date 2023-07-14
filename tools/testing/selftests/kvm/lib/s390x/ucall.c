// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2019 Red Hat, Inc.
 */
#include "kvm_util.h"

void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
}

void ucall_arch_do_ucall(vm_vaddr_t uc)
{
	/* Exit via DIAGNOSE 0x501 (normally used for breakpoints) */
	asm volatile ("diag 0,%0,0x501" : : "a"(uc) : "memory");
}

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == KVM_EXIT_S390_SIEIC &&
	    run->s390_sieic.icptcode == 4 &&
	    (run->s390_sieic.ipa >> 8) == 0x83 &&    /* 0x83 means DIAGNOSE */
	    (run->s390_sieic.ipb >> 16) == 0x501) {
		int reg = run->s390_sieic.ipa & 0xf;

		return (void *)run->s.regs.gprs[reg];
	}
	return NULL;
}
