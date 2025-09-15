// SPDX-License-Identifier: GPL-2.0-only
/*
 * external_abort - Tests for userspace external abort injection
 *
 * Copyright (c) 2024 Google LLC
 */
#include "processor.h"
#include "test_util.h"

#define MMIO_ADDR		0x8000000ULL
#define EXPECTED_SERROR_ISS	(ESR_ELx_ISV | 0x1d1ed)

static u64 expected_abort_pc;

static void expect_sea_handler(struct ex_regs *regs)
{
	u64 esr = read_sysreg(esr_el1);

	GUEST_ASSERT_EQ(regs->pc, expected_abort_pc);
	GUEST_ASSERT_EQ(ESR_ELx_EC(esr), ESR_ELx_EC_DABT_CUR);
	GUEST_ASSERT_EQ(esr & ESR_ELx_FSC_TYPE, ESR_ELx_FSC_EXTABT);

	GUEST_DONE();
}

static void unexpected_dabt_handler(struct ex_regs *regs)
{
	GUEST_FAIL("Unexpected data abort at PC: %lx\n", regs->pc);
}

static struct kvm_vm *vm_create_with_dabt_handler(struct kvm_vcpu **vcpu, void *guest_code,
						  handler_fn dabt_handler)
{
	struct kvm_vm *vm = vm_create_with_one_vcpu(vcpu, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(*vcpu);
	vm_install_sync_handler(vm, VECTOR_SYNC_CURRENT, ESR_ELx_EC_DABT_CUR, dabt_handler);

	virt_map(vm, MMIO_ADDR, MMIO_ADDR, 1);

	return vm;
}

static void vcpu_inject_sea(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_events events = {};

	events.exception.ext_dabt_pending = true;
	vcpu_events_set(vcpu, &events);
}

static bool vcpu_has_ras(struct kvm_vcpu *vcpu)
{
	u64 pfr0 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1));

	return SYS_FIELD_GET(ID_AA64PFR0_EL1, RAS, pfr0);
}

static bool guest_has_ras(void)
{
	return SYS_FIELD_GET(ID_AA64PFR0_EL1, RAS, read_sysreg(id_aa64pfr0_el1));
}

static void vcpu_inject_serror(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_events events = {};

	events.exception.serror_pending = true;
	if (vcpu_has_ras(vcpu)) {
		events.exception.serror_has_esr = true;
		events.exception.serror_esr = EXPECTED_SERROR_ISS;
	}

	vcpu_events_set(vcpu, &events);
}

static void __vcpu_run_expect(struct kvm_vcpu *vcpu, unsigned int cmd)
{
	struct ucall uc;

	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	default:
		if (uc.cmd == cmd)
			return;

		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}
}

static void vcpu_run_expect_done(struct kvm_vcpu *vcpu)
{
	__vcpu_run_expect(vcpu, UCALL_DONE);
}

static void vcpu_run_expect_sync(struct kvm_vcpu *vcpu)
{
	__vcpu_run_expect(vcpu, UCALL_SYNC);
}

extern char test_mmio_abort_insn;

static noinline void test_mmio_abort_guest(void)
{
	WRITE_ONCE(expected_abort_pc, (u64)&test_mmio_abort_insn);

	asm volatile("test_mmio_abort_insn:\n\t"
		     "ldr x0, [%0]\n\t"
		     : : "r" (MMIO_ADDR) : "x0", "memory");

	GUEST_FAIL("MMIO instruction should not retire");
}

/*
 * Test that KVM doesn't complete MMIO emulation when userspace has made an
 * external abort pending for the instruction.
 */
static void test_mmio_abort(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_mmio_abort_guest,
							expect_sea_handler);
	struct kvm_run *run = vcpu->run;

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_MMIO);
	TEST_ASSERT_EQ(run->mmio.phys_addr, MMIO_ADDR);
	TEST_ASSERT_EQ(run->mmio.len, sizeof(unsigned long));
	TEST_ASSERT(!run->mmio.is_write, "Expected MMIO read");

	vcpu_inject_sea(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

extern char test_mmio_nisv_insn;

static void test_mmio_nisv_guest(void)
{
	WRITE_ONCE(expected_abort_pc, (u64)&test_mmio_nisv_insn);

	asm volatile("test_mmio_nisv_insn:\n\t"
		     "ldr x0, [%0], #8\n\t"
		     : : "r" (MMIO_ADDR) : "x0", "memory");

	GUEST_FAIL("MMIO instruction should not retire");
}

/*
 * Test that the KVM_RUN ioctl fails for ESR_EL2.ISV=0 MMIO aborts if userspace
 * hasn't enabled KVM_CAP_ARM_NISV_TO_USER.
 */
static void test_mmio_nisv(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_mmio_nisv_guest,
							unexpected_dabt_handler);

	TEST_ASSERT(_vcpu_run(vcpu), "Expected nonzero return code from KVM_RUN");
	TEST_ASSERT_EQ(errno, ENOSYS);

	kvm_vm_free(vm);
}

