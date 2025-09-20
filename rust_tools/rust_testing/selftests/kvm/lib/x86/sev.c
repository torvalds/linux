// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdbool.h>

#include "sev.h"

/*
 * sparsebit_next_clear() can return 0 if [x, 2**64-1] are all set, and the
 * -1 would then cause an underflow back to 2**64 - 1. This is expected and
 * correct.
 *
 * If the last range in the sparsebit is [x, y] and we try to iterate,
 * sparsebit_next_set() will return 0, and sparsebit_next_clear() will try
 * and find the first range, but that's correct because the condition
 * expression would cause us to quit the loop.
 */
static void encrypt_region(struct kvm_vm *vm, struct userspace_mem_region *region,
			   uint8_t page_type, bool private)
{
	const struct sparsebit *protected_phy_pages = region->protected_phy_pages;
	const vm_paddr_t gpa_base = region->region.guest_phys_addr;
	const sparsebit_idx_t lowest_page_in_region = gpa_base >> vm->page_shift;
	sparsebit_idx_t i, j;

	if (!sparsebit_any_set(protected_phy_pages))
		return;

	if (!is_sev_snp_vm(vm))
		sev_register_encrypted_memory(vm, region);

	sparsebit_for_each_set_range(protected_phy_pages, i, j) {
		const uint64_t size = (j - i + 1) * vm->page_size;
		const uint64_t offset = (i - lowest_page_in_region) * vm->page_size;

		if (private)
			vm_mem_set_private(vm, gpa_base + offset, size);

		if (is_sev_snp_vm(vm))
			snp_launch_update_data(vm, gpa_base + offset,
					       (uint64_t)addr_gpa2hva(vm, gpa_base + offset),
					       size, page_type);
		else
			sev_launch_update_data(vm, gpa_base + offset, size);

	}
}

void sev_vm_init(struct kvm_vm *vm)
{
	if (vm->type == KVM_X86_DEFAULT_VM) {
		TEST_ASSERT_EQ(vm->arch.sev_fd, -1);
		vm->arch.sev_fd = open_sev_dev_path_or_exit();
		vm_sev_ioctl(vm, KVM_SEV_INIT, NULL);
	} else {
		struct kvm_sev_init init = { 0 };
		TEST_ASSERT_EQ(vm->type, KVM_X86_SEV_VM);
		vm_sev_ioctl(vm, KVM_SEV_INIT2, &init);
	}
}

void sev_es_vm_init(struct kvm_vm *vm)
{
	if (vm->type == KVM_X86_DEFAULT_VM) {
		TEST_ASSERT_EQ(vm->arch.sev_fd, -1);
		vm->arch.sev_fd = open_sev_dev_path_or_exit();
		vm_sev_ioctl(vm, KVM_SEV_ES_INIT, NULL);
	} else {
		struct kvm_sev_init init = { 0 };
		TEST_ASSERT_EQ(vm->type, KVM_X86_SEV_ES_VM);
		vm_sev_ioctl(vm, KVM_SEV_INIT2, &init);
	}
}

void snp_vm_init(struct kvm_vm *vm)
{
	struct kvm_sev_init init = { 0 };

	TEST_ASSERT_EQ(vm->type, KVM_X86_SNP_VM);
	vm_sev_ioctl(vm, KVM_SEV_INIT2, &init);
}

void sev_vm_launch(struct kvm_vm *vm, uint32_t policy)
{
	struct kvm_sev_launch_start launch_start = {
		.policy = policy,
	};
	struct userspace_mem_region *region;
	struct kvm_sev_guest_status status;
	int ctr;

	vm_sev_ioctl(vm, KVM_SEV_LAUNCH_START, &launch_start);
	vm_sev_ioctl(vm, KVM_SEV_GUEST_STATUS, &status);

	TEST_ASSERT_EQ(status.policy, policy);
	TEST_ASSERT_EQ(status.state, SEV_GUEST_STATE_LAUNCH_UPDATE);

	hash_for_each(vm->regions.slot_hash, ctr, region, slot_node)
		encrypt_region(vm, region, KVM_SEV_PAGE_TYPE_INVALID, false);

	if (policy & SEV_POLICY_ES)
		vm_sev_ioctl(vm, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL);

	vm->arch.is_pt_protected = true;
}

void sev_vm_launch_measure(struct kvm_vm *vm, uint8_t *measurement)
{
	struct kvm_sev_launch_measure launch_measure;
	struct kvm_sev_guest_status guest_status;

	launch_measure.len = 256;
	launch_measure.uaddr = (__u64)measurement;
	vm_sev_ioctl(vm, KVM_SEV_LAUNCH_MEASURE, &launch_measure);

	vm_sev_ioctl(vm, KVM_SEV_GUEST_STATUS, &guest_status);
	TEST_ASSERT_EQ(guest_status.state, SEV_GUEST_STATE_LAUNCH_SECRET);
}

void sev_vm_launch_finish(struct kvm_vm *vm)
{
	struct kvm_sev_guest_status status;

	vm_sev_ioctl(vm, KVM_SEV_GUEST_STATUS, &status);
	TEST_ASSERT(status.state == SEV_GUEST_STATE_LAUNCH_UPDATE ||
		    status.state == SEV_GUEST_STATE_LAUNCH_SECRET,
		    "Unexpected guest state: %d", status.state);

	vm_sev_ioctl(vm, KVM_SEV_LAUNCH_FINISH, NULL);

	vm_sev_ioctl(vm, KVM_SEV_GUEST_STATUS, &status);
	TEST_ASSERT_EQ(status.state, SEV_GUEST_STATE_RUNNING);
}

void snp_vm_launch_start(struct kvm_vm *vm, uint64_t policy)
{
	struct kvm_sev_snp_launch_start launch_start = {
		.policy = policy,
	};

	vm_sev_ioctl(vm, KVM_SEV_SNP_LAUNCH_START, &launch_start);
}

void snp_vm_launch_update(struct kvm_vm *vm)
{
	struct userspace_mem_region *region;
	int ctr;

	hash_for_each(vm->regions.slot_hash, ctr, region, slot_node)
		encrypt_region(vm, region, KVM_SEV_SNP_PAGE_TYPE_NORMAL, true);

	vm->arch.is_pt_protected = true;
}

void snp_vm_launch_finish(struct kvm_vm *vm)
{
	struct kvm_sev_snp_launch_finish launch_finish = { 0 };

	vm_sev_ioctl(vm, KVM_SEV_SNP_LAUNCH_FINISH, &launch_finish);
}

struct kvm_vm *vm_sev_create_with_one_vcpu(uint32_t type, void *guest_code,
					   struct kvm_vcpu **cpu)
{
	struct vm_shape shape = {
		.mode = VM_MODE_DEFAULT,
		.type = type,
	};
	struct kvm_vm *vm;
	struct kvm_vcpu *cpus[1];

	vm = __vm_create_with_vcpus(shape, 1, 0, guest_code, cpus);
	*cpu = cpus[0];

	return vm;
}

void vm_sev_launch(struct kvm_vm *vm, uint64_t policy, uint8_t *measurement)
{
	if (is_sev_snp_vm(vm)) {
		vm_enable_cap(vm, KVM_CAP_EXIT_HYPERCALL, BIT(KVM_HC_MAP_GPA_RANGE));

		snp_vm_launch_start(vm, policy);

		snp_vm_launch_update(vm);

		snp_vm_launch_finish(vm);

		return;
	}

	sev_vm_launch(vm, policy);

	if (!measurement)
		measurement = alloca(256);

	sev_vm_launch_measure(vm, measurement);

	sev_vm_launch_finish(vm);
}
