// SPDX-License-Identifier: GPL-2.0-only
/*
 * vpmu_counter_access - Test vPMU event counter access
 *
 * Copyright (c) 2023 Google LLC.
 *
 * This test checks if the guest can see the same number of the PMU event
 * counters (PMCR_EL0.N) that userspace sets.
 * This test runs only when KVM_CAP_ARM_PMU_V3 is supported on the host.
 */
#include <kvm_util.h>
#include <processor.h>
#include <test_util.h>
#include <vgic.h>
#include <perf/arm_pmuv3.h>
#include <linux/bitfield.h>

/* The max number of the PMU event counters (excluding the cycle counter) */
#define ARMV8_PMU_MAX_GENERAL_COUNTERS	(ARMV8_PMU_MAX_COUNTERS - 1)

struct vpmu_vm {
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	int gic_fd;
};

static struct vpmu_vm vpmu_vm;

static uint64_t get_pmcr_n(uint64_t pmcr)
{
	return (pmcr >> ARMV8_PMU_PMCR_N_SHIFT) & ARMV8_PMU_PMCR_N_MASK;
}

static void set_pmcr_n(uint64_t *pmcr, uint64_t pmcr_n)
{
	*pmcr = *pmcr & ~(ARMV8_PMU_PMCR_N_MASK << ARMV8_PMU_PMCR_N_SHIFT);
	*pmcr |= (pmcr_n << ARMV8_PMU_PMCR_N_SHIFT);
}

static void guest_sync_handler(struct ex_regs *regs)
{
	uint64_t esr, ec;

	esr = read_sysreg(esr_el1);
	ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
	__GUEST_ASSERT(0, "PC: 0x%lx; ESR: 0x%lx; EC: 0x%lx", regs->pc, esr, ec);
}

/*
 * The guest is configured with PMUv3 with @expected_pmcr_n number of
 * event counters.
 * Check if @expected_pmcr_n is consistent with PMCR_EL0.N.
 */
static void guest_code(uint64_t expected_pmcr_n)
{
	uint64_t pmcr, pmcr_n;

	__GUEST_ASSERT(expected_pmcr_n <= ARMV8_PMU_MAX_GENERAL_COUNTERS,
			"Expected PMCR.N: 0x%lx; ARMv8 general counters: 0x%lx",
			expected_pmcr_n, ARMV8_PMU_MAX_GENERAL_COUNTERS);

	pmcr = read_sysreg(pmcr_el0);
	pmcr_n = get_pmcr_n(pmcr);

	/* Make sure that PMCR_EL0.N indicates the value userspace set */
	__GUEST_ASSERT(pmcr_n == expected_pmcr_n,
			"Expected PMCR.N: 0x%lx, PMCR.N: 0x%lx",
			expected_pmcr_n, pmcr_n);

	GUEST_DONE();
}

#define GICD_BASE_GPA	0x8000000ULL
#define GICR_BASE_GPA	0x80A0000ULL

/* Create a VM that has one vCPU with PMUv3 configured. */
static void create_vpmu_vm(void *guest_code)
{
	struct kvm_vcpu_init init;
	uint8_t pmuver, ec;
	uint64_t dfr0, irq = 23;
	struct kvm_device_attr irq_attr = {
		.group = KVM_ARM_VCPU_PMU_V3_CTRL,
		.attr = KVM_ARM_VCPU_PMU_V3_IRQ,
		.addr = (uint64_t)&irq,
	};
	struct kvm_device_attr init_attr = {
		.group = KVM_ARM_VCPU_PMU_V3_CTRL,
		.attr = KVM_ARM_VCPU_PMU_V3_INIT,
	};

	/* The test creates the vpmu_vm multiple times. Ensure a clean state */
	memset(&vpmu_vm, 0, sizeof(vpmu_vm));

	vpmu_vm.vm = vm_create(1);
	vm_init_descriptor_tables(vpmu_vm.vm);
	for (ec = 0; ec < ESR_EC_NUM; ec++) {
		vm_install_sync_handler(vpmu_vm.vm, VECTOR_SYNC_CURRENT, ec,
					guest_sync_handler);
	}

	/* Create vCPU with PMUv3 */
	vm_ioctl(vpmu_vm.vm, KVM_ARM_PREFERRED_TARGET, &init);
	init.features[0] |= (1 << KVM_ARM_VCPU_PMU_V3);
	vpmu_vm.vcpu = aarch64_vcpu_add(vpmu_vm.vm, 0, &init, guest_code);
	vcpu_init_descriptor_tables(vpmu_vm.vcpu);
	vpmu_vm.gic_fd = vgic_v3_setup(vpmu_vm.vm, 1, 64,
					GICD_BASE_GPA, GICR_BASE_GPA);
	__TEST_REQUIRE(vpmu_vm.gic_fd >= 0,
		       "Failed to create vgic-v3, skipping");

	/* Make sure that PMUv3 support is indicated in the ID register */
	vcpu_get_reg(vpmu_vm.vcpu,
		     KVM_ARM64_SYS_REG(SYS_ID_AA64DFR0_EL1), &dfr0);
	pmuver = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer), dfr0);
	TEST_ASSERT(pmuver != ID_AA64DFR0_EL1_PMUVer_IMP_DEF &&
		    pmuver >= ID_AA64DFR0_EL1_PMUVer_IMP,
		    "Unexpected PMUVER (0x%x) on the vCPU with PMUv3", pmuver);

	/* Initialize vPMU */
	vcpu_ioctl(vpmu_vm.vcpu, KVM_SET_DEVICE_ATTR, &irq_attr);
	vcpu_ioctl(vpmu_vm.vcpu, KVM_SET_DEVICE_ATTR, &init_attr);
}

