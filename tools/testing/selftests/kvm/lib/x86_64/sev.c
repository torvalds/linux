// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE /* for program_invocation_short_name */
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
static void encrypt_region(struct kvm_vm *vm, struct userspace_mem_region *region)
{
	const struct sparsebit *protected_phy_pages = region->protected_phy_pages;
	const vm_paddr_t gpa_base = region->region.guest_phys_addr;
	const sparsebit_idx_t lowest_page_in_region = gpa_base >> vm->page_shift;
	sparsebit_idx_t i, j;

	if (!sparsebit_any_set(protected_phy_pages))
		return;

	sev_register_encrypted_memory(vm, region);

	sparsebit_for_each_set_range(protected_phy_pages, i, j) {
		const uint64_t size = (j - i + 1) * vm->page_size;
		const uint64_t offset = (i - lowest_page_in_region) * vm->page_size;

		sev_launch_update_data(vm, gpa_base + offset, size);
	}
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
		encrypt_region(vm, region);

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

struct kvm_vm *vm_sev_create_with_one_vcpu(uint32_t policy, void *guest_code,
					   struct kvm_vcpu **cpu)
{
	struct vm_shape shape = {
		.type = VM_TYPE_DEFAULT,
		.mode = VM_MODE_DEFAULT,
		.subtype = policy & SEV_POLICY_ES ? VM_SUBTYPE_SEV_ES :
						    VM_SUBTYPE_SEV,
	};
	struct kvm_vm *vm;
	struct kvm_vcpu *cpus[1];
	uint8_t measurement[512];

	vm = __vm_create_with_vcpus(shape, 1, 0, guest_code, cpus);
	*cpu = cpus[0];

	sev_vm_launch(vm, policy);

	/* TODO: Validate the measurement is as expected. */
	sev_vm_launch_measure(vm, measurement);

	sev_vm_launch_finish(vm);

	return vm;
}
