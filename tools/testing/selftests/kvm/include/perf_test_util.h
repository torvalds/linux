// SPDX-License-Identifier: GPL-2.0
/*
 * tools/testing/selftests/kvm/include/perf_test_util.h
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef SELFTEST_KVM_PERF_TEST_UTIL_H
#define SELFTEST_KVM_PERF_TEST_UTIL_H

#include <pthread.h>

#include "kvm_util.h"

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

#define DEFAULT_PER_VCPU_MEM_SIZE	(1 << 30) /* 1G */

#define PERF_TEST_MEM_SLOT_INDEX	1

struct perf_test_vcpu_args {
	uint64_t gpa;
	uint64_t gva;
	uint64_t pages;

	/* Only used by the host userspace part of the vCPU thread */
	struct kvm_vcpu *vcpu;
	int vcpu_idx;
};

struct perf_test_args {
	struct kvm_vm *vm;
	/* The starting address and size of the guest test region. */
	uint64_t gpa;
	uint64_t size;
	uint64_t guest_page_size;
	int wr_fract;

	/* Run vCPUs in L2 instead of L1, if the architecture supports it. */
	bool nested;

	struct perf_test_vcpu_args vcpu_args[KVM_MAX_VCPUS];
};

extern struct perf_test_args perf_test_args;

struct kvm_vm *perf_test_create_vm(enum vm_guest_mode mode, int nr_vcpus,
				   uint64_t vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src,
				   bool partition_vcpu_memory_access);
void perf_test_destroy_vm(struct kvm_vm *vm);

void perf_test_set_wr_fract(struct kvm_vm *vm, int wr_fract);

void perf_test_start_vcpu_threads(int vcpus, void (*vcpu_fn)(struct perf_test_vcpu_args *));
void perf_test_join_vcpu_threads(int vcpus);
void perf_test_guest_code(uint32_t vcpu_id);

uint64_t perf_test_nested_pages(int nr_vcpus);
void perf_test_setup_nested(struct kvm_vm *vm, int nr_vcpus, struct kvm_vcpu *vcpus[]);

#endif /* SELFTEST_KVM_PERF_TEST_UTIL_H */
