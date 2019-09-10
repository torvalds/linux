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
#include "x86.h"

#define VCPU_ID 0
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

static void set_msr_platform_info_enabled(struct kvm_vm *vm, bool enable)
{
	struct kvm_enable_cap cap = {};

	cap.cap = KVM_CAP_MSR_PLATFORM_INFO;
	cap.flags = 0;
	cap.args[0] = (int)enable;
	vm_enable_cap(vm, &cap);
}

static void test_msr_platform_info_enabled(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct guest_args args;

	set_msr_platform_info_enabled(vm, true);
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			"Exit_reason other than KVM_EXIT_IO: %u (%s),\n",
			run->exit_reason,
			exit_reason_str(run->exit_reason));
	guest_args_read(vm, VCPU_ID, &args);
	TEST_ASSERT(args.port == GUEST_PORT_SYNC,
			"Received IO from port other than PORT_HOST_SYNC: %u\n",
			run->io.port);
	TEST_ASSERT((args.arg1 & MSR_PLATFORM_INFO_MAX_TURBO_RATIO) ==
		MSR_PLATFORM_INFO_MAX_TURBO_RATIO,
		"Expected MSR_PLATFORM_INFO to have max turbo ratio mask: %i.",
		MSR_PLATFORM_INFO_MAX_TURBO_RATIO);
}

static void test_msr_platform_info_disabled(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);

	set_msr_platform_info_enabled(vm, false);
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN,
			"Exit_reason other than KVM_EXIT_SHUTDOWN: %u (%s)\n",
			run->exit_reason,
			exit_reason_str(run->exit_reason));
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_run *state;
	int rv;
	uint64_t msr_platform_info;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	rv = kvm_check_cap(KVM_CAP_MSR_PLATFORM_INFO);
	if (!rv) {
		fprintf(stderr,
			"KVM_CAP_MSR_PLATFORM_INFO not supported, skip test\n");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	msr_platform_info = vcpu_get_msr(vm, VCPU_ID, MSR_PLATFORM_INFO);
	vcpu_set_msr(vm, VCPU_ID, MSR_PLATFORM_INFO,
		msr_platform_info | MSR_PLATFORM_INFO_MAX_TURBO_RATIO);
	test_msr_platform_info_enabled(vm);
	test_msr_platform_info_disabled(vm);
	vcpu_set_msr(vm, VCPU_ID, MSR_PLATFORM_INFO, msr_platform_info);

	kvm_vm_free(vm);

	return 0;
}
