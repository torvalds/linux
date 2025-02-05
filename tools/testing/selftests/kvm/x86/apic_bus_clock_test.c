// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Intel Corporation
 *
 * Verify KVM correctly emulates the APIC bus frequency when the VMM configures
 * the frequency via KVM_CAP_X86_APIC_BUS_CYCLES_NS.  Start the APIC timer by
 * programming TMICT (timer initial count) to the largest value possible (so
 * that the timer will not expire during the test).  Then, after an arbitrary
 * amount of time has elapsed, verify TMCCT (timer current count) is within 1%
 * of the expected value based on the time elapsed, the APIC bus frequency, and
 * the programmed TDCR (timer divide configuration register).
 */

#include "apic.h"
#include "test_util.h"

/*
 * Possible TDCR values with matching divide count. Used to modify APIC
 * timer frequency.
 */
static const struct {
	const uint32_t tdcr;
	const uint32_t divide_count;
} tdcrs[] = {
	{0x0, 2},
	{0x1, 4},
	{0x2, 8},
	{0x3, 16},
	{0x8, 32},
	{0x9, 64},
	{0xa, 128},
	{0xb, 1},
};

static bool is_x2apic;

static void apic_enable(void)
{
	if (is_x2apic)
		x2apic_enable();
	else
		xapic_enable();
}

static uint32_t apic_read_reg(unsigned int reg)
{
	return is_x2apic ? x2apic_read_reg(reg) : xapic_read_reg(reg);
}

static void apic_write_reg(unsigned int reg, uint32_t val)
{
	if (is_x2apic)
		x2apic_write_reg(reg, val);
	else
		xapic_write_reg(reg, val);
}

static void apic_guest_code(uint64_t apic_hz, uint64_t delay_ms)
{
	uint64_t tsc_hz = guest_tsc_khz * 1000;
	const uint32_t tmict = ~0u;
	uint64_t tsc0, tsc1, freq;
	uint32_t tmcct;
	int i;

	apic_enable();

	/*
	 * Setup one-shot timer.  The vector does not matter because the
	 * interrupt should not fire.
	 */
	apic_write_reg(APIC_LVTT, APIC_LVT_TIMER_ONESHOT | APIC_LVT_MASKED);

	for (i = 0; i < ARRAY_SIZE(tdcrs); i++) {
		apic_write_reg(APIC_TDCR, tdcrs[i].tdcr);
		apic_write_reg(APIC_TMICT, tmict);

		tsc0 = rdtsc();
		udelay(delay_ms * 1000);
		tmcct = apic_read_reg(APIC_TMCCT);
		tsc1 = rdtsc();

		/*
		 * Stop the timer _after_ reading the current, final count, as
		 * writing the initial counter also modifies the current count.
		 */
		apic_write_reg(APIC_TMICT, 0);

		freq = (tmict - tmcct) * tdcrs[i].divide_count * tsc_hz / (tsc1 - tsc0);
		/* Check if measured frequency is within 5% of configured frequency. */
		__GUEST_ASSERT(freq < apic_hz * 105 / 100 && freq > apic_hz * 95 / 100,
			       "Frequency = %lu (wanted %lu - %lu), bus = %lu, div = %u, tsc = %lu",
			       freq, apic_hz * 95 / 100, apic_hz * 105 / 100,
			       apic_hz, tdcrs[i].divide_count, tsc_hz);
	}

	GUEST_DONE();
}

static void test_apic_bus_clock(struct kvm_vcpu *vcpu)
{
	bool done = false;
	struct ucall uc;

	while (!done) {
		vcpu_run(vcpu);

		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_DONE:
			done = true;
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
			break;
		}
	}
}

static void run_apic_bus_clock_test(uint64_t apic_hz, uint64_t delay_ms,
				    bool x2apic)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int ret;

	is_x2apic = x2apic;

	vm = vm_create(1);

	sync_global_to_guest(vm, is_x2apic);

	vm_enable_cap(vm, KVM_CAP_X86_APIC_BUS_CYCLES_NS,
		      NSEC_PER_SEC / apic_hz);

	vcpu = vm_vcpu_add(vm, 0, apic_guest_code);
	vcpu_args_set(vcpu, 2, apic_hz, delay_ms);

	ret = __vm_enable_cap(vm, KVM_CAP_X86_APIC_BUS_CYCLES_NS,
			      NSEC_PER_SEC / apic_hz);
	TEST_ASSERT(ret < 0 && errno == EINVAL,
		    "Setting of APIC bus frequency after vCPU is created should fail.");

	if (!is_x2apic)
		virt_pg_map(vm, APIC_DEFAULT_GPA, APIC_DEFAULT_GPA);

	test_apic_bus_clock(vcpu);
	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-d delay] [-f APIC bus freq]\n", name);
	puts("");
	printf("-d: Delay (in msec) guest uses to measure APIC bus frequency.\n");
	printf("-f: The APIC bus frequency (in MHz) to be configured for the guest.\n");
	puts("");
}

int main(int argc, char *argv[])
{
	/*
	 * Arbitrarilty default to 25MHz for the APIC bus frequency, which is
	 * different enough from the default 1GHz to be interesting.
	 */
	uint64_t apic_hz = 25 * 1000 * 1000;
	uint64_t delay_ms = 100;
	int opt;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_APIC_BUS_CYCLES_NS));

	while ((opt = getopt(argc, argv, "d:f:h")) != -1) {
		switch (opt) {
		case 'f':
			apic_hz = atoi_positive("APIC bus frequency", optarg) * 1000 * 1000;
			break;
		case 'd':
			delay_ms = atoi_positive("Delay in milliseconds", optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			exit(KSFT_SKIP);
		}
	}

	run_apic_bus_clock_test(apic_hz, delay_ms, false);
	run_apic_bus_clock_test(apic_hz, delay_ms, true);
}
