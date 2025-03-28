// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include "kvm_util.h"

vm_vaddr_t *ucall_exit_mmio_addr;

void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
	vm_vaddr_t mmio_gva = vm_vaddr_unused_gap(vm, vm->page_size, KVM_UTIL_MIN_VADDR);

	virt_map(vm, mmio_gva, mmio_gpa, 1);

	vm->ucall_mmio_addr = mmio_gpa;

	write_guest_global(vm, ucall_exit_mmio_addr, (vm_vaddr_t *)mmio_gva);
}

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == KVM_EXIT_MMIO &&
	    run->mmio.phys_addr == vcpu->vm->ucall_mmio_addr) {
		TEST_ASSERT(run->mmio.is_write && run->mmio.len == sizeof(uint64_t),
			    "Unexpected ucall exit mmio address access");
		return (void *)(*((uint64_t *)run->mmio.data));
	}

	return NULL;
}
