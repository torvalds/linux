// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include "kvm_util.h"

static vm_vaddr_t *ucall_exit_mmio_addr;

void ucall_arch_init(struct kvm_vm *vm, vm_paddr_t mmio_gpa)
{
	virt_pg_map(vm, mmio_gpa, mmio_gpa);

	ucall_exit_mmio_addr = (vm_vaddr_t *)mmio_gpa;
	sync_global_to_guest(vm, ucall_exit_mmio_addr);
}

void ucall_arch_uninit(struct kvm_vm *vm)
{
	ucall_exit_mmio_addr = 0;
	sync_global_to_guest(vm, ucall_exit_mmio_addr);
}

void ucall_arch_do_ucall(vm_vaddr_t uc)
{
	WRITE_ONCE(*ucall_exit_mmio_addr, uc);
}

void *ucall_arch_get_ucall(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;

	if (run->exit_reason == KVM_EXIT_MMIO &&
	    run->mmio.phys_addr == (uint64_t)ucall_exit_mmio_addr) {
		vm_vaddr_t gva;

		TEST_ASSERT(run->mmio.is_write && run->mmio.len == 8,
			    "Unexpected ucall exit mmio address access");
		memcpy(&gva, run->mmio.data, sizeof(gva));
		return addr_gva2hva(vcpu->vm, gva);
	}

	return NULL;
}
