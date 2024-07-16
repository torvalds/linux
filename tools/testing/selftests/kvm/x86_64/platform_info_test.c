// SPDX-License-Identifier: GPL-2.0
/*
 * Test for x86 KVM_CAP_MSR_PLATFORM_INFO
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Verifies expected behavior of controlling guest access to
 * MSR_PLATFORM_INFO.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define MSR_PLATFORM_INFO_MAX_TURBO_RATIO 0xff00

static void guest_code(void)
{
	uint64_t msr_platform_info;

	for (;;) {
		msr_platform_info = rdmsr(MSR_PLATFORM_INFO);
		GUEST_SYNC(msr_platform_info);
		asm volatile ("inc %r11");
	}
}

static void test_msr_platform_info_enabled(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	struct ucall uc;

	vm_enable_cap(vcpu->vm, KVM_CAP_MSR_PLATFORM_INFO, true);
	vcpu_run(vcpu);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			"Exit_reason other than KVM_EXIT_IO: %u (%s),\n",
			run->exit_reason,
			exit_reason_str(run->exit_reason));
	get_ucall(vcpu, &uc);
	TEST_ASSERT(uc.cmd == UCALL_SYNC,
			"Received ucall other than UCALL_SYNC: %lu\n", uc.cmd);
	TEST_ASSERT((uc.args[1] & MSR_PLATFORM_INFO_MAX_TURBO_RATIO) ==
		MSR_PLATFORM_INFO_MAX_TURBO_RATIO,
		"Expected MSR_PLATFORM_INFO to have max turbo ratio mask: %i.",
		MSR_PLATFORM_INFO_MAX_TURBO_RATIO);
}

static void test_msr_platform_info_disabled(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	vm_enable_cap(vcpu->vm, KVM_CAP_MSR_PLATFORM_INFO, false);
	vcpu_run(vcpu);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN,
			"Exit_reason other than KVM_EXIT_SHUTDOWN: %u (%s)\n",
			run->exit_reason,
			exit_reason_str(run->exit_reason));
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t msr_platform_info;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_MSR_PLATFORM_INFO));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	msr_platform_info = vcpu_get_msr(vcpu, MSR_PLATFORM_INFO);
	vcpu_set_msr(vcpu, MSR_PLATFORM_INFO,
		     msr_platform_info | MSR_PLATFORM_INFO_MAX_TURBO_RATIO);
	test_msr_platform_info_enabled(vcpu);
	test_msr_platform_info_disabled(vcpu);
	vcpu_set_msr(vcpu, MSR_PLATFORM_INFO, msr_platform_info);

	kvm_vm_free(vm);

	return 0;
}
