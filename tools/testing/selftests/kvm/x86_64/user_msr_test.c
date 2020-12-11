// SPDX-License-Identifier: GPL-2.0-only
/*
 * tests for KVM_CAP_X86_USER_SPACE_MSR and KVM_X86_SET_MSR_FILTER
 *
 * Copyright (C) 2020, Amazon Inc.
 *
 * This is a functional test to verify that we can deflect MSR events
 * into user space.
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

#define VCPU_ID                  5

static u32 msr_reads, msr_writes;

static u8 bitmap_00000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_00000000_write[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_40000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_c0000000[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_c0000000_read[KVM_MSR_FILTER_MAX_BITMAP_SIZE];
static u8 bitmap_deadbeef[1] = { 0x1 };

static void deny_msr(uint8_t *bitmap, u32 msr)
{
	u32 idx = msr & (KVM_MSR_FILTER_MAX_BITMAP_SIZE - 1);

	bitmap[idx / 8] &= ~(1 << (idx % 8));
}

static void prepare_bitmaps(void)
{
	memset(bitmap_00000000, 0xff, sizeof(bitmap_00000000));
	memset(bitmap_00000000_write, 0xff, sizeof(bitmap_00000000_write));
	memset(bitmap_40000000, 0xff, sizeof(bitmap_40000000));
	memset(bitmap_c0000000, 0xff, sizeof(bitmap_c0000000));
	memset(bitmap_c0000000_read, 0xff, sizeof(bitmap_c0000000_read));

	deny_msr(bitmap_00000000_write, MSR_IA32_POWER_CTL);
	deny_msr(bitmap_c0000000_read, MSR_SYSCALL_MASK);
	deny_msr(bitmap_c0000000_read, MSR_GS_BASE);
}

struct kvm_msr_filter filter = {
	.flags = KVM_MSR_FILTER_DEFAULT_DENY,
	.ranges = {
		{
			.flags = KVM_MSR_FILTER_READ,
			.base = 0x00000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_00000000,
		}, {
			.flags = KVM_MSR_FILTER_WRITE,
			.base = 0x00000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_00000000_write,
		}, {
			.flags = KVM_MSR_FILTER_READ | KVM_MSR_FILTER_WRITE,
			.base = 0x40000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_40000000,
		}, {
			.flags = KVM_MSR_FILTER_READ,
			.base = 0xc0000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_c0000000_read,
		}, {
			.flags = KVM_MSR_FILTER_WRITE,
			.base = 0xc0000000,
			.nmsrs = KVM_MSR_FILTER_MAX_BITMAP_SIZE * BITS_PER_BYTE,
			.bitmap = bitmap_c0000000,
		}, {
			.flags = KVM_MSR_FILTER_WRITE | KVM_MSR_FILTER_READ,
			.base = 0xdeadbeef,
			.nmsrs = 1,
			.bitmap = bitmap_deadbeef,
		},
	},
};

struct kvm_msr_filter no_filter = {
	.flags = KVM_MSR_FILTER_DEFAULT_ALLOW,
};

static void guest_msr_calls(bool trapped)
{
	/* This goes into the in-kernel emulation */
	wrmsr(MSR_SYSCALL_MASK, 0);

	if (trapped) {
		/* This goes into user space emulation */
		GUEST_ASSERT(rdmsr(MSR_SYSCALL_MASK) == MSR_SYSCALL_MASK);
		GUEST_ASSERT(rdmsr(MSR_GS_BASE) == MSR_GS_BASE);
	} else {
		GUEST_ASSERT(rdmsr(MSR_SYSCALL_MASK) != MSR_SYSCALL_MASK);
		GUEST_ASSERT(rdmsr(MSR_GS_BASE) != MSR_GS_BASE);
	}

	/* If trapped == true, this goes into user space emulation */
	wrmsr(MSR_IA32_POWER_CTL, 0x1234);

	/* This goes into the in-kernel emulation */
	rdmsr(MSR_IA32_POWER_CTL);

	/* Invalid MSR, should always be handled by user space exit */
	GUEST_ASSERT(rdmsr(0xdeadbeef) == 0xdeadbeef);
	wrmsr(0xdeadbeef, 0x1234);
}

static void guest_code(void)
{
	guest_msr_calls(true);

	/*
	 * Disable msr filtering, so that the kernel
	 * handles everything in the next round
	 */
	GUEST_SYNC(0);

	guest_msr_calls(false);

	GUEST_DONE();
}

static int handle_ucall(struct kvm_vm *vm)
{
	struct ucall uc;

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_ABORT:
		TEST_FAIL("Guest assertion not met");
		break;
	case UCALL_SYNC:
		vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &no_filter);
		break;
	case UCALL_DONE:
		return 1;
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
	}

	return 0;
}

static void handle_rdmsr(struct kvm_run *run)
{
	run->msr.data = run->msr.index;
	msr_reads++;

	if (run->msr.index == MSR_SYSCALL_MASK ||
	    run->msr.index == MSR_GS_BASE) {
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER,
			    "MSR read trap w/o access fault");
	}

	if (run->msr.index == 0xdeadbeef) {
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN,
			    "MSR deadbeef read trap w/o inval fault");
	}
}

static void handle_wrmsr(struct kvm_run *run)
{
	/* ignore */
	msr_writes++;

	if (run->msr.index == MSR_IA32_POWER_CTL) {
		TEST_ASSERT(run->msr.data == 0x1234,
			    "MSR data for MSR_IA32_POWER_CTL incorrect");
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_FILTER,
			    "MSR_IA32_POWER_CTL trap w/o access fault");
	}

	if (run->msr.index == 0xdeadbeef) {
		TEST_ASSERT(run->msr.data == 0x1234,
			    "MSR data for deadbeef incorrect");
		TEST_ASSERT(run->msr.reason == KVM_MSR_EXIT_REASON_UNKNOWN,
			    "deadbeef trap w/o inval fault");
	}
}

int main(int argc, char *argv[])
{
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_X86_USER_SPACE_MSR,
		.args[0] = KVM_MSR_EXIT_REASON_INVAL |
			   KVM_MSR_EXIT_REASON_UNKNOWN |
			   KVM_MSR_EXIT_REASON_FILTER,
	};
	struct kvm_vm *vm;
	struct kvm_run *run;
	int rc;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	run = vcpu_state(vm, VCPU_ID);

	rc = kvm_check_cap(KVM_CAP_X86_USER_SPACE_MSR);
	TEST_ASSERT(rc, "KVM_CAP_X86_USER_SPACE_MSR is available");
	vm_enable_cap(vm, &cap);

	rc = kvm_check_cap(KVM_CAP_X86_MSR_FILTER);
	TEST_ASSERT(rc, "KVM_CAP_X86_MSR_FILTER is available");

	prepare_bitmaps();
	vm_ioctl(vm, KVM_X86_SET_MSR_FILTER, &filter);

	while (1) {
		rc = _vcpu_run(vm, VCPU_ID);

		TEST_ASSERT(rc == 0, "vcpu_run failed: %d\n", rc);

		switch (run->exit_reason) {
		case KVM_EXIT_X86_RDMSR:
			handle_rdmsr(run);
			break;
		case KVM_EXIT_X86_WRMSR:
			handle_wrmsr(run);
			break;
		case KVM_EXIT_IO:
			if (handle_ucall(vm))
				goto done;
			break;
		}

	}

done:
	TEST_ASSERT(msr_reads == 4, "Handled 4 rdmsr in user space");
	TEST_ASSERT(msr_writes == 3, "Handled 3 wrmsr in user space");

	kvm_vm_free(vm);

	return 0;
}
