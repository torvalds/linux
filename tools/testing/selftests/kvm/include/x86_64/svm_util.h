/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/svm_utils.h
 * Header for nested SVM testing
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */

#ifndef SELFTEST_KVM_SVM_UTILS_H
#define SELFTEST_KVM_SVM_UTILS_H

#include <stdint.h>
#include "svm.h"
#include "processor.h"

#define CPUID_SVM_BIT		2
#define CPUID_SVM		BIT_ULL(CPUID_SVM_BIT)

#define SVM_EXIT_VMMCALL	0x081

struct svm_test_data {
	/* VMCB */
	struct vmcb *vmcb; /* gva */
	void *vmcb_hva;
	uint64_t vmcb_gpa;

	/* host state-save area */
	struct vmcb_save_area *save_area; /* gva */
	void *save_area_hva;
	uint64_t save_area_gpa;
};

struct svm_test_data *vcpu_alloc_svm(struct kvm_vm *vm, vm_vaddr_t *p_svm_gva);
void generic_svm_setup(struct svm_test_data *svm, void *guest_rip, void *guest_rsp);
void run_guest(struct vmcb *vmcb, uint64_t vmcb_gpa);
bool nested_svm_supported(void);
void nested_svm_check_supported(void);

static inline bool cpu_has_svm(void)
{
	u32 eax = 0x80000001, ecx;

	asm("cpuid" :
	    "=a" (eax), "=c" (ecx) : "0" (eax) : "ebx", "edx");

	return ecx & CPUID_SVM;
}

int open_sev_dev_path_or_exit(void);

#endif /* SELFTEST_KVM_SVM_UTILS_H */
