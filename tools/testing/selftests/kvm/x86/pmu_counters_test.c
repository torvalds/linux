// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023, Tencent, Inc.
 */
#include <x86intrin.h>

#include "pmu.h"
#include "processor.h"

/* Number of iterations of the loop for the guest measurement payload. */
#define NUM_LOOPS			10

/* Each iteration of the loop retires one branch instruction. */
#define NUM_BRANCH_INSNS_RETIRED	(NUM_LOOPS)

/*
 * Number of instructions in each loop. 1 CLFLUSH/CLFLUSHOPT/NOP, 1 MFENCE,
 * 1 LOOP.
 */
#define NUM_INSNS_PER_LOOP		4

/*
 * Number of "extra" instructions that will be counted, i.e. the number of
 * instructions that are needed to set up the loop and then disable the
 * counter.  2 MOV, 2 XOR, 1 WRMSR.
 */
#define NUM_EXTRA_INSNS			5

/* Total number of instructions retired within the measured section. */
#define NUM_INSNS_RETIRED		(NUM_LOOPS * NUM_INSNS_PER_LOOP + NUM_EXTRA_INSNS)

/* Track which architectural events are supported by hardware. */
static uint32_t hardware_pmu_arch_events;

static uint8_t kvm_pmu_version;
static bool kvm_has_perf_caps;

#define X86_PMU_FEATURE_NULL						\
({									\
	struct kvm_x86_pmu_feature feature = {};			\
									\
	feature;							\
})

static bool pmu_is_null_feature(struct kvm_x86_pmu_feature event)
{
	return !(*(u64 *)&event);
}

struct kvm_intel_pmu_event {
	struct kvm_x86_pmu_feature gp_event;
	struct kvm_x86_pmu_feature fixed_event;
};

/*
 * Wrap the array to appease the compiler, as the macros used to construct each
 * kvm_x86_pmu_feature use syntax that's only valid in function scope, and the
 * compiler often thinks the feature definitions aren't compile-time constants.
 */
static struct kvm_intel_pmu_event intel_event_to_feature(uint8_t idx)
{
	const struct kvm_intel_pmu_event __intel_event_to_feature[] = {
		[INTEL_ARCH_CPU_CYCLES_INDEX]		 = { X86_PMU_FEATURE_CPU_CYCLES, X86_PMU_FEATURE_CPU_CYCLES_FIXED },
		[INTEL_ARCH_INSTRUCTIONS_RETIRED_INDEX]	 = { X86_PMU_FEATURE_INSNS_RETIRED, X86_PMU_FEATURE_INSNS_RETIRED_FIXED },
		/*
		 * Note, the fixed counter for reference cycles is NOT the same as the
		 * general purpose architectural event.  The fixed counter explicitly
		 * counts at the same frequency as the TSC, whereas the GP event counts
		 * at a fixed, but uarch specific, frequency.  Bundle them here for
		 * simplicity.
		 */
		[INTEL_ARCH_REFERENCE_CYCLES_INDEX]	 = { X86_PMU_FEATURE_REFERENCE_CYCLES, X86_PMU_FEATURE_REFERENCE_TSC_CYCLES_FIXED },
		[INTEL_ARCH_LLC_REFERENCES_INDEX]	 = { X86_PMU_FEATURE_LLC_REFERENCES, X86_PMU_FEATURE_NULL },
		[INTEL_ARCH_LLC_MISSES_INDEX]		 = { X86_PMU_FEATURE_LLC_MISSES, X86_PMU_FEATURE_NULL },
		[INTEL_ARCH_BRANCHES_RETIRED_INDEX]	 = { X86_PMU_FEATURE_BRANCH_INSNS_RETIRED, X86_PMU_FEATURE_NULL },
		[INTEL_ARCH_BRANCHES_MISPREDICTED_INDEX] = { X86_PMU_FEATURE_BRANCHES_MISPREDICTED, X86_PMU_FEATURE_NULL },
		[INTEL_ARCH_TOPDOWN_SLOTS_INDEX]	 = { X86_PMU_FEATURE_TOPDOWN_SLOTS, X86_PMU_FEATURE_TOPDOWN_SLOTS_FIXED },
	};

