// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hyper-V specific functions.
 *
 * Copyright (C) 2021, Red Hat Inc.
 */
#include <stdint.h>
#include "processor.h"
#include "hyperv.h"

struct hyperv_test_pages *vcpu_alloc_hyperv_test_pages(struct kvm_vm *vm,
						       vm_vaddr_t *p_hv_pages_gva)
{
	vm_vaddr_t hv_pages_gva = vm_vaddr_alloc_page(vm);
	struct hyperv_test_pages *hv = addr_gva2hva(vm, hv_pages_gva);

	/* Setup of a region of guest memory for the VP Assist page. */
	hv->vp_assist = (void *)vm_vaddr_alloc_page(vm);
	hv->vp_assist_hva = addr_gva2hva(vm, (uintptr_t)hv->vp_assist);
	hv->vp_assist_gpa = addr_gva2gpa(vm, (uintptr_t)hv->vp_assist);

	/* Setup of a region of guest memory for the partition assist page. */
	hv->partition_assist = (void *)vm_vaddr_alloc_page(vm);
	hv->partition_assist_hva = addr_gva2hva(vm, (uintptr_t)hv->partition_assist);
	hv->partition_assist_gpa = addr_gva2gpa(vm, (uintptr_t)hv->partition_assist);

	/* Setup of a region of guest memory for the enlightened VMCS. */
	hv->enlightened_vmcs = (void *)vm_vaddr_alloc_page(vm);
	hv->enlightened_vmcs_hva = addr_gva2hva(vm, (uintptr_t)hv->enlightened_vmcs);
	hv->enlightened_vmcs_gpa = addr_gva2gpa(vm, (uintptr_t)hv->enlightened_vmcs);

	*p_hv_pages_gva = hv_pages_gva;
	return hv;
}

int enable_vp_assist(uint64_t vp_assist_pa, void *vp_assist)
{
	uint64_t val = (vp_assist_pa & HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_MASK) |
		HV_X64_MSR_VP_ASSIST_PAGE_ENABLE;

	wrmsr(HV_X64_MSR_VP_ASSIST_PAGE, val);

	current_vp_assist = vp_assist;

	return 0;
}
