// SPDX-License-Identifier: GPL-2.0
/*
 * Test that KVM_SET_BOOT_CPU_ID works as intended
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "apic.h"

static void guest_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT_NE(get_bsp_flag(), 0);

	GUEST_DONE();
}

static void guest_not_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT_EQ(get_bsp_flag(), 0);

	GUEST_DONE();
}

static void test_set_bsp_busy(struct kvm_vcpu *vcpu, const char *msg)
{
	int r = __vm_ioctl(vcpu->vm, KVM_SET_BOOT_CPU_ID,
			   (void *)(unsigned long)vcpu->id);

	TEST_ASSERT(r == -1 && errno == EBUSY, "KVM_SET_BOOT_CPU_ID set %s", msg);
}

static void run_vcpu(struct kvm_vcpu *vcpu)
{
	struct ucall uc;
	int stage;

	for (stage = 0; stage < 2; stage++) {

		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
					uc.args[1] == stage + 1,
					"Stage %d: Unexpected register values vmexit, got %lx",
					stage + 1, (ulong)uc.args[1]);
			test_set_bsp_busy(vcpu, "while running vm");
			break;
		case UCALL_DONE:
			TEST_ASSERT(stage == 1,
					"Expected GUEST_DONE in stage 2, got stage %d",
					stage);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_ASSERT(false, "Unexpected exit: %s",
				    exit_reason_str(vcpu->run->exit_reason));
		}
	}
}

static struct kvm_vm *create_vm(uint32_t nr_vcpus, uint32_t bsp_vcpu_id,
				struct kvm_vcpu *vcpus[])
{
	struct kvm_vm *vm;
	uint32_t i;

	vm = vm_create(nr_vcpus);

	vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *)(unsigned long)bsp_vcpu_id);

	for (i = 0; i < nr_vcpus; i++)
		vcpus[i] = vm_vcpu_add(vm, i, i == bsp_vcpu_id ? guest_bsp_vcpu :
								 guest_not_bsp_vcpu);
	return vm;
}

static void run_vm_bsp(uint32_t bsp_vcpu_id)
{
	struct kvm_vcpu *vcpus[2];
	struct kvm_vm *vm;

	vm = create_vm(ARRAY_SIZE(vcpus), bsp_vcpu_id, vcpus);

	run_vcpu(vcpus[0]);
	run_vcpu(vcpus[1]);

	kvm_vm_free(vm);
}

static void check_set_bsp_busy(void)
{
	struct kvm_vcpu *vcpus[2];
	struct kvm_vm *vm;

	vm = create_vm(ARRAY_SIZE(vcpus), 0, vcpus);

	test_set_bsp_busy(vcpus[1], "after adding vcpu");

	run_vcpu(vcpus[0]);
	run_vcpu(vcpus[1]);

	test_set_bsp_busy(vcpus[1], "to a terminated vcpu");

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SET_BOOT_CPU_ID));

	run_vm_bsp(0);
	run_vm_bsp(1);
	run_vm_bsp(0);

	check_set_bsp_busy();
}