	kvm_static_assert(ARRAY_SIZE(__intel_event_to_feature) == NR_INTEL_ARCH_EVENTS);

	return __intel_event_to_feature[idx];
}

static struct kvm_vm *pmu_vm_create_with_one_vcpu(struct kvm_vcpu **vcpu,
						  void *guest_code,
						  uint8_t pmu_version,
						  uint64_t perf_capabilities)
{
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(vcpu, guest_code);
	sync_global_to_guest(vm, kvm_pmu_version);
	sync_global_to_guest(vm, hardware_pmu_arch_events);

	/*
	 * Set PERF_CAPABILITIES before PMU version as KVM disallows enabling
	 * features via PERF_CAPABILITIES if the guest doesn't have a vPMU.
	 */
	if (kvm_has_perf_caps)
		vcpu_set_msr(*vcpu, MSR_IA32_PERF_CAPABILITIES, perf_capabilities);

	vcpu_set_cpuid_property(*vcpu, X86_PROPERTY_PMU_VERSION, pmu_version);
	return vm;
}

static void run_vcpu(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	do {
		vcpu_run(vcpu);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_PRINTF:
			pr_info("%s", uc.buffer);
			break;
		case UCALL_DONE:
			break;
		default:
			TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
		}
	} while (uc.cmd != UCALL_DONE);
}

static uint8_t guest_get_pmu_version(void)
{
	/*
	 * Return the effective PMU version, i.e. the minimum between what KVM
	 * supports and what is enumerated to the guest.  The host deliberately
	 * advertises a PMU version to the guest beyond what is actually
	 * supported by KVM to verify KVM doesn't freak out and do something
	 * bizarre with an architecturally valid, but unsupported, version.
	 */
	return min_t(uint8_t, kvm_pmu_version, this_cpu_property(X86_PROPERTY_PMU_VERSION));
}

/*
 * If an architectural event is supported and guaranteed to generate at least
 * one "hit, assert that its count is non-zero.  If an event isn't supported or
 * the test can't guarantee the associated action will occur, then all bets are
 * off regarding the count, i.e. no checks can be done.
 *
 * Sanity check that in all cases, the event doesn't count when it's disabled,
 * and that KVM correctly emulates the write of an arbitrary value.
 */
static void guest_assert_event_count(uint8_t idx, uint32_t pmc, uint32_t pmc_msr)
{
	uint64_t count;

	count = _rdpmc(pmc);
	if (!(hardware_pmu_arch_events & BIT(idx)))
		goto sanity_checks;

	switch (idx) {
	case INTEL_ARCH_INSTRUCTIONS_RETIRED_INDEX:
		GUEST_ASSERT_EQ(count, NUM_INSNS_RETIRED);
		break;
	case INTEL_ARCH_BRANCHES_RETIRED_INDEX:
		GUEST_ASSERT_EQ(count, NUM_BRANCH_INSNS_RETIRED);
		break;
	case INTEL_ARCH_LLC_REFERENCES_INDEX:
	case INTEL_ARCH_LLC_MISSES_INDEX:
		if (!this_cpu_has(X86_FEATURE_CLFLUSHOPT) &&
		    !this_cpu_has(X86_FEATURE_CLFLUSH))
			break;
		fallthrough;
	case INTEL_ARCH_CPU_CYCLES_INDEX:
	case INTEL_ARCH_REFERENCE_CYCLES_INDEX:
		GUEST_ASSERT_NE(count, 0);
		break;
	case INTEL_ARCH_TOPDOWN_SLOTS_INDEX:
		__GUEST_ASSERT(count >= NUM_INSNS_RETIRED,
			       "Expected top-down slots >= %u, got count = %lu",
			       NUM_INSNS_RETIRED, count);
		break;
	default:
		break;
	}

sanity_checks:
	__asm__ __volatile__("loop ." : "+c"((int){NUM_LOOPS}));
	GUEST_ASSERT_EQ(_rdpmc(pmc), count);

	wrmsr(pmc_msr, 0xdead);
	GUEST_ASSERT_EQ(_rdpmc(pmc), 0xdead);
}

