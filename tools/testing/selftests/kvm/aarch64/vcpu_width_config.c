// SPDX-License-Identifier: GPL-2.0-only
/*
 * vcpu_width_config - Test KVM_ARM_VCPU_INIT() with KVM_ARM_VCPU_EL1_32BIT.
 *
 * Copyright (c) 2022 Google LLC.
 *
 * This is a test that ensures that non-mixed-width vCPUs (all 64bit vCPUs
 * or all 32bit vcPUs) can be configured and mixed-width vCPUs cannot be
 * configured.
 */

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"


/*
 * Add a vCPU, run KVM_ARM_VCPU_INIT with @init1, and then
 * add another vCPU, and run KVM_ARM_VCPU_INIT with @init2.
 */
static int add_init_2vcpus(struct kvm_vcpu_init *init1,
			   struct kvm_vcpu_init *init2)
{
	struct kvm_vm *vm;
	int ret;

	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);

	vm_vcpu_add(vm, 0);
	ret = _vcpu_ioctl(vm, 0, KVM_ARM_VCPU_INIT, init1);
	if (ret)
		goto free_exit;

	vm_vcpu_add(vm, 1);
	ret = _vcpu_ioctl(vm, 1, KVM_ARM_VCPU_INIT, init2);

free_exit:
	kvm_vm_free(vm);
	return ret;
}

/*
 * Add two vCPUs, then run KVM_ARM_VCPU_INIT for one vCPU with @init1,
 * and run KVM_ARM_VCPU_INIT for another vCPU with @init2.
 */
static int add_2vcpus_init_2vcpus(struct kvm_vcpu_init *init1,
				  struct kvm_vcpu_init *init2)
{
	struct kvm_vm *vm;
	int ret;

	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);

	vm_vcpu_add(vm, 0);
	vm_vcpu_add(vm, 1);

	ret = _vcpu_ioctl(vm, 0, KVM_ARM_VCPU_INIT, init1);
	if (ret)
		goto free_exit;

	ret = _vcpu_ioctl(vm, 1, KVM_ARM_VCPU_INIT, init2);

free_exit:
	kvm_vm_free(vm);
	return ret;
}

/*
 * Tests that two 64bit vCPUs can be configured, two 32bit vCPUs can be
 * configured, and two mixed-width vCPUs cannot be configured.
 * Each of those three cases, configure vCPUs in two different orders.
 * The one is running KVM_CREATE_VCPU for 2 vCPUs, and then running
 * KVM_ARM_VCPU_INIT for them.
 * The other is running KVM_CREATE_VCPU and KVM_ARM_VCPU_INIT for a vCPU,
 * and then run those commands for another vCPU.
 */
int main(void)
{
	struct kvm_vcpu_init init1, init2;
	struct kvm_vm *vm;
	int ret;

	if (!kvm_check_cap(KVM_CAP_ARM_EL1_32BIT)) {
		print_skip("KVM_CAP_ARM_EL1_32BIT is not supported");
		exit(KSFT_SKIP);
	}

	/* Get the preferred target type and copy that to init2 for later use */
	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);
	vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &init1);
	kvm_vm_free(vm);
	init2 = init1;

	/* Test with 64bit vCPUs */
	ret = add_init_2vcpus(&init1, &init1);
	TEST_ASSERT(ret == 0,
		    "Configuring 64bit EL1 vCPUs failed unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init1, &init1);
	TEST_ASSERT(ret == 0,
		    "Configuring 64bit EL1 vCPUs failed unexpectedly");

	/* Test with 32bit vCPUs */
	init1.features[0] = (1 << KVM_ARM_VCPU_EL1_32BIT);
	ret = add_init_2vcpus(&init1, &init1);
	TEST_ASSERT(ret == 0,
		    "Configuring 32bit EL1 vCPUs failed unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init1, &init1);
	TEST_ASSERT(ret == 0,
		    "Configuring 32bit EL1 vCPUs failed unexpectedly");

	/* Test with mixed-width vCPUs  */
	init1.features[0] = 0;
	init2.features[0] = (1 << KVM_ARM_VCPU_EL1_32BIT);
	ret = add_init_2vcpus(&init1, &init2);
	TEST_ASSERT(ret != 0,
		    "Configuring mixed-width vCPUs worked unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init1, &init2);
	TEST_ASSERT(ret != 0,
		    "Configuring mixed-width vCPUs worked unexpectedly");

	return 0;
}
