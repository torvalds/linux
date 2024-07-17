// SPDX-License-Identifier: GPL-2.0-only
/*
 * vpmu_counter_access - Test vPMU event counter access
 *
 * Copyright (c) 2023 Google LLC.
 *
 * This test checks if the guest can see the same number of the PMU event
 * counters (PMCR_EL0.N) that userspace sets, if the guest can access
 * those counters, and if the guest is prevented from accessing any
 * other counters.
 * It also checks if the userspace accesses to the PMU regsisters honor the
 * PMCR.N value that's set for the guest.
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

/* The cycle counter bit position that's common among the PMU registers */
#define ARMV8_PMU_CYCLE_IDX		31

struct vpmu_vm {
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	int gic_fd;
};

static struct vpmu_vm vpmu_vm;

struct pmreg_sets {
	uint64_t set_reg_id;
	uint64_t clr_reg_id;
};

#define PMREG_SET(set, clr) {.set_reg_id = set, .clr_reg_id = clr}

static uint64_t get_pmcr_n(uint64_t pmcr)
{
	return FIELD_GET(ARMV8_PMU_PMCR_N, pmcr);
}

static void set_pmcr_n(uint64_t *pmcr, uint64_t pmcr_n)
{
	u64p_replace_bits((__u64 *) pmcr, pmcr_n, ARMV8_PMU_PMCR_N);
}

static uint64_t get_counters_mask(uint64_t n)
{
	uint64_t mask = BIT(ARMV8_PMU_CYCLE_IDX);

	if (n)
		mask |= GENMASK(n - 1, 0);
	return mask;
}

/* Read PMEVTCNTR<n>_EL0 through PMXEVCNTR_EL0 */
static inline unsigned long read_sel_evcntr(int sel)
{
	write_sysreg(sel, pmselr_el0);
	isb();
	return read_sysreg(pmxevcntr_el0);
}

/* Write PMEVTCNTR<n>_EL0 through PMXEVCNTR_EL0 */
static inline void write_sel_evcntr(int sel, unsigned long val)
{
	write_sysreg(sel, pmselr_el0);
	isb();
	write_sysreg(val, pmxevcntr_el0);
	isb();
}

/* Read PMEVTYPER<n>_EL0 through PMXEVTYPER_EL0 */
static inline unsigned long read_sel_evtyper(int sel)
{
	write_sysreg(sel, pmselr_el0);
	isb();
	return read_sysreg(pmxevtyper_el0);
}

/* Write PMEVTYPER<n>_EL0 through PMXEVTYPER_EL0 */
static inline void write_sel_evtyper(int sel, unsigned long val)
{
	write_sysreg(sel, pmselr_el0);
	isb();
	write_sysreg(val, pmxevtyper_el0);
	isb();
}

static void pmu_disable_reset(void)
{
	uint64_t pmcr = read_sysreg(pmcr_el0);

	/* Reset all counters, disabling them */
	pmcr &= ~ARMV8_PMU_PMCR_E;
	write_sysreg(pmcr | ARMV8_PMU_PMCR_P, pmcr_el0);
	isb();
}

