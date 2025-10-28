// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, Intel, Inc
 *
 * Author:
 * Isaku Yamahata <isaku.yamahata at gmail.com>
 */
#include <linux/sizes.h>

#include <test_util.h>
#include <kvm_util.h>
#include <processor.h>
#include <pthread.h>

/* Arbitrarily chosen values */
#define TEST_SIZE		(SZ_2M + PAGE_SIZE)
#define TEST_NPAGES		(TEST_SIZE / PAGE_SIZE)
#define TEST_SLOT		10

static void guest_code(uint64_t base_gpa)
{
	volatile uint64_t val __used;
	int i;

	for (i = 0; i < TEST_NPAGES; i++) {
		uint64_t *src = (uint64_t *)(base_gpa + i * PAGE_SIZE);

		val = *src;
	}

	GUEST_DONE();
}

struct slot_worker_data {
	struct kvm_vm *vm;
	u64 gpa;
	uint32_t flags;
	bool worker_ready;
	bool prefault_ready;
	bool recreate_slot;
};

static void *delete_slot_worker(void *__data)
{
	struct slot_worker_data *data = __data;
	struct kvm_vm *vm = data->vm;

	WRITE_ONCE(data->worker_ready, true);

	while (!READ_ONCE(data->prefault_ready))
		cpu_relax();

	vm_mem_region_delete(vm, TEST_SLOT);

	while (!READ_ONCE(data->recreate_slot))
		cpu_relax();

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, data->gpa,
				    TEST_SLOT, TEST_NPAGES, data->flags);

	return NULL;
}

static void pre_fault_memory(struct kvm_vcpu *vcpu, u64 base_gpa, u64 offset,
			     u64 size, u64 expected_left, bool private)
{
	struct kvm_pre_fault_memory range = {
		.gpa = base_gpa + offset,
		.size = size,
		.flags = 0,
	};
	struct slot_worker_data data = {
		.vm = vcpu->vm,
		.gpa = base_gpa,
		.flags = private ? KVM_MEM_GUEST_MEMFD : 0,
	};
	bool slot_recreated = false;
	pthread_t slot_worker;
	int ret, save_errno;
	u64 prev;

	/*
	 * Concurrently delete (and recreate) the slot to test KVM's handling
	 * of a racing memslot deletion with prefaulting.
	 */
	pthread_create(&slot_worker, NULL, delete_slot_worker, &data);

	while (!READ_ONCE(data.worker_ready))
		cpu_relax();

	WRITE_ONCE(data.prefault_ready, true);

	for (;;) {
		prev = range.size;
		ret = __vcpu_ioctl(vcpu, KVM_PRE_FAULT_MEMORY, &range);
		save_errno = errno;
		TEST_ASSERT((range.size < prev) ^ (ret < 0),
			    "%sexpecting range.size to change on %s",
			    ret < 0 ? "not " : "",
			    ret < 0 ? "failure" : "success");

		/*
		 * Immediately retry prefaulting if KVM was interrupted by an
		 * unrelated signal/event.
		 */
		if (ret < 0 && save_errno == EINTR)
			continue;

		/*
		 * Tell the worker to recreate the slot in order to complete
		 * prefaulting (if prefault didn't already succeed before the
		 * slot was deleted) and/or to prepare for the next testcase.
		 * Wait for the worker to exit so that the next invocation of
		 * prefaulting is guaranteed to complete (assuming no KVM bugs).
		 */
		if (!slot_recreated) {
			WRITE_ONCE(data.recreate_slot, true);
			pthread_join(slot_worker, NULL);
			slot_recreated = true;

			/*
			 * Retry prefaulting to get a stable result, i.e. to
			 * avoid seeing random EAGAIN failures.  Don't retry if
			 * prefaulting already succeeded, as KVM disallows
			 * prefaulting with size=0, i.e. blindly retrying would
			 * result in test failures due to EINVAL.  KVM should
			 * always return success if all bytes are prefaulted,
			 * i.e. there is no need to guard against EAGAIN being
			 * returned.
			 */
			if (range.size)
				continue;
		}

		/*
		 * All done if there are no remaining bytes to prefault, or if
		 * prefaulting failed (EINTR was handled above, and EAGAIN due
		 * to prefaulting a memslot that's being actively deleted should
		 * be impossible since the memslot has already been recreated).
		 */
		if (!range.size || ret < 0)
			break;
	}

	TEST_ASSERT(range.size == expected_left,
		    "Completed with %llu bytes left, expected %lu",
		    range.size, expected_left);

	/*
	 * Assert success if prefaulting the entire range should succeed, i.e.
	 * complete with no bytes remaining.  Otherwise prefaulting should have
	 * failed due to ENOENT (due to RET_PF_EMULATE for emulated MMIO when
	 * no memslot exists).
	 */
	if (!expected_left)
		TEST_ASSERT_VM_VCPU_IOCTL(!ret, KVM_PRE_FAULT_MEMORY, ret, vcpu->vm);
	else
		TEST_ASSERT_VM_VCPU_IOCTL(ret && save_errno == ENOENT,
					  KVM_PRE_FAULT_MEMORY, ret, vcpu->vm);
}

