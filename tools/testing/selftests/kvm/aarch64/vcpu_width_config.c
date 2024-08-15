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
 * Add a vCPU, run KVM_ARM_VCPU_INIT with @init0, and then
 * add another vCPU, and run KVM_ARM_VCPU_INIT with @init1.
 */
static int add_init_2vcpus(struct kvm_vcpu_init *init0,
			   struct kvm_vcpu_init *init1)
{
	struct kvm_vcpu *vcpu0, *vcpu1;
	struct kvm_vm *vm;
	int ret;

	vm = vm_create_barebones();

	vcpu0 = __vm_vcpu_add(vm, 0);
	ret = __vcpu_ioctl(vcpu0, KVM_ARM_VCPU_INIT, init0);
	if (ret)
		goto free_exit;

	vcpu1 = __vm_vcpu_add(vm, 1);
	ret = __vcpu_ioctl(vcpu1, KVM_ARM_VCPU_INIT, init1);

free_exit:
	kvm_vm_free(vm);
	return ret;
}

/*
 * Add two vCPUs, then run KVM_ARM_VCPU_INIT for one vCPU with @init0,
 * and run KVM_ARM_VCPU_INIT for another vCPU with @init1.
 */
static int add_2vcpus_init_2vcpus(struct kvm_vcpu_init *init0,
				  struct kvm_vcpu_init *init1)
{
	struct kvm_vcpu *vcpu0, *vcpu1;
	struct kvm_vm *vm;
	int ret;

	vm = vm_create_barebones();

	vcpu0 = __vm_vcpu_add(vm, 0);
	vcpu1 = __vm_vcpu_add(vm, 1);

	ret = __vcpu_ioctl(vcpu0, KVM_ARM_VCPU_INIT, init0);
	if (ret)
		goto free_exit;

	ret = __vcpu_ioctl(vcpu1, KVM_ARM_VCPU_INIT, init1);

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
	struct kvm_vcpu_init init0, init1;
	struct kvm_vm *vm;
	int ret;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ARM_EL1_32BIT));

	/* Get the preferred target type and copy that to init1 for later use */
	vm = vm_create_barebones();
	vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &init0);
	kvm_vm_free(vm);
	init1 = init0;

	/* Test with 64bit vCPUs */
	ret = add_init_2vcpus(&init0, &init0);
	TEST_ASSERT(ret == 0,
		    "Configuring 64bit EL1 vCPUs failed unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init0, &init0);
	TEST_ASSERT(ret == 0,
		    "Configuring 64bit EL1 vCPUs failed unexpectedly");

	/* Test with 32bit vCPUs */
	init0.features[0] = (1 << KVM_ARM_VCPU_EL1_32BIT);
	ret = add_init_2vcpus(&init0, &init0);
	TEST_ASSERT(ret == 0,
		    "Configuring 32bit EL1 vCPUs failed unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init0, &init0);
	TEST_ASSERT(ret == 0,
		    "Configuring 32bit EL1 vCPUs failed unexpectedly");

	/* Test with mixed-width vCPUs  */
	init0.features[0] = 0;
	init1.features[0] = (1 << KVM_ARM_VCPU_EL1_32BIT);
	ret = add_init_2vcpus(&init0, &init1);
	TEST_ASSERT(ret != 0,
		    "Configuring mixed-width vCPUs worked unexpectedly");
	ret = add_2vcpus_init_2vcpus(&init0, &init1);
	TEST_ASSERT(ret != 0,
		    "Configuring mixed-width vCPUs worked unexpectedly");

	return 0;
}
