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

static void pre_fault_memory(struct kvm_vcpu *vcpu, u64 gpa, u64 size,
			     u64 left)
{
	struct kvm_pre_fault_memory range = {
		.gpa = gpa,
		.size = size,
		.flags = 0,
	};
	u64 prev;
	int ret, save_errno;

	do {
		prev = range.size;
		ret = __vcpu_ioctl(vcpu, KVM_PRE_FAULT_MEMORY, &range);
		save_errno = errno;
		TEST_ASSERT((range.size < prev) ^ (ret < 0),
			    "%sexpecting range.size to change on %s",
			    ret < 0 ? "not " : "",
			    ret < 0 ? "failure" : "success");
	} while (ret >= 0 ? range.size : save_errno == EINTR);

	TEST_ASSERT(range.size == left,
		    "Completed with %lld bytes left, expected %" PRId64,
		    range.size, left);

	if (left == 0)
		__TEST_ASSERT_VM_VCPU_IOCTL(!ret, "KVM_PRE_FAULT_MEMORY", ret, vcpu->vm);
	else
		/* No memory slot causes RET_PF_EMULATE. it results in -ENOENT. */
		__TEST_ASSERT_VM_VCPU_IOCTL(ret && save_errno == ENOENT,
					    "KVM_PRE_FAULT_MEMORY", ret, vcpu->vm);
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
	pre_fault_memory(vcpu, guest_test_phys_mem, SZ_2M, 0);
	pre_fault_memory(vcpu, guest_test_phys_mem + SZ_2M, PAGE_SIZE * 2, PAGE_SIZE);
	pre_fault_memory(vcpu, guest_test_phys_mem + TEST_SIZE, PAGE_SIZE, PAGE_SIZE);

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
