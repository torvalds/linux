// SPDX-License-Identifier: GPL-2.0
/*
 * Test that KVM_SET_BOOT_CPU_ID works as intended
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#define _GNU_SOURCE /* for program_invocation_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "apic.h"

#define N_VCPU 2
#define VCPU_ID0 0
#define VCPU_ID1 1

static void guest_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT(get_bsp_flag() != 0);

	GUEST_DONE();
}

static void guest_not_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT(get_bsp_flag() == 0);

	GUEST_DONE();
}

static void test_set_boot_busy(struct kvm_vm *vm)
{
	int res;

	res = _vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) VCPU_ID0);
	TEST_ASSERT(res == -1 && errno == EBUSY,
			"KVM_SET_BOOT_CPU_ID set while running vm");
}

static void run_vcpu(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct ucall uc;
	int stage;

	for (stage = 0; stage < 2; stage++) {

		vcpu_run(vm, vcpuid);

		switch (get_ucall(vm, vcpuid, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
					uc.args[1] == stage + 1,
					"Stage %d: Unexpected register values vmexit, got %lx",
					stage + 1, (ulong)uc.args[1]);
			test_set_boot_busy(vm);
			break;
		case UCALL_DONE:
			TEST_ASSERT(stage == 1,
					"Expected GUEST_DONE in stage 2, got stage %d",
					stage);
			break;
		case UCALL_ABORT:
			TEST_ASSERT(false, "%s at %s:%ld\n\tvalues: %#lx, %#lx",
						(const char *)uc.args[0], __FILE__,
						uc.args[1], uc.args[2], uc.args[3]);
		default:
			TEST_ASSERT(false, "Unexpected exit: %s",
					exit_reason_str(vcpu_state(vm, vcpuid)->exit_reason));
		}
	}
}

static struct kvm_vm *create_vm(void)
{
	struct kvm_vm *vm;
	uint64_t vcpu_pages = (DEFAULT_STACK_PGS) * 2;
	uint64_t extra_pg_pages = vcpu_pages / PTES_PER_MIN_PAGE * N_VCPU;
	uint64_t pages = DEFAULT_GUEST_PHY_PAGES + vcpu_pages + extra_pg_pages;

	pages = vm_adjust_num_guest_pages(VM_MODE_DEFAULT, pages);
	vm = vm_create(VM_MODE_DEFAULT, pages, O_RDWR);

	kvm_vm_elf_load(vm, program_invocation_name);
	vm_create_irqchip(vm);

	return vm;
}

static void add_x86_vcpu(struct kvm_vm *vm, uint32_t vcpuid, bool bsp_code)
{
	if (bsp_code)
		vm_vcpu_add_default(vm, vcpuid, guest_bsp_vcpu);
	else
		vm_vcpu_add_default(vm, vcpuid, guest_not_bsp_vcpu);
}

static void run_vm_bsp(uint32_t bsp_vcpu)
{
	struct kvm_vm *vm;
	bool is_bsp_vcpu1 = bsp_vcpu == VCPU_ID1;

	vm = create_vm();

	if (is_bsp_vcpu1)
		vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) VCPU_ID1);

	add_x86_vcpu(vm, VCPU_ID0, !is_bsp_vcpu1);
	add_x86_vcpu(vm, VCPU_ID1, is_bsp_vcpu1);

	run_vcpu(vm, VCPU_ID0);
	run_vcpu(vm, VCPU_ID1);

	kvm_vm_free(vm);
}

static void check_set_bsp_busy(void)
{
	struct kvm_vm *vm;
	int res;

	vm = create_vm();

	add_x86_vcpu(vm, VCPU_ID0, true);
	add_x86_vcpu(vm, VCPU_ID1, false);

	res = _vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) VCPU_ID1);
	TEST_ASSERT(res == -1 && errno == EBUSY, "KVM_SET_BOOT_CPU_ID set after adding vcpu");

	run_vcpu(vm, VCPU_ID0);
	run_vcpu(vm, VCPU_ID1);

	res = _vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *) VCPU_ID1);
	TEST_ASSERT(res == -1 && errno == EBUSY, "KVM_SET_BOOT_CPU_ID set to a terminated vcpu");

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	if (!kvm_check_cap(KVM_CAP_SET_BOOT_CPU_ID)) {
		print_skip("set_boot_cpu_id not available");
		return 0;
	}

	run_vm_bsp(VCPU_ID0);
	run_vm_bsp(VCPU_ID1);
	run_vm_bsp(VCPU_ID0);

	check_set_bsp_busy();
}
