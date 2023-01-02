// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Google LLC.
 *
 * Tests for adjusting the system counter from userspace
 */
#include <asm/kvm_para.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#ifdef __x86_64__

struct test_case {
	uint64_t tsc_offset;
};

static struct test_case test_cases[] = {
	{ 0 },
	{ 180 * NSEC_PER_SEC },
	{ -180 * NSEC_PER_SEC },
};

static void check_preconditions(struct kvm_vcpu *vcpu)
{
	__TEST_REQUIRE(!__vcpu_has_device_attr(vcpu, KVM_VCPU_TSC_CTRL,
					       KVM_VCPU_TSC_OFFSET),
		       "KVM_VCPU_TSC_OFFSET not supported; skipping test");
}

static void setup_system_counter(struct kvm_vcpu *vcpu, struct test_case *test)
{
	vcpu_device_attr_set(vcpu, KVM_VCPU_TSC_CTRL, KVM_VCPU_TSC_OFFSET,
			     &test->tsc_offset);
}

static uint64_t guest_read_system_counter(struct test_case *test)
{
	return rdtsc();
}

static uint64_t host_read_guest_system_counter(struct test_case *test)
{
	return rdtsc() + test->tsc_offset;
}

#else /* __x86_64__ */

#error test not implemented for this architecture!

#endif

#define GUEST_SYNC_CLOCK(__stage, __val)			\
		GUEST_SYNC_ARGS(__stage, __val, 0, 0, 0)

static void guest_main(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct test_case *test = &test_cases[i];

		GUEST_SYNC_CLOCK(i, guest_read_system_counter(test));
	}
}

static void handle_sync(struct ucall *uc, uint64_t start, uint64_t end)
{
	uint64_t obs = uc->args[2];

	TEST_ASSERT(start <= obs && obs <= end,
		    "unexpected system counter value: %"PRIu64" expected range: [%"PRIu64", %"PRIu64"]",
		    obs, start, end);

	pr_info("system counter value: %"PRIu64" expected range [%"PRIu64", %"PRIu64"]\n",
		obs, start, end);
}

static void handle_abort(struct ucall *uc)
{
	REPORT_GUEST_ASSERT(*uc);
}

static void enter_guest(struct kvm_vcpu *vcpu)
{
	uint64_t start, end;
	struct ucall uc;
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		struct test_case *test = &test_cases[i];

		setup_system_counter(vcpu, test);
		start = host_read_guest_system_counter(test);
		vcpu_run(vcpu);
		end = host_read_guest_system_counter(test);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			handle_sync(&uc, start, end);
			break;
		case UCALL_ABORT:
			handle_abort(&uc);
			return;
		default:
			TEST_ASSERT(0, "unhandled ucall %ld\n",
				    get_ucall(vcpu, &uc));
		}
	}
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);
	check_preconditions(vcpu);

	enter_guest(vcpu);
	kvm_vm_free(vm);
}
