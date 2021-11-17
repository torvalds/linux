// SPDX-License-Identifier: GPL-2.0
/*
 * tools/testing/selftests/kvm/include/perf_test_util.h
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef SELFTEST_KVM_PERF_TEST_UTIL_H
#define SELFTEST_KVM_PERF_TEST_UTIL_H

#include "kvm_util.h"

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

#define DEFAULT_PER_VCPU_MEM_SIZE	(1 << 30) /* 1G */

#define PERF_TEST_MEM_SLOT_INDEX	1

struct perf_test_vcpu_args {
	uint64_t gva;
	uint64_t pages;

	/* Only used by the host userspace part of the vCPU thread */
	int vcpu_id;
};

struct perf_test_args {
	struct kvm_vm *vm;
	uint64_t host_page_size;
	uint64_t guest_page_size;
	int wr_fract;

	struct perf_test_vcpu_args vcpu_args[KVM_MAX_VCPUS];
};

extern struct perf_test_args perf_test_args;

/*
 * Guest physical memory offset of the testing memory slot.
 * This will be set to the topmost valid physical address minus
 * the test memory size.
 */
extern uint64_t guest_test_phys_mem;

struct kvm_vm *perf_test_create_vm(enum vm_guest_mode mode, int vcpus,
				   uint64_t vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src);
void perf_test_destroy_vm(struct kvm_vm *vm);
void perf_test_setup_vcpus(struct kvm_vm *vm, int vcpus,
			   uint64_t vcpu_memory_bytes,
			   bool partition_vcpu_memory_access);

#endif /* SELFTEST_KVM_PERF_TEST_UTIL_H */
