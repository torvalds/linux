/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UCALL_H
#define SELFTEST_KVM_UCALL_H

#include "kvm_util.h"

#define UCALL_EXIT_REASON       KVM_EXIT_IO

static inline void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
}

#endif
