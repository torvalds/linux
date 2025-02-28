// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hyper-V specific functions.
 *
 * Copyright (C) 2021, Red Hat Inc.
 */
#include <stdint.h>
#include "processor.h"
#include "hyperv.h"

const struct kvm_cpuid2 *kvm_get_supported_hv_cpuid(void)
{
	static struct kvm_cpuid2 *cpuid;
	int kvm_fd;

	if (cpuid)
		return cpuid;

	cpuid = allocate_kvm_cpuid2(MAX_NR_CPUID_ENTRIES);
	kvm_fd = open_kvm_dev_path_or_exit();

	kvm_ioctl(kvm_fd, KVM_GET_SUPPORTED_HV_CPUID, cpuid);

	close(kvm_fd);
	return cpuid;
}

void vcpu_set_hv_cpuid(struct kvm_vcpu *vcpu)
{
	static struct kvm_cpuid2 *cpuid_full;
	const struct kvm_cpuid2 *cpuid_sys, *cpuid_hv;
	int i, nent = 0;

	if (!cpuid_full) {
		cpuid_sys = kvm_get_supported_cpuid();
		cpuid_hv = kvm_get_supported_hv_cpuid();

		cpuid_full = allocate_kvm_cpuid2(cpuid_sys->nent + cpuid_hv->nent);
		if (!cpuid_full) {
			perror("malloc");
			abort();
		}

		/* Need to skip KVM CPUID leaves 0x400000xx */
		for (i = 0; i < cpuid_sys->nent; i++) {
			if (cpuid_sys->entries[i].function >= 0x40000000 &&
			    cpuid_sys->entries[i].function < 0x40000100)
				continue;
			cpuid_full->entries[nent] = cpuid_sys->entries[i];
			nent++;
		}

		memcpy(&cpuid_full->entries[nent], cpuid_hv->entries,
		       cpuid_hv->nent * sizeof(struct kvm_cpuid_entry2));
		cpuid_full->nent = nent + cpuid_hv->nent;
	}

	vcpu_init_cpuid(vcpu, cpuid_full);
}

const struct kvm_cpuid2 *vcpu_get_supported_hv_cpuid(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid2 *cpuid = allocate_kvm_cpuid2(MAX_NR_CPUID_ENTRIES);

	vcpu_ioctl(vcpu, KVM_GET_SUPPORTED_HV_CPUID, cpuid);

	return cpuid;
}

bool kvm_hv_cpu_has(struct kvm_x86_cpu_feature feature)
{
	if (!kvm_has_cap(KVM_CAP_SYS_HYPERV_CPUID))
		return false;

	return kvm_cpuid_has(kvm_get_supported_hv_cpuid(), feature);
}

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
