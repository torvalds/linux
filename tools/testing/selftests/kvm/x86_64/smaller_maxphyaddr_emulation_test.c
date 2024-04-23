// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Test that KVM emulates instructions in response to EPT violations when
 * allow_smaller_maxphyaddr is enabled and guest.MAXPHYADDR < host.MAXPHYADDR.
 */
#include "flds_emulation.h"

#include "test_util.h"
#include "kvm_util.h"
#include "vmx.h"

#define MAXPHYADDR 36

#define MEM_REGION_GVA	0x0000123456789000
#define MEM_REGION_GPA	0x0000000700000000
#define MEM_REGION_SLOT	10
#define MEM_REGION_SIZE PAGE_SIZE

static void guest_code(bool tdp_enabled)
{
	uint64_t error_code;
	uint64_t vector;

	vector = kvm_asm_safe_ec(FLDS_MEM_EAX, error_code, "a"(MEM_REGION_GVA));

	/*
	 * When TDP is enabled, flds will trigger an emulation failure, exit to
	 * userspace, and then the selftest host "VMM" skips the instruction.
	 *
	 * When TDP is disabled, no instruction emulation is required so flds
	 * should generate #PF(RSVD).
	 */
	if (tdp_enabled) {
		GUEST_ASSERT(!vector);
	} else {
		GUEST_ASSERT_EQ(vector, PF_VECTOR);
		GUEST_ASSERT(error_code & PFERR_RSVD_MASK);
	}

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	uint64_t *pte;
	uint64_t *hva;
	uint64_t gpa;
	int rc;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SMALLER_MAXPHYADDR));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vcpu_args_set(vcpu, 1, kvm_is_tdp_enabled());

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_MAX_PHY_ADDR, MAXPHYADDR);

	rc = kvm_check_cap(KVM_CAP_EXIT_ON_EMULATION_FAILURE);
	TEST_ASSERT(rc, "KVM_CAP_EXIT_ON_EMULATION_FAILURE is unavailable");
	vm_enable_cap(vm, KVM_CAP_EXIT_ON_EMULATION_FAILURE, 1);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    MEM_REGION_GPA, MEM_REGION_SLOT,
				    MEM_REGION_SIZE / PAGE_SIZE, 0);
	gpa = vm_phy_pages_alloc(vm, MEM_REGION_SIZE / PAGE_SIZE,
				 MEM_REGION_GPA, MEM_REGION_SLOT);
	TEST_ASSERT(gpa == MEM_REGION_GPA, "Failed vm_phy_pages_alloc");
	virt_map(vm, MEM_REGION_GVA, MEM_REGION_GPA, 1);
	hva = addr_gpa2hva(vm, MEM_REGION_GPA);
	memset(hva, 0, PAGE_SIZE);

	pte = vm_get_page_table_entry(vm, MEM_REGION_GVA);
	*pte |= BIT_ULL(MAXPHYADDR);

	vcpu_run(vcpu);

	/*
	 * When TDP is enabled, KVM must emulate in response the guest physical
	 * address that is illegal from the guest's perspective, but is legal
	 * from hardware's perspeective.  This should result in an emulation
	 * failure exit to userspace since KVM doesn't support emulating flds.
	 */
	if (kvm_is_tdp_enabled()) {
		handle_flds_emulation_failure_exit(vcpu);
		vcpu_run(vcpu);
	}

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unrecognized ucall: %lu", uc.cmd);
	}

	kvm_vm_free(vm);

	return 0;
}
