/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UCALL_H
#define SELFTEST_KVM_UCALL_H

#include "hcall.h"

#define UCALL_EXIT_REASON	KVM_EXIT_PAPR_HCALL

#define UCALL_R4_UCALL	0x5715 /* regular ucall, r5 contains ucall pointer */
#define UCALL_R4_SIMPLE	0x0000 /* simple exit usable by asm with no ucall data */

static inline void ucall_arch_init(struct kvm_vm *vm, gpa_t mmio_gpa)
{
}

static inline void ucall_arch_do_ucall(gva_t uc)
{
	hcall2(H_UCALL, UCALL_R4_UCALL, (uintptr_t)(uc));
}

#endif