#define RETURN_READ_PMEVCNTRN(n) \
	return read_sysreg(pmevcntr##n##_el0)
static unsigned long read_pmevcntrn(int n)
{
	PMEVN_SWITCH(n, RETURN_READ_PMEVCNTRN);
	return 0;
}

#define WRITE_PMEVCNTRN(n) \
	write_sysreg(val, pmevcntr##n##_el0)
static void write_pmevcntrn(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVCNTRN);
	isb();
}

#define READ_PMEVTYPERN(n) \
	return read_sysreg(pmevtyper##n##_el0)
static unsigned long read_pmevtypern(int n)
{
	PMEVN_SWITCH(n, READ_PMEVTYPERN);
	return 0;
}

#define WRITE_PMEVTYPERN(n) \
	write_sysreg(val, pmevtyper##n##_el0)
static void write_pmevtypern(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVTYPERN);
	isb();
}

/*
 * The pmc_accessor structure has pointers to PMEV{CNTR,TYPER}<n>_EL0
 * accessors that test cases will use. Each of the accessors will
 * either directly reads/writes PMEV{CNTR,TYPER}<n>_EL0
 * (i.e. {read,write}_pmev{cnt,type}rn()), or reads/writes them through
 * PMXEV{CNTR,TYPER}_EL0 (i.e. {read,write}_sel_ev{cnt,type}r()).
 *
 * This is used to test that combinations of those accessors provide
 * the consistent behavior.
 */
struct pmc_accessor {
	/* A function to be used to read PMEVTCNTR<n>_EL0 */
	unsigned long	(*read_cntr)(int idx);
	/* A function to be used to write PMEVTCNTR<n>_EL0 */
	void		(*write_cntr)(int idx, unsigned long val);
	/* A function to be used to read PMEVTYPER<n>_EL0 */
	unsigned long	(*read_typer)(int idx);
	/* A function to be used to write PMEVTYPER<n>_EL0 */
	void		(*write_typer)(int idx, unsigned long val);
};

struct pmc_accessor pmc_accessors[] = {
	/* test with all direct accesses */
	{ read_pmevcntrn, write_pmevcntrn, read_pmevtypern, write_pmevtypern },
	/* test with all indirect accesses */
	{ read_sel_evcntr, write_sel_evcntr, read_sel_evtyper, write_sel_evtyper },
	/* read with direct accesses, and write with indirect accesses */
	{ read_pmevcntrn, write_sel_evcntr, read_pmevtypern, write_sel_evtyper },
	/* read with indirect accesses, and write with direct accesses */
	{ read_sel_evcntr, write_pmevcntrn, read_sel_evtyper, write_pmevtypern },
};

/*
 * Convert a pointer of pmc_accessor to an index in pmc_accessors[],
 * assuming that the pointer is one of the entries in pmc_accessors[].
 */
#define PMC_ACC_TO_IDX(acc)	(acc - &pmc_accessors[0])

#define GUEST_ASSERT_BITMAP_REG(regname, mask, set_expected)			 \
{										 \
	uint64_t _tval = read_sysreg(regname);					 \
										 \
	if (set_expected)							 \
		__GUEST_ASSERT((_tval & mask),					 \
				"tval: 0x%lx; mask: 0x%lx; set_expected: %u",	 \
				_tval, mask, set_expected);			 \
	else									 \
		__GUEST_ASSERT(!(_tval & mask),					 \
				"tval: 0x%lx; mask: 0x%lx; set_expected: %u",	 \
				_tval, mask, set_expected);			 \
}

/*
 * Check if @mask bits in {PMCNTEN,PMINTEN,PMOVS}{SET,CLR} registers
 * are set or cleared as specified in @set_expected.
 */
static void check_bitmap_pmu_regs(uint64_t mask, bool set_expected)
{
	GUEST_ASSERT_BITMAP_REG(pmcntenset_el0, mask, set_expected);
	GUEST_ASSERT_BITMAP_REG(pmcntenclr_el0, mask, set_expected);
	GUEST_ASSERT_BITMAP_REG(pmintenset_el1, mask, set_expected);
	GUEST_ASSERT_BITMAP_REG(pmintenclr_el1, mask, set_expected);
	GUEST_ASSERT_BITMAP_REG(pmovsset_el0, mask, set_expected);
	GUEST_ASSERT_BITMAP_REG(pmovsclr_el0, mask, set_expected);
}

/*
 * Check if the bit in {PMCNTEN,PMINTEN,PMOVS}{SET,CLR} registers corresponding
 * to the specified counter (@pmc_idx) can be read/written as expected.
 * When @set_op is true, it tries to set the bit for the counter in
 * those registers by writing the SET registers (the bit won't be set
 * if the counter is not implemented though).
 * Otherwise, it tries to clear the bits in the registers by writing
 * the CLR registers.
 * Then, it checks if the values indicated in the registers are as expected.
 */
static void test_bitmap_pmu_regs(int pmc_idx, bool set_op)
{
	uint64_t pmcr_n, test_bit = BIT(pmc_idx);
	bool set_expected = false;

	if (set_op) {
		write_sysreg(test_bit, pmcntenset_el0);
		write_sysreg(test_bit, pmintenset_el1);
		write_sysreg(test_bit, pmovsset_el0);

		/* The bit will be set only if the counter is implemented */
		pmcr_n = get_pmcr_n(read_sysreg(pmcr_el0));
		set_expected = (pmc_idx < pmcr_n) ? true : false;
	} else {
		write_sysreg(test_bit, pmcntenclr_el0);
		write_sysreg(test_bit, pmintenclr_el1);
		write_sysreg(test_bit, pmovsclr_el0);
	}
	check_bitmap_pmu_regs(test_bit, set_expected);
}

/*
 * Tests for reading/writing registers for the (implemented) event counter
 * specified by @pmc_idx.
 */
static void test_access_pmc_regs(struct pmc_accessor *acc, int pmc_idx)
{
	uint64_t write_data, read_data;

	/* Disable all PMCs and reset all PMCs to zero. */
	pmu_disable_reset();

	/*
	 * Tests for reading/writing {PMCNTEN,PMINTEN,PMOVS}{SET,CLR}_EL1.
	 */

	/* Make sure that the bit in those registers are set to 0 */
	test_bitmap_pmu_regs(pmc_idx, false);
	/* Test if setting the bit in those registers works */
	test_bitmap_pmu_regs(pmc_idx, true);
	/* Test if clearing the bit in those registers works */
	test_bitmap_pmu_regs(pmc_idx, false);

	/*
	 * Tests for reading/writing the event type register.
	 */

	/*
	 * Set the event type register to an arbitrary value just for testing
	 * of reading/writing the register.
	 * Arm ARM says that for the event from 0x0000 to 0x003F,
	 * the value indicated in the PMEVTYPER<n>_EL0.evtCount field is
	 * the value written to the field even when the specified event
	 * is not supported.
	 */
	write_data = (ARMV8_PMU_EXCLUDE_EL1 | ARMV8_PMUV3_PERFCTR_INST_RETIRED);
	acc->write_typer(pmc_idx, write_data);
	read_data = acc->read_typer(pmc_idx);
	__GUEST_ASSERT(read_data == write_data,
		       "pmc_idx: 0x%x; acc_idx: 0x%lx; read_data: 0x%lx; write_data: 0x%lx",
		       pmc_idx, PMC_ACC_TO_IDX(acc), read_data, write_data);

	/*
	 * Tests for reading/writing the event count register.
	 */

	read_data = acc->read_cntr(pmc_idx);

	/* The count value must be 0, as it is disabled and reset */
	__GUEST_ASSERT(read_data == 0,
		       "pmc_idx: 0x%x; acc_idx: 0x%lx; read_data: 0x%lx",
		       pmc_idx, PMC_ACC_TO_IDX(acc), read_data);

	write_data = read_data + pmc_idx + 0x12345;
	acc->write_cntr(pmc_idx, write_data);
	read_data = acc->read_cntr(pmc_idx);
	__GUEST_ASSERT(read_data == write_data,
		       "pmc_idx: 0x%x; acc_idx: 0x%lx; read_data: 0x%lx; write_data: 0x%lx",
		       pmc_idx, PMC_ACC_TO_IDX(acc), read_data, write_data);
}

#define INVALID_EC	(-1ul)
uint64_t expected_ec = INVALID_EC;

static void guest_sync_handler(struct ex_regs *regs)
{
	uint64_t esr, ec;

	esr = read_sysreg(esr_el1);
	ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

	__GUEST_ASSERT(expected_ec == ec,
			"PC: 0x%lx; ESR: 0x%lx; EC: 0x%lx; EC expected: 0x%lx",
			regs->pc, esr, ec, expected_ec);

	/* skip the trapping instruction */
	regs->pc += 4;

	/* Use INVALID_EC to indicate an exception occurred */
	expected_ec = INVALID_EC;
}

/*
 * Run the given operation that should trigger an exception with the
 * given exception class. The exception handler (guest_sync_handler)
 * will reset op_end_addr to 0, expected_ec to INVALID_EC, and skip
 * the instruction that trapped.
 */
#define TEST_EXCEPTION(ec, ops)				\
({							\
	GUEST_ASSERT(ec != INVALID_EC);			\
	WRITE_ONCE(expected_ec, ec);			\
	dsb(ish);					\
	ops;						\
	GUEST_ASSERT(expected_ec == INVALID_EC);	\
})

/*
 * Tests for reading/writing registers for the unimplemented event counter
 * specified by @pmc_idx (>= PMCR_EL0.N).
 */
static void test_access_invalid_pmc_regs(struct pmc_accessor *acc, int pmc_idx)
{
	/*
	 * Reading/writing the event count/type registers should cause
	 * an UNDEFINED exception.
	 */
	TEST_EXCEPTION(ESR_EC_UNKNOWN, acc->read_cntr(pmc_idx));
	TEST_EXCEPTION(ESR_EC_UNKNOWN, acc->write_cntr(pmc_idx, 0));
	TEST_EXCEPTION(ESR_EC_UNKNOWN, acc->read_typer(pmc_idx));
	TEST_EXCEPTION(ESR_EC_UNKNOWN, acc->write_typer(pmc_idx, 0));
	/*
	 * The bit corresponding to the (unimplemented) counter in
	 * {PMCNTEN,PMINTEN,PMOVS}{SET,CLR} registers should be RAZ.
	 */
	test_bitmap_pmu_regs(pmc_idx, 1);
	test_bitmap_pmu_regs(pmc_idx, 0);
}

/*
 * The guest is configured with PMUv3 with @expected_pmcr_n number of
 * event counters.
 * Check if @expected_pmcr_n is consistent with PMCR_EL0.N, and
 * if reading/writing PMU registers for implemented or unimplemented
 * counters works as expected.
 */
static void guest_code(uint64_t expected_pmcr_n)
{
	uint64_t pmcr, pmcr_n, unimp_mask;
	int i, pmc;

	__GUEST_ASSERT(expected_pmcr_n <= ARMV8_PMU_MAX_GENERAL_COUNTERS,
			"Expected PMCR.N: 0x%lx; ARMv8 general counters: 0x%x",
			expected_pmcr_n, ARMV8_PMU_MAX_GENERAL_COUNTERS);

	pmcr = read_sysreg(pmcr_el0);
	pmcr_n = get_pmcr_n(pmcr);

	/* Make sure that PMCR_EL0.N indicates the value userspace set */
	__GUEST_ASSERT(pmcr_n == expected_pmcr_n,
			"Expected PMCR.N: 0x%lx, PMCR.N: 0x%lx",
			expected_pmcr_n, pmcr_n);

	/*
	 * Make sure that (RAZ) bits corresponding to unimplemented event
	 * counters in {PMCNTEN,PMINTEN,PMOVS}{SET,CLR} registers are reset
	 * to zero.
	 * (NOTE: bits for implemented event counters are reset to UNKNOWN)
	 */
	unimp_mask = GENMASK_ULL(ARMV8_PMU_MAX_GENERAL_COUNTERS - 1, pmcr_n);
	check_bitmap_pmu_regs(unimp_mask, false);

	/*
	 * Tests for reading/writing PMU registers for implemented counters.
	 * Use each combination of PMEV{CNTR,TYPER}<n>_EL0 accessor functions.
	 */
	for (i = 0; i < ARRAY_SIZE(pmc_accessors); i++) {
		for (pmc = 0; pmc < pmcr_n; pmc++)
			test_access_pmc_regs(&pmc_accessors[i], pmc);
	}

	/*
	 * Tests for reading/writing PMU registers for unimplemented counters.
	 * Use each combination of PMEV{CNTR,TYPER}<n>_EL0 accessor functions.
	 */
	for (i = 0; i < ARRAY_SIZE(pmc_accessors); i++) {
		for (pmc = pmcr_n; pmc < ARMV8_PMU_MAX_GENERAL_COUNTERS; pmc++)
			test_access_invalid_pmc_regs(&pmc_accessors[i], pmc);
	}

	GUEST_DONE();
}

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
	vpmu_vm.gic_fd = vgic_v3_setup(vpmu_vm.vm, 1, 64);
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
			    "PMCR.N modified by KVM to a larger value (PMCR: 0x%lx) for pmcr_n: 0x%lx",
			    pmcr, pmcr_n);
	else
		TEST_ASSERT(pmcr_n == get_pmcr_n(pmcr),
			    "Failed to update PMCR.N to %lu (received: %lu)",
			    pmcr_n, get_pmcr_n(pmcr));
}