static void __test_pre_fault_memory(unsigned long vm_type, bool private)
{
	const struct vm_shape shape = {
		.mode = VM_MODE_DEFAULT,
		.type = vm_type,
	};
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;

	uint64_t guest_test_phys_mem;
	uint64_t guest_test_virt_mem;
	uint64_t alignment, guest_page_size;

	vm = vm_create_shape_with_one_vcpu(shape, &vcpu, guest_code);

	alignment = guest_page_size = vm_guest_mode_params[VM_MODE_DEFAULT].page_size;
	guest_test_phys_mem = (vm->max_gfn - TEST_NPAGES) * guest_page_size;
#ifdef __s390x__
	alignment = max(0x100000UL, guest_page_size);
#else
	alignment = SZ_2M;
#endif
	guest_test_phys_mem = align_down(guest_test_phys_mem, alignment);
	guest_test_virt_mem = guest_test_phys_mem & ((1ULL << (vm->va_bits - 1)) - 1);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    guest_test_phys_mem, TEST_SLOT, TEST_NPAGES,
				    private ? KVM_MEM_GUEST_MEMFD : 0);
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem, TEST_NPAGES);

	if (private)
		vm_mem_set_private(vm, guest_test_phys_mem, TEST_SIZE);

	pre_fault_memory(vcpu, guest_test_phys_mem, 0, SZ_2M, 0, private);
	pre_fault_memory(vcpu, guest_test_phys_mem, SZ_2M, PAGE_SIZE * 2, PAGE_SIZE, private);
	pre_fault_memory(vcpu, guest_test_phys_mem, TEST_SIZE, PAGE_SIZE, PAGE_SIZE, private);

	vcpu_args_set(vcpu, 1, guest_test_virt_mem);
	vcpu_run(vcpu);

	run = vcpu->run;
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Wanted KVM_EXIT_IO, got exit reason: %u (%s)",
		    run->exit_reason, exit_reason_str(run->exit_reason));

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
		break;
	}

	kvm_vm_free(vm);
}

static void test_pre_fault_memory(unsigned long vm_type, bool private)
{
	if (vm_type && !(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(vm_type))) {
		pr_info("Skipping tests for vm_type 0x%lx\n", vm_type);
		return;
	}

	__test_pre_fault_memory(vm_type, private);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_PRE_FAULT_MEMORY));

	test_pre_fault_memory(0, false);
#ifdef __x86_64__
	test_pre_fault_memory(KVM_X86_SW_PROTECTED_VM, false);
	test_pre_fault_memory(KVM_X86_SW_PROTECTED_VM, true);
#endif
	return 0;
}
