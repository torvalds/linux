/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timer test specific header
 *
 * Copyright (C) 2018, Google LLC
 */

#ifndef SELFTEST_KVM_TIMER_TEST_H
#define SELFTEST_KVM_TIMER_TEST_H

#include "kvm_util.h"

#define NR_VCPUS_DEF            4
#define NR_TEST_ITERS_DEF       5
#define TIMER_TEST_PERIOD_MS_DEF    10
#define TIMER_TEST_ERR_MARGIN_US    100
#define TIMER_TEST_MIGRATION_FREQ_MS    2

/* Timer test cmdline parameters */
struct test_args {
	uint32_t nr_vcpus;
	uint32_t nr_iter;
	uint32_t timer_period_ms;
	uint32_t migration_freq_ms;
	uint32_t timer_err_margin_us;
	/* Members of struct kvm_arm_counter_offset */
	uint64_t counter_offset;
	uint64_t reserved;
};

/* Shared variables between host and guest */
struct test_vcpu_shared_data {
	uint32_t nr_iter;
	int guest_stage;
	uint64_t xcnt;
};

extern struct test_args test_args;
extern struct kvm_vcpu *vcpus[];
extern struct test_vcpu_shared_data vcpu_shared_data[];

struct kvm_vm *test_vm_create(void);
void test_vm_cleanup(struct kvm_vm *vm);

#endif /* SELFTEST_KVM_TIMER_TEST_H */