/*
 * Enable and disable the PMC in a monolithic asm blob to ensure that the
 * compiler can't insert _any_ code into the measured sequence.  Note, ECX
 * doesn't need to be clobbered as the input value, @pmc_msr, is restored
 * before the end of the sequence.
 *
 * If CLFUSH{,OPT} is supported, flush the cacheline containing (at least) the
 * CLFUSH{,OPT} instruction on each loop iteration to force LLC references and
 * misses, i.e. to allow testing that those events actually count.
 *
 * If forced emulation is enabled (and specified), force emulation on a subset
 * of the measured code to verify that KVM correctly emulates instructions and
 * branches retired events in conjunction with hardware also counting said
 * events.
 */
#define GUEST_MEASURE_EVENT(_msr, _value, clflush, FEP)				\
do {										\
	__asm__ __volatile__("wrmsr\n\t"					\
			     " mov $" __stringify(NUM_LOOPS) ", %%ecx\n\t"	\
			     "1:\n\t"						\
			     clflush "\n\t"					\
			     "mfence\n\t"					\
			     "mov %[m], %%eax\n\t"				\
			     FEP "loop 1b\n\t"					\
			     FEP "mov %%edi, %%ecx\n\t"				\
			     FEP "xor %%eax, %%eax\n\t"				\
			     FEP "xor %%edx, %%edx\n\t"				\
			     "wrmsr\n\t"					\
			     :: "a"((uint32_t)_value), "d"(_value >> 32),	\
				"c"(_msr), "D"(_msr), [m]"m"(kvm_pmu_version)	\
	);									\
} while (0)

#define GUEST_TEST_EVENT(_idx, _pmc, _pmc_msr, _ctrl_msr, _value, FEP)		\
do {										\
	wrmsr(_pmc_msr, 0);							\
										\
	if (this_cpu_has(X86_FEATURE_CLFLUSHOPT))				\
		GUEST_MEASURE_EVENT(_ctrl_msr, _value, "clflushopt %[m]", FEP);	\
	else if (this_cpu_has(X86_FEATURE_CLFLUSH))				\
		GUEST_MEASURE_EVENT(_ctrl_msr, _value, "clflush  %[m]", FEP);	\
	else									\
		GUEST_MEASURE_EVENT(_ctrl_msr, _value, "nop", FEP);		\
										\
	guest_assert_event_count(_idx, _pmc, _pmc_msr);				\
} while (0)

static void __guest_test_arch_event(uint8_t idx, uint32_t pmc, uint32_t pmc_msr,
				    uint32_t ctrl_msr, uint64_t ctrl_msr_value)
{
	GUEST_TEST_EVENT(idx, pmc, pmc_msr, ctrl_msr, ctrl_msr_value, "");

	if (is_forced_emulation_enabled)
		GUEST_TEST_EVENT(idx, pmc, pmc_msr, ctrl_msr, ctrl_msr_value, KVM_FEP);
}