static void destroy_vpmu_vm(void)
{
	close(vpmu_vm.gic_fd);
	kvm_vm_free(vpmu_vm.vm);
}

static void run_vcpu(struct kvm_vcpu *vcpu, uint64_t pmcr_n)
{
	struct ucall uc;

	vcpu_args_set(vcpu, 1, pmcr_n);
	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unknown ucall %lu", uc.cmd);
		break;
	}
}

static void test_create_vpmu_vm_with_pmcr_n(uint64_t pmcr_n, bool expect_fail)
{
	struct kvm_vcpu *vcpu;
	uint64_t pmcr, pmcr_orig;

	create_vpmu_vm(guest_code);
	vcpu = vpmu_vm.vcpu;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_PMCR_EL0), &pmcr_orig);
	pmcr = pmcr_orig;

	/*
	 * Setting a larger value of PMCR.N should not modify the field, and
	 * return a success.
	 */
	set_pmcr_n(&pmcr, pmcr_n);
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_PMCR_EL0), pmcr);
	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_PMCR_EL0), &pmcr);

	if (expect_fail)
		TEST_ASSERT(pmcr_orig == pmcr,
			    "PMCR.N modified by KVM to a larger value (PMCR: 0x%lx) for pmcr_n: 0x%lx\n",
			    pmcr, pmcr_n);
	else
		TEST_ASSERT(pmcr_n == get_pmcr_n(pmcr),
			    "Failed to update PMCR.N to %lu (received: %lu)\n",
			    pmcr_n, get_pmcr_n(pmcr));
}

/*
 * Create a guest with one vCPU, set the PMCR_EL0.N for the vCPU to @pmcr_n,
 * and run the test.
 */
static void run_test(uint64_t pmcr_n)
{
	uint64_t sp;
	struct kvm_vcpu *vcpu;
	struct kvm_vcpu_init init;

	pr_debug("Test with pmcr_n %lu\n", pmcr_n);

	test_create_vpmu_vm_with_pmcr_n(pmcr_n, false);
	vcpu = vpmu_vm.vcpu;

	/* Save the initial sp to restore them later to run the guest again */
	vcpu_get_reg(vcpu, ARM64_CORE_REG(sp_el1), &sp);

	run_vcpu(vcpu, pmcr_n);

	/*
	 * Reset and re-initialize the vCPU, and run the guest code again to
	 * check if PMCR_EL0.N is preserved.
	 */
	vm_ioctl(vpmu_vm.vm, KVM_ARM_PREFERRED_TARGET, &init);
	init.features[0] |= (1 << KVM_ARM_VCPU_PMU_V3);
	aarch64_vcpu_setup(vcpu, &init);
	vcpu_init_descriptor_tables(vcpu);
	vcpu_set_reg(vcpu, ARM64_CORE_REG(sp_el1), sp);
	vcpu_set_reg(vcpu, ARM64_CORE_REG(regs.pc), (uint64_t)guest_code);

	run_vcpu(vcpu, pmcr_n);

	destroy_vpmu_vm();
}

/*
 * Create a guest with one vCPU, and attempt to set the PMCR_EL0.N for
 * the vCPU to @pmcr_n, which is larger than the host value.
 * The attempt should fail as @pmcr_n is too big to set for the vCPU.
 */
static void run_error_test(uint64_t pmcr_n)
{
	pr_debug("Error test with pmcr_n %lu (larger than the host)\n", pmcr_n);

	test_create_vpmu_vm_with_pmcr_n(pmcr_n, true);
	destroy_vpmu_vm();
}

/*
 * Return the default number of implemented PMU event counters excluding
 * the cycle counter (i.e. PMCR_EL0.N value) for the guest.
 */
static uint64_t get_pmcr_n_limit(void)
{
	uint64_t pmcr;

	create_vpmu_vm(guest_code);
	vcpu_get_reg(vpmu_vm.vcpu, KVM_ARM64_SYS_REG(SYS_PMCR_EL0), &pmcr);
	destroy_vpmu_vm();
	return get_pmcr_n(pmcr);
}

int main(void)
{
	uint64_t i, pmcr_n;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_ARM_PMU_V3));

	pmcr_n = get_pmcr_n_limit();
	for (i = 0; i <= pmcr_n; i++)
		run_test(i);

	for (i = pmcr_n + 1; i < ARMV8_PMU_MAX_COUNTERS; i++)
		run_error_test(i);

	return 0;
}
