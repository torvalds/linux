// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Google LLC.
 */
#include <linux/kvm.h>
#include <pthread.h>
#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"

/* Arbitrarily selected to avoid overlaps with anything else */
#define EXITS_TEST_GVA 0xc0000000
#define EXITS_TEST_GPA EXITS_TEST_GVA
#define EXITS_TEST_NPAGES 1
#define EXITS_TEST_SIZE (EXITS_TEST_NPAGES * PAGE_SIZE)
#define EXITS_TEST_SLOT 10

static uint64_t guest_repeatedly_read(void)
{
	volatile uint64_t value;

	while (true)
		value = *((uint64_t *) EXITS_TEST_GVA);

	return value;
}

static uint32_t run_vcpu_get_exit_reason(struct kvm_vcpu *vcpu)
{
	int r;

	r = _vcpu_run(vcpu);
	if (r) {
		TEST_ASSERT(errno == EFAULT, KVM_IOCTL_ERROR(KVM_RUN, r));
		TEST_ASSERT_EQ(vcpu->run->exit_reason, KVM_EXIT_MEMORY_FAULT);
	}
	return vcpu->run->exit_reason;
}

const struct vm_shape protected_vm_shape = {
	.mode = VM_MODE_DEFAULT,
	.type = KVM_X86_SW_PROTECTED_VM,
};

static void test_private_access_memslot_deleted(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	pthread_t vm_thread;
	void *thread_return;
	uint32_t exit_reason;

	vm = vm_create_shape_with_one_vcpu(protected_vm_shape, &vcpu,
					   guest_repeatedly_read);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    EXITS_TEST_GPA, EXITS_TEST_SLOT,
				    EXITS_TEST_NPAGES,
				    KVM_MEM_GUEST_MEMFD);

	virt_map(vm, EXITS_TEST_GVA, EXITS_TEST_GPA, EXITS_TEST_NPAGES);

	/* Request to access page privately */
	vm_mem_set_private(vm, EXITS_TEST_GPA, EXITS_TEST_SIZE);

	pthread_create(&vm_thread, NULL,
		       (void *(*)(void *))run_vcpu_get_exit_reason,
		       (void *)vcpu);

	vm_mem_region_delete(vm, EXITS_TEST_SLOT);

	pthread_join(vm_thread, &thread_return);
	exit_reason = (uint32_t)(uint64_t)thread_return;

	TEST_ASSERT_EQ(exit_reason, KVM_EXIT_MEMORY_FAULT);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.flags, KVM_MEMORY_EXIT_FLAG_PRIVATE);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.gpa, EXITS_TEST_GPA);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.size, EXITS_TEST_SIZE);

	kvm_vm_free(vm);
}

static void test_private_access_memslot_not_private(void)
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	uint32_t exit_reason;

	vm = vm_create_shape_with_one_vcpu(protected_vm_shape, &vcpu,
					   guest_repeatedly_read);

	/* Add a non-private memslot (flags = 0) */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    EXITS_TEST_GPA, EXITS_TEST_SLOT,
				    EXITS_TEST_NPAGES, 0);

	virt_map(vm, EXITS_TEST_GVA, EXITS_TEST_GPA, EXITS_TEST_NPAGES);

	/* Request to access page privately */
	vm_mem_set_private(vm, EXITS_TEST_GPA, EXITS_TEST_SIZE);

	exit_reason = run_vcpu_get_exit_reason(vcpu);

	TEST_ASSERT_EQ(exit_reason, KVM_EXIT_MEMORY_FAULT);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.flags, KVM_MEMORY_EXIT_FLAG_PRIVATE);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.gpa, EXITS_TEST_GPA);
	TEST_ASSERT_EQ(vcpu->run->memory_fault.size, EXITS_TEST_SIZE);

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SW_PROTECTED_VM));

	test_private_access_memslot_deleted();
	test_private_access_memslot_not_private();
}