/*
 * Create a guest with one vCPU, set the PMCR_EL0.N for the vCPU to @pmcr_n,
 * and run the test.
 */
static void run_access_test(uint64_t pmcr_n)
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

static struct pmreg_sets validity_check_reg_sets[] = {
	PMREG_SET(SYS_PMCNTENSET_EL0, SYS_PMCNTENCLR_EL0),
	PMREG_SET(SYS_PMINTENSET_EL1, SYS_PMINTENCLR_EL1),
	PMREG_SET(SYS_PMOVSSET_EL0, SYS_PMOVSCLR_EL0),
};

/*
 * Create a VM, and check if KVM handles the userspace accesses of
 * the PMU register sets in @validity_check_reg_sets[] correctly.
 */
static void run_pmregs_validity_test(uint64_t pmcr_n)
{
	int i;
	struct kvm_vcpu *vcpu;
	uint64_t set_reg_id, clr_reg_id, reg_val;
	uint64_t valid_counters_mask, max_counters_mask;

	test_create_vpmu_vm_with_pmcr_n(pmcr_n, false);
	vcpu = vpmu_vm.vcpu;

	valid_counters_mask = get_counters_mask(pmcr_n);
	max_counters_mask = get_counters_mask(ARMV8_PMU_MAX_COUNTERS);

	for (i = 0; i < ARRAY_SIZE(validity_check_reg_sets); i++) {
		set_reg_id = validity_check_reg_sets[i].set_reg_id;
		clr_reg_id = validity_check_reg_sets[i].clr_reg_id;

		/*
		 * Test if the 'set' and 'clr' variants of the registers
		 * are initialized based on the number of valid counters.
		 */
		vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(set_reg_id), &reg_val);
		TEST_ASSERT((reg_val & (~valid_counters_mask)) == 0,
			    "Initial read of set_reg: 0x%llx has unimplemented counters enabled: 0x%lx",
			    KVM_ARM64_SYS_REG(set_reg_id), reg_val);

		vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(clr_reg_id), &reg_val);
		TEST_ASSERT((reg_val & (~valid_counters_mask)) == 0,
			    "Initial read of clr_reg: 0x%llx has unimplemented counters enabled: 0x%lx",
			    KVM_ARM64_SYS_REG(clr_reg_id), reg_val);

		/*
		 * Using the 'set' variant, force-set the register to the
		 * max number of possible counters and test if KVM discards
		 * the bits for unimplemented counters as it should.
		 */
		vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(set_reg_id), max_counters_mask);

		vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(set_reg_id), &reg_val);
		TEST_ASSERT((reg_val & (~valid_counters_mask)) == 0,
			    "Read of set_reg: 0x%llx has unimplemented counters enabled: 0x%lx",
			    KVM_ARM64_SYS_REG(set_reg_id), reg_val);

		vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(clr_reg_id), &reg_val);
		TEST_ASSERT((reg_val & (~valid_counters_mask)) == 0,
			    "Read of clr_reg: 0x%llx has unimplemented counters enabled: 0x%lx",
			    KVM_ARM64_SYS_REG(clr_reg_id), reg_val);
	}

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
	for (i = 0; i <= pmcr_n; i++) {
		run_access_test(i);
		run_pmregs_validity_test(i);
	}

	for (i = pmcr_n + 1; i < ARMV8_PMU_MAX_COUNTERS; i++)
		run_error_test(i);

	return 0;
}
