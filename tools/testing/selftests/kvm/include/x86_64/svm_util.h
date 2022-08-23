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

#define SVM_EXIT_EXCP_BASE	0x040
#define SVM_EXIT_HLT		0x078
#define SVM_EXIT_MSR		0x07c
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

	/* MSR-Bitmap */
	void *msr; /* gva */
	void *msr_hva;
	uint64_t msr_gpa;
};

#define stgi()			\
	__asm__ __volatile__(	\
		"stgi\n"	\
		)

#define clgi()			\
	__asm__ __volatile__(	\
		"clgi\n"	\
		)

struct svm_test_data *vcpu_alloc_svm(struct kvm_vm *vm, vm_vaddr_t *p_svm_gva);
void generic_svm_setup(struct svm_test_data *svm, void *guest_rip, void *guest_rsp);
void run_guest(struct vmcb *vmcb, uint64_t vmcb_gpa);

int open_sev_dev_path_or_exit(void);

#endif /* SELFTEST_KVM_SVM_UTILS_H */
