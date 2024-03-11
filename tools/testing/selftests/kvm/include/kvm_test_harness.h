/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Macros for defining a KVM test
 *
 * Copyright (C) 2022, Google LLC.
 */

#ifndef SELFTEST_KVM_TEST_HARNESS_H
#define SELFTEST_KVM_TEST_HARNESS_H

#include "kselftest_harness.h"

#define KVM_ONE_VCPU_TEST_SUITE(name)					\
	FIXTURE(name) {							\
		struct kvm_vcpu *vcpu;					\
	};								\
									\
	FIXTURE_SETUP(name) {						\
		(void)vm_create_with_one_vcpu(&self->vcpu, NULL);	\
	}								\
									\
	FIXTURE_TEARDOWN(name) {					\
		kvm_vm_free(self->vcpu->vm);				\
	}

#define KVM_ONE_VCPU_TEST(suite, test, guestcode)			\
static void __suite##_##test(struct kvm_vcpu *vcpu);			\
									\
TEST_F(suite, test)							\
{									\
	vcpu_arch_set_entry_point(self->vcpu, guestcode);		\
	__suite##_##test(self->vcpu);					\
}									\
static void __suite##_##test(struct kvm_vcpu *vcpu)

#endif /* SELFTEST_KVM_TEST_HARNESS_H */
