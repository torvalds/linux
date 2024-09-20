/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UCALL_H
#define SELFTEST_KVM_UCALL_H

#include "processor.h"
#include "sbi.h"

#define UCALL_EXIT_REASON       KVM_EXIT_RISCV_SBI

static inline void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
}

static inline void ucall_arch_do_ucall(vm_vaddr_t uc)
{
	sbi_ecall(KVM_RISCV_SELFTESTS_SBI_EXT,
		  KVM_RISCV_SELFTESTS_SBI_UCALL,
		  uc, 0, 0, 0, 0, 0);
}

#endif