static void guest_test_arch_event(uint8_t idx)
{
	uint32_t nr_gp_counters = this_cpu_property(X86_PROPERTY_PMU_NR_GP_COUNTERS);
	uint32_t pmu_version = guest_get_pmu_version();
	/* PERF_GLOBAL_CTRL exists only for Architectural PMU Version 2+. */
	bool guest_has_perf_global_ctrl = pmu_version >= 2;
	struct kvm_x86_pmu_feature gp_event, fixed_event;
	uint32_t base_pmc_msr;
	unsigned int i;

	/* The host side shouldn't invoke this without a guest PMU. */
	GUEST_ASSERT(pmu_version);

	if (this_cpu_has(X86_FEATURE_PDCM) &&
	    rdmsr(MSR_IA32_PERF_CAPABILITIES) & PMU_CAP_FW_WRITES)
		base_pmc_msr = MSR_IA32_PMC0;
	else
		base_pmc_msr = MSR_IA32_PERFCTR0;

	gp_event = intel_event_to_feature(idx).gp_event;
	GUEST_ASSERT_EQ(idx, gp_event.f.bit);

	GUEST_ASSERT(nr_gp_counters);

	for (i = 0; i < nr_gp_counters; i++) {
		uint64_t eventsel = ARCH_PERFMON_EVENTSEL_OS |
				    ARCH_PERFMON_EVENTSEL_ENABLE |
				    intel_pmu_arch_events[idx];

		wrmsr(MSR_P6_EVNTSEL0 + i, 0);
		if (guest_has_perf_global_ctrl)
			wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, BIT_ULL(i));

		__guest_test_arch_event(idx, i, base_pmc_msr + i,
					MSR_P6_EVNTSEL0 + i, eventsel);
	}

	if (!guest_has_perf_global_ctrl)
		return;

	fixed_event = intel_event_to_feature(idx).fixed_event;
	if (pmu_is_null_feature(fixed_event) || !this_pmu_has(fixed_event))
		return;

	i = fixed_event.f.bit;

	wrmsr(MSR_CORE_PERF_FIXED_CTR_CTRL, FIXED_PMC_CTRL(i, FIXED_PMC_KERNEL));

	__guest_test_arch_event(idx, i | INTEL_RDPMC_FIXED,
				MSR_CORE_PERF_FIXED_CTR0 + i,
				MSR_CORE_PERF_GLOBAL_CTRL,
				FIXED_PMC_GLOBAL_CTRL_ENABLE(i));
}

static void guest_test_arch_events(void)
{
	uint8_t i;

	for (i = 0; i < NR_INTEL_ARCH_EVENTS; i++)
		guest_test_arch_event(i);

	GUEST_DONE();
}

static void test_arch_events(uint8_t pmu_version, uint64_t perf_capabilities,
			     uint8_t length, uint8_t unavailable_mask)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	/* Testing arch events requires a vPMU (there are no negative tests). */
	if (!pmu_version)
		return;

	vm = pmu_vm_create_with_one_vcpu(&vcpu, guest_test_arch_events,
					 pmu_version, perf_capabilities);

	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH,
				length);
	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_PMU_EVENTS_MASK,
				unavailable_mask);

	run_vcpu(vcpu);

	kvm_vm_free(vm);
}

/*
 * Limit testing to MSRs that are actually defined by Intel (in the SDM).  MSRs
 * that aren't defined counter MSRs *probably* don't exist, but there's no
 * guarantee that currently undefined MSR indices won't be used for something
 * other than PMCs in the future.
 */
#define MAX_NR_GP_COUNTERS	8
#define MAX_NR_FIXED_COUNTERS	3

#define GUEST_ASSERT_PMC_MSR_ACCESS(insn, msr, expect_gp, vector)		\
__GUEST_ASSERT(expect_gp ? vector == GP_VECTOR : !vector,			\
	       "Expected %s on " #insn "(0x%x), got vector %u",			\
	       expect_gp ? "#GP" : "no fault", msr, vector)			\

#define GUEST_ASSERT_PMC_VALUE(insn, msr, val, expected)			\
	__GUEST_ASSERT(val == expected,					\
		       "Expected " #insn "(0x%x) to yield 0x%lx, got 0x%lx",	\
		       msr, expected, val);

static void guest_test_rdpmc(uint32_t rdpmc_idx, bool expect_success,
			     uint64_t expected_val)
{
	uint8_t vector;
	uint64_t val;

	vector = rdpmc_safe(rdpmc_idx, &val);
	GUEST_ASSERT_PMC_MSR_ACCESS(RDPMC, rdpmc_idx, !expect_success, vector);
	if (expect_success)
		GUEST_ASSERT_PMC_VALUE(RDPMC, rdpmc_idx, val, expected_val);

	if (!is_forced_emulation_enabled)
		return;

	vector = rdpmc_safe_fep(rdpmc_idx, &val);
	GUEST_ASSERT_PMC_MSR_ACCESS(RDPMC, rdpmc_idx, !expect_success, vector);
	if (expect_success)
		GUEST_ASSERT_PMC_VALUE(RDPMC, rdpmc_idx, val, expected_val);
}

