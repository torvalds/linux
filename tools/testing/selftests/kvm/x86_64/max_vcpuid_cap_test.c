// SPDX-License-Identifier: GPL-2.0-only
/*
 * maximum APIC ID capability tests
 *
 * Copyright (C) 2022, Intel, Inc.
 *
 * Tests for getting/setting maximum APIC ID capability
 */

#include "kvm_util.h"
#include "../lib/kvm_util_internal.h"

#define MAX_VCPU_ID	2

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_enable_cap cap = { 0 };
	int ret;

	vm = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);

	/* Get KVM_CAP_MAX_VCPU_ID cap supported in KVM */
	ret = vm_check_cap(vm, KVM_CAP_MAX_VCPU_ID);

	/* Try to set KVM_CAP_MAX_VCPU_ID beyond KVM cap */
	cap.cap = KVM_CAP_MAX_VCPU_ID;
	cap.args[0] = ret + 1;
	ret = ioctl(vm->fd, KVM_ENABLE_CAP, &cap);
	TEST_ASSERT(ret < 0,
		    "Unexpected success to enable KVM_CAP_MAX_VCPU_ID"
		    "beyond KVM cap!\n");

	/* Set KVM_CAP_MAX_VCPU_ID */
	cap.cap = KVM_CAP_MAX_VCPU_ID;
	cap.args[0] = MAX_VCPU_ID;
	ret = ioctl(vm->fd, KVM_ENABLE_CAP, &cap);
	TEST_ASSERT(ret == 0,
		    "Unexpected failure to enable KVM_CAP_MAX_VCPU_ID!\n");

	/* Try to set KVM_CAP_MAX_VCPU_ID again */
	cap.args[0] = MAX_VCPU_ID + 1;
	ret = ioctl(vm->fd, KVM_ENABLE_CAP, &cap);
	TEST_ASSERT(ret < 0,
		    "Unexpected success to enable KVM_CAP_MAX_VCPU_ID again\n");

	/* Create vCPU with id beyond KVM_CAP_MAX_VCPU_ID cap*/
	ret = ioctl(vm->fd, KVM_CREATE_VCPU, MAX_VCPU_ID);
	TEST_ASSERT(ret < 0,
		    "Unexpected success in creating a vCPU with VCPU ID out of range\n");

	kvm_vm_free(vm);
	return 0;
}
