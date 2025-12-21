// SPDX-License-Identifier: GPL-2.0-only
/*
 * at - Test for KVM's AT emulation in the EL2&0 and EL1&0 translation regimes.
 */
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include "ucall.h"

#include <asm/sysreg.h>

#define TEST_ADDR	0x80000000

enum {
	CLEAR_ACCESS_FLAG,
	TEST_ACCESS_FLAG,
};

static u64 *ptep_hva;

#define copy_el2_to_el1(reg)						\
	write_sysreg_s(read_sysreg_s(SYS_##reg##_EL1), SYS_##reg##_EL12)

/* Yes, this is an ugly hack */
#define __at(op, addr)	write_sysreg_s(addr, op)

#define test_at_insn(op, expect_fault)							\
do {											\
	u64 par, fsc;									\
	bool fault;									\
											\
	GUEST_SYNC(CLEAR_ACCESS_FLAG);							\
											\
	__at(OP_AT_##op, TEST_ADDR);							\
	isb();										\
	par = read_sysreg(par_el1);							\
											\
	fault = par & SYS_PAR_EL1_F;							\
	fsc = FIELD_GET(SYS_PAR_EL1_FST, par);						\
											\
	__GUEST_ASSERT((expect_fault) == fault,						\
		       "AT "#op": %sexpected fault (par: %lx)1",			\
		       (expect_fault) ? "" : "un", par);				\
	if ((expect_fault)) {								\
		__GUEST_ASSERT(fsc == ESR_ELx_FSC_ACCESS_L(3),				\
			       "AT "#op": expected access flag fault (par: %lx)",	\
			       par);							\
	} else {									\
		GUEST_ASSERT_EQ(FIELD_GET(SYS_PAR_EL1_ATTR, par), MAIR_ATTR_NORMAL);	\
		GUEST_ASSERT_EQ(FIELD_GET(SYS_PAR_EL1_SH, par), PTE_SHARED >> 8);	\
		GUEST_ASSERT_EQ(par & SYS_PAR_EL1_PA, TEST_ADDR);			\
		GUEST_SYNC(TEST_ACCESS_FLAG);						\
	}										\
} while (0)

static void test_at(bool expect_fault)
{
	test_at_insn(S1E2R, expect_fault);
	test_at_insn(S1E2W, expect_fault);

	/* Reuse the stage-1 MMU context from EL2 at EL1 */
	copy_el2_to_el1(SCTLR);
	copy_el2_to_el1(MAIR);
	copy_el2_to_el1(TCR);
	copy_el2_to_el1(TTBR0);
	copy_el2_to_el1(TTBR1);

	/* Disable stage-2 translation and enter a non-host context */
	write_sysreg(0, vtcr_el2);
	write_sysreg(0, vttbr_el2);
	sysreg_clear_set(hcr_el2, HCR_EL2_TGE | HCR_EL2_VM, 0);
	isb();

	test_at_insn(S1E1R, expect_fault);
	test_at_insn(S1E1W, expect_fault);
}

static void guest_code(void)
{
	sysreg_clear_set(tcr_el1, TCR_HA, 0);
	isb();

	test_at(true);

	if (!SYS_FIELD_GET(ID_AA64MMFR1_EL1, HAFDBS, read_sysreg(id_aa64mmfr1_el1)))
		GUEST_DONE();

	/*
	 * KVM's software PTW makes the implementation choice that the AT
	 * instruction sets the access flag.
	 */
	sysreg_clear_set(tcr_el1, 0, TCR_HA);
	isb();
	test_at(false);

	GUEST_DONE();
}

static void handle_sync(struct kvm_vcpu *vcpu, struct ucall *uc)
{
	switch (uc->args[1]) {
	case CLEAR_ACCESS_FLAG:
		/*
		 * Delete + reinstall the memslot to invalidate stage-2
		 * mappings of the stage-1 page tables, forcing KVM to
		 * use the 'slow' AT emulation path.
		 *
		 * This and clearing the access flag from host userspace
		 * ensures that the access flag cannot be set speculatively
		 * and is reliably cleared at the time of the AT instruction.
		 */
		clear_bit(__ffs(PTE_AF), ptep_hva);
		vm_mem_region_reload(vcpu->vm, vcpu->vm->memslots[MEM_REGION_PT]);
		break;
	case TEST_ACCESS_FLAG:
		TEST_ASSERT(test_bit(__ffs(PTE_AF), ptep_hva),
			    "Expected access flag to be set (desc: %lu)", *ptep_hva);
		break;
	default:
		TEST_FAIL("Unexpected SYNC arg: %lu", uc->args[1]);
	}
}

static void run_test(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	while (true) {
		vcpu_run(vcpu);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_DONE:
			return;
		case UCALL_SYNC:
			handle_sync(vcpu, &uc);
			continue;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			return;
		default:
			TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
		}
	}
}

int main(void)
{
	struct kvm_vcpu_init init;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_check_cap(KVM_CAP_ARM_EL2));

	vm = vm_create(1);

	kvm_get_default_vcpu_target(vm, &init);
	init.features[0] |= BIT(KVM_ARM_VCPU_HAS_EL2);
	vcpu = aarch64_vcpu_add(vm, 0, &init, guest_code);
	kvm_arch_vm_finalize_vcpus(vm);

	virt_map(vm, TEST_ADDR, TEST_ADDR, 1);
	ptep_hva = virt_get_pte_hva_at_level(vm, TEST_ADDR, 3);
	run_test(vcpu);

	kvm_vm_free(vm);
	return 0;
}