static void guest_rd_wr_counters(uint32_t base_msr, uint8_t nr_possible_counters,
				 uint8_t nr_counters, uint32_t or_mask)
{
	const bool pmu_has_fast_mode = !guest_get_pmu_version();
	uint8_t i;

	for (i = 0; i < nr_possible_counters; i++) {
		/*
		 * TODO: Test a value that validates full-width writes and the
		 * width of the counters.
		 */
		const uint64_t test_val = 0xffff;
		const uint32_t msr = base_msr + i;

		/*
		 * Fixed counters are supported if the counter is less than the
		 * number of enumerated contiguous counters *or* the counter is
		 * explicitly enumerated in the supported counters mask.
		 */
		const bool expect_success = i < nr_counters || (or_mask & BIT(i));

		/*
		 * KVM drops writes to MSR_P6_PERFCTR[0|1] if the counters are
		 * unsupported, i.e. doesn't #GP and reads back '0'.
		 */
		const uint64_t expected_val = expect_success ? test_val : 0;
		const bool expect_gp = !expect_success && msr != MSR_P6_PERFCTR0 &&
				       msr != MSR_P6_PERFCTR1;
		uint32_t rdpmc_idx;
		uint8_t vector;
		uint64_t val;

		vector = wrmsr_safe(msr, test_val);
		GUEST_ASSERT_PMC_MSR_ACCESS(WRMSR, msr, expect_gp, vector);

		vector = rdmsr_safe(msr, &val);
		GUEST_ASSERT_PMC_MSR_ACCESS(RDMSR, msr, expect_gp, vector);

		/* On #GP, the result of RDMSR is undefined. */
		if (!expect_gp)
			GUEST_ASSERT_PMC_VALUE(RDMSR, msr, val, expected_val);

		/*
		 * Redo the read tests with RDPMC, which has different indexing
		 * semantics and additional capabilities.
		 */
		rdpmc_idx = i;
		if (base_msr == MSR_CORE_PERF_FIXED_CTR0)
			rdpmc_idx |= INTEL_RDPMC_FIXED;

		guest_test_rdpmc(rdpmc_idx, expect_success, expected_val);

		/*
		 * KVM doesn't support non-architectural PMUs, i.e. it should
		 * impossible to have fast mode RDPMC.  Verify that attempting
		 * to use fast RDPMC always #GPs.
		 */
		GUEST_ASSERT(!expect_success || !pmu_has_fast_mode);
		rdpmc_idx |= INTEL_RDPMC_FAST;
		guest_test_rdpmc(rdpmc_idx, false, -1ull);

		vector = wrmsr_safe(msr, 0);
		GUEST_ASSERT_PMC_MSR_ACCESS(WRMSR, msr, expect_gp, vector);
	}
}

static void guest_test_gp_counters(void)
{
	uint8_t pmu_version = guest_get_pmu_version();
	uint8_t nr_gp_counters = 0;
	uint32_t base_msr;

	if (pmu_version)
		nr_gp_counters = this_cpu_property(X86_PROPERTY_PMU_NR_GP_COUNTERS);

	/*
	 * For v2+ PMUs, PERF_GLOBAL_CTRL's architectural post-RESET value is
	 * "Sets bits n-1:0 and clears the upper bits", where 'n' is the number
	 * of GP counters.  If there are no GP counters, require KVM to leave
	 * PERF_GLOBAL_CTRL '0'.  This edge case isn't covered by the SDM, but
	 * follow the spirit of the architecture and only globally enable GP
	 * counters, of which there are none.
	 */
	if (pmu_version > 1) {
		uint64_t global_ctrl = rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);

		if (nr_gp_counters)
			GUEST_ASSERT_EQ(global_ctrl, GENMASK_ULL(nr_gp_counters - 1, 0));
		else
			GUEST_ASSERT_EQ(global_ctrl, 0);
	}

	if (this_cpu_has(X86_FEATURE_PDCM) &&
	    rdmsr(MSR_IA32_PERF_CAPABILITIES) & PMU_CAP_FW_WRITES)
		base_msr = MSR_IA32_PMC0;
	else
		base_msr = MSR_IA32_PERFCTR0;

	guest_rd_wr_counters(base_msr, MAX_NR_GP_COUNTERS, nr_gp_counters, 0);
	GUEST_DONE();
}

