/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UCALL_H
#define SELFTEST_KVM_UCALL_H

#include "kvm_util.h"

#define UCALL_EXIT_REASON       KVM_EXIT_S390_SIEIC

static inline void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
}

static inline void ucall_arch_do_ucall(vm_vaddr_t uc)
{
	/* Exit via DIAGNOSE 0x501 (normally used for breakpoints) */
	asm volatile ("diag 0,%0,0x501" : : "a"(uc) : "memory");
}

#endif
