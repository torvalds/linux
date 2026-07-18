// SPDX-License-Identifier: GPL-2.0
/*
 * LoongArch KVM PMU event counting test
 *
 * Test hardware event counting: CPU_CYCLES, INSTR_RETIRED,
 * BRANCH_INSTRUCTIONS and BRANCH_MISSES.
 */
#include <linux/bitops.h>
#include "kvm_util.h"
#include "pmu.h"
#include "loongarch/processor.h"

static int pmu_irq_count;

/* Check PMU support */
static bool has_pmu_support(void)
{
	u32 cfg6;

	/* Read CPUCFG6 to check PMU */
	cfg6 = read_cpucfg(LOONGARCH_CPUCFG6);

	/* Check PMU present bit */
	if (!(cfg6 & CPUCFG6_PMP))
		return false;

	/* Check that at least one counter exists */
	if (((cfg6 & CPUCFG6_PMNUM) >> CPUCFG6_PMNUM_SHIFT) == 0)
		return false;

	return true;
}

/* Dump PMU capabilities */
static void dump_pmu_caps(void)
{
	u32 cfg6;
	int nr_counters, counter_bits;

	cfg6 = read_cpucfg(LOONGARCH_CPUCFG6);
	nr_counters = ((cfg6 & CPUCFG6_PMNUM) >> CPUCFG6_PMNUM_SHIFT) + 1;
	counter_bits = ((cfg6 & CPUCFG6_PMBITS) >> CPUCFG6_PMBITS_SHIFT) + 1;

	pr_info("PMU capabilities:\n");
	pr_info("  Counters present: %s\n", cfg6 & CPUCFG6_PMP ? "yes" : "no");
	pr_info("  Number of counters: %d\n", nr_counters);
	pr_info("  Counter width: %d bits\n", counter_bits);
}

/* Guest test code - runs inside VM */
static void guest_pmu_base_test(void)
{
	int i;
	u32 cfg6, pmnum;
	u64 cnt[4];

	cfg6 = read_cpucfg(LOONGARCH_CPUCFG6);
	pmnum = (cfg6 >> 4) & 0xf;
	GUEST_PRINTF("CPUCFG6 = 0x%x\n", cfg6);
	GUEST_PRINTF("PMP enabled: %s\n", (cfg6 & 0x1) ? "YES" : "NO");
	GUEST_PRINTF("Number of counters (PMNUM): %x\n", pmnum + 1);
	GUEST_ASSERT(pmnum == 3);

	GUEST_PRINTF("Clean csr_perfcntr0-3\n");
	csr_write(0, LOONGARCH_CSR_PERFCNTR0);
	csr_write(0, LOONGARCH_CSR_PERFCNTR1);
	csr_write(0, LOONGARCH_CSR_PERFCNTR2);
	csr_write(0, LOONGARCH_CSR_PERFCNTR3);
	GUEST_PRINTF("Set csr_perfctrl0 for cycles event\n");
	csr_write(PMU_ENVENT_ENABLED |
		LOONGARCH_PMU_EVENT_CYCLES, LOONGARCH_CSR_PERFCTRL0);
	GUEST_PRINTF("Set csr_perfctrl1 for instr_retired event\n");
	csr_write(PMU_ENVENT_ENABLED |
		LOONGARCH_PMU_EVENT_INSTR_RETIRED, LOONGARCH_CSR_PERFCTRL1);
	GUEST_PRINTF("Set csr_perfctrl2 for branch_instructions event\n");
	csr_write(PMU_ENVENT_ENABLED |
		PERF_COUNT_HW_BRANCH_INSTRUCTIONS, LOONGARCH_CSR_PERFCTRL2);
	GUEST_PRINTF("Set csr_perfctrl3 for branch_misses event\n");
	csr_write(PMU_ENVENT_ENABLED |
		PERF_COUNT_HW_BRANCH_MISSES, LOONGARCH_CSR_PERFCTRL3);

	for (i = 0; i < NUM_LOOPS; i++)
		cpu_relax();

	cnt[0] = csr_read(LOONGARCH_CSR_PERFCNTR0);
	GUEST_PRINTF("csr_perfcntr0 is %lx\n", cnt[0]);
	cnt[1] = csr_read(LOONGARCH_CSR_PERFCNTR1);
	GUEST_PRINTF("csr_perfcntr1 is %lx\n", cnt[1]);
	cnt[2] = csr_read(LOONGARCH_CSR_PERFCNTR2);
	GUEST_PRINTF("csr_perfcntr2 is %lx\n", cnt[2]);
	cnt[3] = csr_read(LOONGARCH_CSR_PERFCNTR3);
	GUEST_PRINTF("csr_perfcntr3 is %lx\n", cnt[3]);

	GUEST_PRINTF("assert csr_perfcntr0 >EXPECTED_CYCLES_MIN && csr_perfcntr0 < UPPER_BOUND\n");
	GUEST_ASSERT(cnt[0] > EXPECTED_CYCLES_MIN && cnt[0] < UPPER_BOUND);
	GUEST_PRINTF("assert csr_perfcntr1 > EXPECTED_INSTR_MIN && csr_perfcntr1 < UPPER_BOUND\n");
	GUEST_ASSERT(cnt[1] > EXPECTED_INSTR_MIN && cnt[1] < UPPER_BOUND);
	GUEST_PRINTF("assert csr_perfcntr2 > 0 && csr_perfcntr2 < UPPER_BOUND\n");
	GUEST_ASSERT(cnt[2] > 0 && cnt[2] < UPPER_BOUND);
	GUEST_PRINTF("assert csr_perfcntr3 > 0 && csr_perfcntr3 < UPPER_BOUND\n");
	GUEST_ASSERT(cnt[3] > 0 && cnt[3] < UPPER_BOUND);
}