static void test_gp_counters(uint8_t pmu_version, uint64_t perf_capabilities,
			     uint8_t nr_gp_counters)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = pmu_vm_create_with_one_vcpu(&vcpu, guest_test_gp_counters,
					 pmu_version, perf_capabilities);

	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_PMU_NR_GP_COUNTERS,
				nr_gp_counters);

	run_vcpu(vcpu);

	kvm_vm_free(vm);
}

static void guest_test_fixed_counters(void)
{
	uint64_t supported_bitmask = 0;
	uint8_t nr_fixed_counters = 0;
	uint8_t i;

	/* Fixed counters require Architectural vPMU Version 2+. */
	if (guest_get_pmu_version() >= 2)
		nr_fixed_counters = this_cpu_property(X86_PROPERTY_PMU_NR_FIXED_COUNTERS);

	/*
	 * The supported bitmask for fixed counters was introduced in PMU
	 * version 5.
	 */
	if (guest_get_pmu_version() >= 5)
		supported_bitmask = this_cpu_property(X86_PROPERTY_PMU_FIXED_COUNTERS_BITMASK);

	guest_rd_wr_counters(MSR_CORE_PERF_FIXED_CTR0, MAX_NR_FIXED_COUNTERS,
			     nr_fixed_counters, supported_bitmask);

	for (i = 0; i < MAX_NR_FIXED_COUNTERS; i++) {
		uint8_t vector;
		uint64_t val;

		if (i >= nr_fixed_counters && !(supported_bitmask & BIT_ULL(i))) {
			vector = wrmsr_safe(MSR_CORE_PERF_FIXED_CTR_CTRL,
					    FIXED_PMC_CTRL(i, FIXED_PMC_KERNEL));
			__GUEST_ASSERT(vector == GP_VECTOR,
				       "Expected #GP for counter %u in FIXED_CTR_CTRL", i);

			vector = wrmsr_safe(MSR_CORE_PERF_GLOBAL_CTRL,
					    FIXED_PMC_GLOBAL_CTRL_ENABLE(i));
			__GUEST_ASSERT(vector == GP_VECTOR,
				       "Expected #GP for counter %u in PERF_GLOBAL_CTRL", i);
			continue;
		}

		wrmsr(MSR_CORE_PERF_FIXED_CTR0 + i, 0);
		wrmsr(MSR_CORE_PERF_FIXED_CTR_CTRL, FIXED_PMC_CTRL(i, FIXED_PMC_KERNEL));
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, FIXED_PMC_GLOBAL_CTRL_ENABLE(i));
		__asm__ __volatile__("loop ." : "+c"((int){NUM_LOOPS}));
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
		val = rdmsr(MSR_CORE_PERF_FIXED_CTR0 + i);

		GUEST_ASSERT_NE(val, 0);
	}
	GUEST_DONE();
}

static void test_fixed_counters(uint8_t pmu_version, uint64_t perf_capabilities,
				uint8_t nr_fixed_counters,
				uint32_t supported_bitmask)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = pmu_vm_create_with_one_vcpu(&vcpu, guest_test_fixed_counters,
					 pmu_version, perf_capabilities);

	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_PMU_FIXED_COUNTERS_BITMASK,
				supported_bitmask);
	vcpu_set_cpuid_property(vcpu, X86_PROPERTY_PMU_NR_FIXED_COUNTERS,
				nr_fixed_counters);

	run_vcpu(vcpu);

	kvm_vm_free(vm);
}