/*
 * Test that ESR_EL2.ISV=0 MMIO aborts reach userspace and that an injected SEA
 * reaches the guest.
 */
static void test_mmio_nisv_abort(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_mmio_nisv_guest,
							expect_sea_handler);
	struct kvm_run *run = vcpu->run;

	vm_enable_cap(vm, KVM_CAP_ARM_NISV_TO_USER, 1);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_ARM_NISV);
	TEST_ASSERT_EQ(run->arm_nisv.fault_ipa, MMIO_ADDR);

	vcpu_inject_sea(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

static void unexpected_serror_handler(struct ex_regs *regs)
{
	GUEST_FAIL("Took unexpected SError exception");
}

static void test_serror_masked_guest(void)
{
	GUEST_ASSERT(read_sysreg(isr_el1) & ISR_EL1_A);

	isb();

	GUEST_DONE();
}

static void test_serror_masked(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_serror_masked_guest,
							unexpected_dabt_handler);

	vm_install_exception_handler(vm, VECTOR_ERROR_CURRENT, unexpected_serror_handler);

	vcpu_inject_serror(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

static void expect_serror_handler(struct ex_regs *regs)
{
	u64 esr = read_sysreg(esr_el1);

	GUEST_ASSERT_EQ(ESR_ELx_EC(esr), ESR_ELx_EC_SERROR);
	if (guest_has_ras())
		GUEST_ASSERT_EQ(ESR_ELx_ISS(esr), EXPECTED_SERROR_ISS);

	GUEST_DONE();
}

static void test_serror_guest(void)
{
	GUEST_ASSERT(read_sysreg(isr_el1) & ISR_EL1_A);

	local_serror_enable();
	isb();
	local_serror_disable();

	GUEST_FAIL("Should've taken pending SError exception");
}

static void test_serror(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_serror_guest,
							unexpected_dabt_handler);

	vm_install_exception_handler(vm, VECTOR_ERROR_CURRENT, expect_serror_handler);

	vcpu_inject_serror(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

static void test_serror_emulated_guest(void)
{
	GUEST_ASSERT(!(read_sysreg(isr_el1) & ISR_EL1_A));

	local_serror_enable();
	GUEST_SYNC(0);
	local_serror_disable();

	GUEST_FAIL("Should've taken unmasked SError exception");
}

static void test_serror_emulated(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_serror_emulated_guest,
							unexpected_dabt_handler);

	vm_install_exception_handler(vm, VECTOR_ERROR_CURRENT, expect_serror_handler);

	vcpu_run_expect_sync(vcpu);
	vcpu_inject_serror(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

static void test_mmio_ease_guest(void)
{
	sysreg_clear_set_s(SYS_SCTLR2_EL1, 0, SCTLR2_EL1_EASE);
	isb();

	test_mmio_abort_guest();
}

/*
 * Test that KVM doesn't complete MMIO emulation when userspace has made an
 * external abort pending for the instruction.
 */
static void test_mmio_ease(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_dabt_handler(&vcpu, test_mmio_ease_guest,
							unexpected_dabt_handler);
	struct kvm_run *run = vcpu->run;
	u64 pfr1;

	pfr1 = vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR1_EL1));
	if (!SYS_FIELD_GET(ID_AA64PFR1_EL1, DF2, pfr1)) {
		pr_debug("Skipping %s\n", __func__);
		return;
	}

	/*
	 * SCTLR2_ELx.EASE changes the exception vector to the SError vector but
	 * doesn't further modify the exception context (e.g. ESR_ELx, FAR_ELx).
	 */
	vm_install_exception_handler(vm, VECTOR_ERROR_CURRENT, expect_sea_handler);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_MMIO);
	TEST_ASSERT_EQ(run->mmio.phys_addr, MMIO_ADDR);
	TEST_ASSERT_EQ(run->mmio.len, sizeof(unsigned long));
	TEST_ASSERT(!run->mmio.is_write, "Expected MMIO read");

	vcpu_inject_sea(vcpu);
	vcpu_run_expect_done(vcpu);
	kvm_vm_free(vm);
}

int main(void)
{
	test_mmio_abort();
	test_mmio_nisv();
	test_mmio_nisv_abort();
	test_serror();
	test_serror_masked();
	test_serror_emulated();
	test_mmio_ease();
}