static void guest_irq_handler(struct ex_regs *regs)
{
	unsigned int intid;

	pmu_irq_disable();
	intid = !!(regs->estat & BIT(INT_PMI));
	GUEST_ASSERT_EQ(intid, 1);
	GUEST_PRINTF("Get PMU interrupt\n");
	WRITE_ONCE(pmu_irq_count, pmu_irq_count + 1);
}

static void guest_pmu_interrupt_test(void)
{
	u64 cnt;

	csr_write(PMU_OVERFLOW - 1, LOONGARCH_CSR_PERFCNTR0);
	csr_write(PMU_ENVENT_ENABLED | CSR_PERFCTRL_PMIE | LOONGARCH_PMU_EVENT_CYCLES, LOONGARCH_CSR_PERFCTRL0);

	cpu_relax();

	GUEST_ASSERT_EQ(pmu_irq_count, 1);
	cnt = csr_read(LOONGARCH_CSR_PERFCNTR0);
	GUEST_PRINTF("csr_perfcntr0 is %lx\n", cnt);
	GUEST_PRINTF("PMU interrupt test success\n");

}

static void guest_code(void)
{
	guest_pmu_base_test();

	pmu_irq_enable();
	local_irq_enable();
	guest_pmu_interrupt_test();

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	int ret = 0;
	struct kvm_device_attr attr;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	/* Check host KVM PMU support */
	if (!has_pmu_support()) {
		print_skip("PMU not supported by host hardware\n");
		dump_pmu_caps();
		return KSFT_SKIP;
	}
	pr_info("Host support PMU\n");

	/* Dump PMU capabilities */
	dump_pmu_caps();

	vm = vm_create(VM_MODE_P47V47_16K);
	vcpu = vm_vcpu_add(vm, 0, guest_code);

	pmu_irq_count = 0;
	vm_init_descriptor_tables(vm);
	loongarch_vcpu_setup(vcpu);
	vm_install_exception_handler(vm, EXCCODE_INT, guest_irq_handler);
	sync_global_to_guest(vm, pmu_irq_count);

	attr.group = KVM_LOONGARCH_VM_FEAT_CTRL,
	attr.attr = KVM_LOONGARCH_VM_FEAT_PMU,

	ret = ioctl(vm->fd, KVM_HAS_DEVICE_ATTR, &attr);

	if (ret == 0) {
		pr_info("PMU is enabled in VM\n");
	} else {
		print_skip("PMU not enabled by VM config\n");
		return KSFT_SKIP;
	}

	while (1) {
		vcpu_run(vcpu);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_PRINTF:
			printf("%s", (const char *)uc.buffer);
			break;
		case UCALL_DONE:
			printf("PMU test PASSED\n");
			goto done;
		case UCALL_ABORT:
			printf("PMU test FAILED\n");
			ret = -1;
			goto done;
		default:
			printf("Unexpected exit\n");
			ret = -1;
			goto done;
		}
	}

done:
	kvm_vm_free(vm);
	return ret;
}