static void test_intel_counters(void)
{
	uint8_t nr_fixed_counters = kvm_cpu_property(X86_PROPERTY_PMU_NR_FIXED_COUNTERS);
	uint8_t nr_gp_counters = kvm_cpu_property(X86_PROPERTY_PMU_NR_GP_COUNTERS);
	uint8_t pmu_version = kvm_cpu_property(X86_PROPERTY_PMU_VERSION);
	unsigned int i;
	uint8_t v, j;
	uint32_t k;

	const uint64_t perf_caps[] = {
		0,
		PMU_CAP_FW_WRITES,
	};

	/*
	 * Test up to PMU v5, which is the current maximum version defined by
	 * Intel, i.e. is the last version that is guaranteed to be backwards
	 * compatible with KVM's existing behavior.
	 */
	uint8_t max_pmu_version = max_t(typeof(pmu_version), pmu_version, 5);

	/*
	 * Detect the existence of events that aren't supported by selftests.
	 * This will (obviously) fail any time hardware adds support for a new
	 * event, but it's worth paying that price to keep the test fresh.
	 */
	TEST_ASSERT(this_cpu_property(X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH) <= NR_INTEL_ARCH_EVENTS,
		    "New architectural event(s) detected; please update this test (length = %u, mask = %x)",
		    this_cpu_property(X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH),
		    this_cpu_property(X86_PROPERTY_PMU_EVENTS_MASK));

	/*
	 * Iterate over known arch events irrespective of KVM/hardware support
	 * to verify that KVM doesn't reject programming of events just because
	 * the *architectural* encoding is unsupported.  Track which events are
	 * supported in hardware; the guest side will validate supported events
	 * count correctly, even if *enumeration* of the event is unsupported
	 * by KVM and/or isn't exposed to the guest.
	 */
	for (i = 0; i < NR_INTEL_ARCH_EVENTS; i++) {
		if (this_pmu_has(intel_event_to_feature(i).gp_event))
			hardware_pmu_arch_events |= BIT(i);
	}

	for (v = 0; v <= max_pmu_version; v++) {
		for (i = 0; i < ARRAY_SIZE(perf_caps); i++) {
			if (!kvm_has_perf_caps && perf_caps[i])
				continue;

			pr_info("Testing arch events, PMU version %u, perf_caps = %lx\n",
				v, perf_caps[i]);
			/*
			 * To keep the total runtime reasonable, test every
			 * possible non-zero, non-reserved bitmap combination
			 * only with the native PMU version and the full bit
			 * vector length.
			 */
			if (v == pmu_version) {
				for (k = 1; k < (BIT(NR_INTEL_ARCH_EVENTS) - 1); k++)
					test_arch_events(v, perf_caps[i], NR_INTEL_ARCH_EVENTS, k);
			}
			/*
			 * Test single bits for all PMU version and lengths up
			 * the number of events +1 (to verify KVM doesn't do
			 * weird things if the guest length is greater than the
			 * host length).  Explicitly test a mask of '0' and all
			 * ones i.e. all events being available and unavailable.
			 */
			for (j = 0; j <= NR_INTEL_ARCH_EVENTS + 1; j++) {
				test_arch_events(v, perf_caps[i], j, 0);
				test_arch_events(v, perf_caps[i], j, 0xff);

				for (k = 0; k < NR_INTEL_ARCH_EVENTS; k++)
					test_arch_events(v, perf_caps[i], j, BIT(k));
			}

			pr_info("Testing GP counters, PMU version %u, perf_caps = %lx\n",
				v, perf_caps[i]);
			for (j = 0; j <= nr_gp_counters; j++)
				test_gp_counters(v, perf_caps[i], j);

			pr_info("Testing fixed counters, PMU version %u, perf_caps = %lx\n",
				v, perf_caps[i]);
			for (j = 0; j <= nr_fixed_counters; j++) {
				for (k = 0; k <= (BIT(nr_fixed_counters) - 1); k++)
					test_fixed_counters(v, perf_caps[i], j, k);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_is_pmu_enabled());

	TEST_REQUIRE(host_cpu_is_intel);
	TEST_REQUIRE(kvm_cpu_has_p(X86_PROPERTY_PMU_VERSION));
	TEST_REQUIRE(kvm_cpu_property(X86_PROPERTY_PMU_VERSION) > 0);

	kvm_pmu_version = kvm_cpu_property(X86_PROPERTY_PMU_VERSION);
	kvm_has_perf_caps = kvm_cpu_has(X86_FEATURE_PDCM);

	test_intel_counters();

	return 0;
}
