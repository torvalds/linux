// SPDX-License-Identifier: GPL-2.0
/*
 * Test for x86 KVM_SET_PMU_EVENT_FILTER.
 *
 * Copyright (C) 2022, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Verifies the expected behavior of allow lists and deny lists for
 * virtual PMU events.
 */
#include "kvm_util.h"
#include "pmu.h"
#include "processor.h"
#include "test_util.h"

#define NUM_BRANCHES 42
#define MAX_TEST_EVENTS		10

#define PMU_EVENT_FILTER_INVALID_ACTION		(KVM_PMU_EVENT_DENY + 1)
#define PMU_EVENT_FILTER_INVALID_FLAGS			(KVM_PMU_EVENT_FLAGS_VALID_MASK << 1)
#define PMU_EVENT_FILTER_INVALID_NEVENTS		(KVM_PMU_EVENT_FILTER_MAX_EVENTS + 1)

struct __kvm_pmu_event_filter {
	__u32 action;
	__u32 nevents;
	__u32 fixed_counter_bitmap;
	__u32 flags;
	__u32 pad[4];
	__u64 events[KVM_PMU_EVENT_FILTER_MAX_EVENTS];
};

/*
 * This event list comprises Intel's known architectural events, plus AMD's
 * "retired branch instructions" for Zen1-Zen3 (and* possibly other AMD CPUs).
 * Note, AMD and Intel use the same encoding for instructions retired.
 */
kvm_static_assert(INTEL_ARCH_INSTRUCTIONS_RETIRED == AMD_ZEN_INSTRUCTIONS_RETIRED);

static const struct __kvm_pmu_event_filter base_event_filter = {
	.nevents = ARRAY_SIZE(base_event_filter.events),
	.events = {
		INTEL_ARCH_CPU_CYCLES,
		INTEL_ARCH_INSTRUCTIONS_RETIRED,
		INTEL_ARCH_REFERENCE_CYCLES,
		INTEL_ARCH_LLC_REFERENCES,
		INTEL_ARCH_LLC_MISSES,
		INTEL_ARCH_BRANCHES_RETIRED,
		INTEL_ARCH_BRANCHES_MISPREDICTED,
		INTEL_ARCH_TOPDOWN_SLOTS,
		AMD_ZEN_BRANCHES_RETIRED,
	},
};

struct {
	uint64_t loads;
	uint64_t stores;
	uint64_t loads_stores;
	uint64_t branches_retired;
	uint64_t instructions_retired;
} pmc_results;

/*
 * If we encounter a #GP during the guest PMU sanity check, then the guest
 * PMU is not functional. Inform the hypervisor via GUEST_SYNC(0).
 */
static void guest_gp_handler(struct ex_regs *regs)
{
	GUEST_SYNC(-EFAULT);
}

/*
 * Check that we can write a new value to the given MSR and read it back.
 * The caller should provide a non-empty set of bits that are safe to flip.
 *
 * Return on success. GUEST_SYNC(0) on error.
 */
static void check_msr(uint32_t msr, uint64_t bits_to_flip)
{
	uint64_t v = rdmsr(msr) ^ bits_to_flip;

	wrmsr(msr, v);
	if (rdmsr(msr) != v)
		GUEST_SYNC(-EIO);

	v ^= bits_to_flip;
	wrmsr(msr, v);
	if (rdmsr(msr) != v)
		GUEST_SYNC(-EIO);
}

static void run_and_measure_loop(uint32_t msr_base)
{
	const uint64_t branches_retired = rdmsr(msr_base + 0);
	const uint64_t insn_retired = rdmsr(msr_base + 1);

	__asm__ __volatile__("loop ." : "+c"((int){NUM_BRANCHES}));

	pmc_results.branches_retired = rdmsr(msr_base + 0) - branches_retired;
	pmc_results.instructions_retired = rdmsr(msr_base + 1) - insn_retired;
}

static void intel_guest_code(void)
{
	check_msr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
	check_msr(MSR_P6_EVNTSEL0, 0xffff);
	check_msr(MSR_IA32_PMC0, 0xffff);
	GUEST_SYNC(0);

	for (;;) {
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
		wrmsr(MSR_P6_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | INTEL_ARCH_BRANCHES_RETIRED);
		wrmsr(MSR_P6_EVNTSEL1, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | INTEL_ARCH_INSTRUCTIONS_RETIRED);
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x3);

		run_and_measure_loop(MSR_IA32_PMC0);
		GUEST_SYNC(0);
	}
}

/*
 * To avoid needing a check for CPUID.80000001:ECX.PerfCtrExtCore[bit 23],
 * this code uses the always-available, legacy K7 PMU MSRs, which alias to
 * the first four of the six extended core PMU MSRs.
 */
static void amd_guest_code(void)
{
	check_msr(MSR_K7_EVNTSEL0, 0xffff);
	check_msr(MSR_K7_PERFCTR0, 0xffff);
	GUEST_SYNC(0);

	for (;;) {
		wrmsr(MSR_K7_EVNTSEL0, 0);
		wrmsr(MSR_K7_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | AMD_ZEN_BRANCHES_RETIRED);
		wrmsr(MSR_K7_EVNTSEL1, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | AMD_ZEN_INSTRUCTIONS_RETIRED);

		run_and_measure_loop(MSR_K7_PERFCTR0);
		GUEST_SYNC(0);
	}
}

/*
 * Run the VM to the next GUEST_SYNC(value), and return the value passed
 * to the sync. Any other exit from the guest is fatal.
 */
static uint64_t run_vcpu_to_sync(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	get_ucall(vcpu, &uc);
	TEST_ASSERT(uc.cmd == UCALL_SYNC,
		    "Received ucall other than UCALL_SYNC: %lu", uc.cmd);
	return uc.args[1];
}

static void run_vcpu_and_sync_pmc_results(struct kvm_vcpu *vcpu)
{
	uint64_t r;

	memset(&pmc_results, 0, sizeof(pmc_results));
	sync_global_to_guest(vcpu->vm, pmc_results);

	r = run_vcpu_to_sync(vcpu);
	TEST_ASSERT(!r, "Unexpected sync value: 0x%lx", r);

	sync_global_from_guest(vcpu->vm, pmc_results);
}

/*
 * In a nested environment or if the vPMU is disabled, the guest PMU
 * might not work as architected (accessing the PMU MSRs may raise
 * #GP, or writes could simply be discarded). In those situations,
 * there is no point in running these tests. The guest code will perform
 * a sanity check and then GUEST_SYNC(success). In the case of failure,
 * the behavior of the guest on resumption is undefined.
 */
static bool sanity_check_pmu(struct kvm_vcpu *vcpu)
{
	uint64_t r;

	vm_install_exception_handler(vcpu->vm, GP_VECTOR, guest_gp_handler);
	r = run_vcpu_to_sync(vcpu);
	vm_install_exception_handler(vcpu->vm, GP_VECTOR, NULL);

	return !r;
}

/*
 * Remove the first occurrence of 'event' (if any) from the filter's
 * event list.
 */
static void remove_event(struct __kvm_pmu_event_filter *f, uint64_t event)
{
	bool found = false;
	int i;

	for (i = 0; i < f->nevents; i++) {
		if (found)
			f->events[i - 1] = f->events[i];
		else
			found = f->events[i] == event;
	}
	if (found)
		f->nevents--;
}

#define ASSERT_PMC_COUNTING_INSTRUCTIONS()						\
do {											\
	uint64_t br = pmc_results.branches_retired;					\
	uint64_t ir = pmc_results.instructions_retired;					\
											\
	if (br && br != NUM_BRANCHES)							\
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",	\
			__func__, br, NUM_BRANCHES);					\
	TEST_ASSERT(br, "%s: Branch instructions retired = %lu (expected > 0)",		\
		    __func__, br);							\
	TEST_ASSERT(ir,	"%s: Instructions retired = %lu (expected > 0)",		\
		    __func__, ir);							\
} while (0)

#define ASSERT_PMC_NOT_COUNTING_INSTRUCTIONS()						\
do {											\
	uint64_t br = pmc_results.branches_retired;					\
	uint64_t ir = pmc_results.instructions_retired;					\
											\
	TEST_ASSERT(!br, "%s: Branch instructions retired = %lu (expected 0)",		\
		    __func__, br);							\
	TEST_ASSERT(!ir, "%s: Instructions retired = %lu (expected 0)",			\
		    __func__, ir);							\
} while (0)

static void test_without_filter(struct kvm_vcpu *vcpu)
{
	run_vcpu_and_sync_pmc_results(vcpu);

	ASSERT_PMC_COUNTING_INSTRUCTIONS();
}

static void test_with_filter(struct kvm_vcpu *vcpu,
			     struct __kvm_pmu_event_filter *__f)
{
	struct kvm_pmu_event_filter *f = (void *)__f;

	vm_ioctl(vcpu->vm, KVM_SET_PMU_EVENT_FILTER, f);
	run_vcpu_and_sync_pmc_results(vcpu);
}

static void test_amd_deny_list(struct kvm_vcpu *vcpu)
{
	struct __kvm_pmu_event_filter f = {
		.action = KVM_PMU_EVENT_DENY,
		.nevents = 1,
		.events = {
			RAW_EVENT(0x1C2, 0),
		},
	};

	test_with_filter(vcpu, &f);

	ASSERT_PMC_COUNTING_INSTRUCTIONS();
}

static void test_member_deny_list(struct kvm_vcpu *vcpu)
{
	struct __kvm_pmu_event_filter f = base_event_filter;

	f.action = KVM_PMU_EVENT_DENY;
	test_with_filter(vcpu, &f);

	ASSERT_PMC_NOT_COUNTING_INSTRUCTIONS();
}

static void test_member_allow_list(struct kvm_vcpu *vcpu)
{
	struct __kvm_pmu_event_filter f = base_event_filter;

	f.action = KVM_PMU_EVENT_ALLOW;
	test_with_filter(vcpu, &f);

	ASSERT_PMC_COUNTING_INSTRUCTIONS();
}

static void test_not_member_deny_list(struct kvm_vcpu *vcpu)
{
	struct __kvm_pmu_event_filter f = base_event_filter;

	f.action = KVM_PMU_EVENT_DENY;

	remove_event(&f, INTEL_ARCH_INSTRUCTIONS_RETIRED);
	remove_event(&f, INTEL_ARCH_BRANCHES_RETIRED);
	remove_event(&f, AMD_ZEN_BRANCHES_RETIRED);
	test_with_filter(vcpu, &f);

	ASSERT_PMC_COUNTING_INSTRUCTIONS();
}

static void test_not_member_allow_list(struct kvm_vcpu *vcpu)
{
	struct __kvm_pmu_event_filter f = base_event_filter;

	f.action = KVM_PMU_EVENT_ALLOW;

	remove_event(&f, INTEL_ARCH_INSTRUCTIONS_RETIRED);
	remove_event(&f, INTEL_ARCH_BRANCHES_RETIRED);
	remove_event(&f, AMD_ZEN_BRANCHES_RETIRED);
	test_with_filter(vcpu, &f);

	ASSERT_PMC_NOT_COUNTING_INSTRUCTIONS();
}

/*
 * Verify that setting KVM_PMU_CAP_DISABLE prevents the use of the PMU.
 *
 * Note that KVM_CAP_PMU_CAPABILITY must be invoked prior to creating VCPUs.
 */
static void test_pmu_config_disable(void (*guest_code)(void))
{
	struct kvm_vcpu *vcpu;
	int r;
	struct kvm_vm *vm;

	r = kvm_check_cap(KVM_CAP_PMU_CAPABILITY);
	if (!(r & KVM_PMU_CAP_DISABLE))
		return;

	vm = vm_create(1);

	vm_enable_cap(vm, KVM_CAP_PMU_CAPABILITY, KVM_PMU_CAP_DISABLE);

	vcpu = vm_vcpu_add(vm, 0, guest_code);
	TEST_ASSERT(!sanity_check_pmu(vcpu),
		    "Guest should not be able to use disabled PMU.");

	kvm_vm_free(vm);
}

/*
 * On Intel, check for a non-zero PMU version, at least one general-purpose
 * counter per logical processor, and support for counting the number of branch
 * instructions retired.
 */
static bool use_intel_pmu(void)
{
	return host_cpu_is_intel &&
	       kvm_cpu_property(X86_PROPERTY_PMU_VERSION) &&
	       kvm_cpu_property(X86_PROPERTY_PMU_NR_GP_COUNTERS) &&
	       kvm_pmu_has(X86_PMU_FEATURE_BRANCH_INSNS_RETIRED);
}

static bool is_zen1(uint32_t family, uint32_t model)
{
	return family == 0x17 && model <= 0x0f;
}

static bool is_zen2(uint32_t family, uint32_t model)
{
	return family == 0x17 && model >= 0x30 && model <= 0x3f;
}

static bool is_zen3(uint32_t family, uint32_t model)
{
	return family == 0x19 && model <= 0x0f;
}

/*
 * Determining AMD support for a PMU event requires consulting the AMD
 * PPR for the CPU or reference material derived therefrom. The AMD
 * test code herein has been verified to work on Zen1, Zen2, and Zen3.
 *
 * Feel free to add more AMD CPUs that are documented to support event
 * select 0xc2 umask 0 as "retired branch instructions."
 */
static bool use_amd_pmu(void)
{
	uint32_t family = kvm_cpu_family();
	uint32_t model = kvm_cpu_model();

	return host_cpu_is_amd &&
		(is_zen1(family, model) ||
		 is_zen2(family, model) ||
		 is_zen3(family, model));
}

/*
 * "MEM_INST_RETIRED.ALL_LOADS", "MEM_INST_RETIRED.ALL_STORES", and
 * "MEM_INST_RETIRED.ANY" from https://perfmon-events.intel.com/
 * supported on Intel Xeon processors:
 *  - Sapphire Rapids, Ice Lake, Cascade Lake, Skylake.
 */
#define MEM_INST_RETIRED		0xD0
#define MEM_INST_RETIRED_LOAD		RAW_EVENT(MEM_INST_RETIRED, 0x81)
#define MEM_INST_RETIRED_STORE		RAW_EVENT(MEM_INST_RETIRED, 0x82)
#define MEM_INST_RETIRED_LOAD_STORE	RAW_EVENT(MEM_INST_RETIRED, 0x83)

static bool supports_event_mem_inst_retired(void)
{
	uint32_t eax, ebx, ecx, edx;

	cpuid(1, &eax, &ebx, &ecx, &edx);
	if (x86_family(eax) == 0x6) {
		switch (x86_model(eax)) {
		/* Sapphire Rapids */
		case 0x8F:
		/* Ice Lake */
		case 0x6A:
		/* Skylake */
		/* Cascade Lake */
		case 0x55:
			return true;
		}
	}

	return false;
}

/*
 * "LS Dispatch", from Processor Programming Reference
 * (PPR) for AMD Family 17h Model 01h, Revision B1 Processors,
 * Preliminary Processor Programming Reference (PPR) for AMD Family
 * 17h Model 31h, Revision B0 Processors, and Preliminary Processor
 * Programming Reference (PPR) for AMD Family 19h Model 01h, Revision
 * B1 Processors Volume 1 of 2.
 */
#define LS_DISPATCH		0x29
#define LS_DISPATCH_LOAD	RAW_EVENT(LS_DISPATCH, BIT(0))
#define LS_DISPATCH_STORE	RAW_EVENT(LS_DISPATCH, BIT(1))
#define LS_DISPATCH_LOAD_STORE	RAW_EVENT(LS_DISPATCH, BIT(2))

#define INCLUDE_MASKED_ENTRY(event_select, mask, match) \
	KVM_PMU_ENCODE_MASKED_ENTRY(event_select, mask, match, false)
#define EXCLUDE_MASKED_ENTRY(event_select, mask, match) \
	KVM_PMU_ENCODE_MASKED_ENTRY(event_select, mask, match, true)

static void masked_events_guest_test(uint32_t msr_base)
{
	/*
	 * The actual value of the counters don't determine the outcome of
	 * the test.  Only that they are zero or non-zero.
	 */
	const uint64_t loads = rdmsr(msr_base + 0);
	const uint64_t stores = rdmsr(msr_base + 1);
	const uint64_t loads_stores = rdmsr(msr_base + 2);
	int val;


	__asm__ __volatile__("movl $0, %[v];"
			     "movl %[v], %%eax;"
			     "incl %[v];"
			     : [v]"+m"(val) :: "eax");

	pmc_results.loads = rdmsr(msr_base + 0) - loads;
	pmc_results.stores = rdmsr(msr_base + 1) - stores;
	pmc_results.loads_stores = rdmsr(msr_base + 2) - loads_stores;
}

static void intel_masked_events_guest_code(void)
{
	for (;;) {
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);

		wrmsr(MSR_P6_EVNTSEL0 + 0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | MEM_INST_RETIRED_LOAD);
		wrmsr(MSR_P6_EVNTSEL0 + 1, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | MEM_INST_RETIRED_STORE);
		wrmsr(MSR_P6_EVNTSEL0 + 2, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | MEM_INST_RETIRED_LOAD_STORE);

		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x7);

		masked_events_guest_test(MSR_IA32_PMC0);
		GUEST_SYNC(0);
	}
}

static void amd_masked_events_guest_code(void)
{
	for (;;) {
		wrmsr(MSR_K7_EVNTSEL0, 0);
		wrmsr(MSR_K7_EVNTSEL1, 0);
		wrmsr(MSR_K7_EVNTSEL2, 0);

		wrmsr(MSR_K7_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | LS_DISPATCH_LOAD);
		wrmsr(MSR_K7_EVNTSEL1, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | LS_DISPATCH_STORE);
		wrmsr(MSR_K7_EVNTSEL2, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | LS_DISPATCH_LOAD_STORE);

		masked_events_guest_test(MSR_K7_PERFCTR0);
		GUEST_SYNC(0);
	}
}

static void run_masked_events_test(struct kvm_vcpu *vcpu,
				   const uint64_t masked_events[],
				   const int nmasked_events)
{
	struct __kvm_pmu_event_filter f = {
		.nevents = nmasked_events,
		.action = KVM_PMU_EVENT_ALLOW,
		.flags = KVM_PMU_EVENT_FLAG_MASKED_EVENTS,
	};

	memcpy(f.events, masked_events, sizeof(uint64_t) * nmasked_events);
	test_with_filter(vcpu, &f);
}

#define ALLOW_LOADS		BIT(0)
#define ALLOW_STORES		BIT(1)
#define ALLOW_LOADS_STORES	BIT(2)

struct masked_events_test {
	uint64_t intel_events[MAX_TEST_EVENTS];
	uint64_t intel_event_end;
	uint64_t amd_events[MAX_TEST_EVENTS];
	uint64_t amd_event_end;
	const char *msg;
	uint32_t flags;
};

/*
 * These are the test cases for the masked events tests.
 *
 * For each test, the guest enables 3 PMU counters (loads, stores,
 * loads + stores).  The filter is then set in KVM with the masked events
 * provided.  The test then verifies that the counters agree with which
 * ones should be counting and which ones should be filtered.
 */
const struct masked_events_test test_cases[] = {
	{
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFF, 0x81),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xFF, BIT(0)),
		},
		.msg = "Only allow loads.",
		.flags = ALLOW_LOADS,
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFF, 0x82),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xFF, BIT(1)),
		},
		.msg = "Only allow stores.",
		.flags = ALLOW_STORES,
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFF, 0x83),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xFF, BIT(2)),
		},
		.msg = "Only allow loads + stores.",
		.flags = ALLOW_LOADS_STORES,
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0x7C, 0),
			EXCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFF, 0x83),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, ~(BIT(0) | BIT(1)), 0),
		},
		.msg = "Only allow loads and stores.",
		.flags = ALLOW_LOADS | ALLOW_STORES,
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0x7C, 0),
			EXCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFF, 0x82),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xF8, 0),
			EXCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xFF, BIT(1)),
		},
		.msg = "Only allow loads and loads + stores.",
		.flags = ALLOW_LOADS | ALLOW_LOADS_STORES
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0xFE, 0x82),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xF8, 0),
			EXCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xFF, BIT(0)),
		},
		.msg = "Only allow stores and loads + stores.",
		.flags = ALLOW_STORES | ALLOW_LOADS_STORES
	}, {
		.intel_events = {
			INCLUDE_MASKED_ENTRY(MEM_INST_RETIRED, 0x7C, 0),
		},
		.amd_events = {
			INCLUDE_MASKED_ENTRY(LS_DISPATCH, 0xF8, 0),
		},
		.msg = "Only allow loads, stores, and loads + stores.",
		.flags = ALLOW_LOADS | ALLOW_STORES | ALLOW_LOADS_STORES
	},
};

static int append_test_events(const struct masked_events_test *test,
			      uint64_t *events, int nevents)
{
	const uint64_t *evts;
	int i;

	evts = use_intel_pmu() ? test->intel_events : test->amd_events;
	for (i = 0; i < MAX_TEST_EVENTS; i++) {
		if (evts[i] == 0)
			break;

		events[nevents + i] = evts[i];
	}

	return nevents + i;
}

static bool bool_eq(bool a, bool b)
{
	return a == b;
}

static void run_masked_events_tests(struct kvm_vcpu *vcpu, uint64_t *events,
				    int nevents)
{
	int ntests = ARRAY_SIZE(test_cases);
	int i, n;

	for (i = 0; i < ntests; i++) {
		const struct masked_events_test *test = &test_cases[i];

		/* Do any test case events overflow MAX_TEST_EVENTS? */
		assert(test->intel_event_end == 0);
		assert(test->amd_event_end == 0);

		n = append_test_events(test, events, nevents);

		run_masked_events_test(vcpu, events, n);

		TEST_ASSERT(bool_eq(pmc_results.loads, test->flags & ALLOW_LOADS) &&
			    bool_eq(pmc_results.stores, test->flags & ALLOW_STORES) &&
			    bool_eq(pmc_results.loads_stores,
				    test->flags & ALLOW_LOADS_STORES),
			    "%s  loads: %lu, stores: %lu, loads + stores: %lu",
			    test->msg, pmc_results.loads, pmc_results.stores,
			    pmc_results.loads_stores);
	}
}

static void add_dummy_events(uint64_t *events, int nevents)
{
	int i;

	for (i = 0; i < nevents; i++) {
		int event_select = i % 0xFF;
		bool exclude = ((i % 4) == 0);

		if (event_select == MEM_INST_RETIRED ||
		    event_select == LS_DISPATCH)
			event_select++;

		events[i] = KVM_PMU_ENCODE_MASKED_ENTRY(event_select, 0,
							0, exclude);
	}
}

static void test_masked_events(struct kvm_vcpu *vcpu)
{
	int nevents = KVM_PMU_EVENT_FILTER_MAX_EVENTS - MAX_TEST_EVENTS;
	uint64_t events[KVM_PMU_EVENT_FILTER_MAX_EVENTS];

	/* Run the test cases against a sparse PMU event filter. */
	run_masked_events_tests(vcpu, events, 0);

	/* Run the test cases against a dense PMU event filter. */
	add_dummy_events(events, KVM_PMU_EVENT_FILTER_MAX_EVENTS);
	run_masked_events_tests(vcpu, events, nevents);
}

static int set_pmu_event_filter(struct kvm_vcpu *vcpu,
				struct __kvm_pmu_event_filter *__f)
{
	struct kvm_pmu_event_filter *f = (void *)__f;

	return __vm_ioctl(vcpu->vm, KVM_SET_PMU_EVENT_FILTER, f);
}

static int set_pmu_single_event_filter(struct kvm_vcpu *vcpu, uint64_t event,
				       uint32_t flags, uint32_t action)
{
	struct __kvm_pmu_event_filter f = {
		.nevents = 1,
		.flags = flags,
		.action = action,
		.events = {
			event,
		},
	};

	return set_pmu_event_filter(vcpu, &f);
}

static void test_filter_ioctl(struct kvm_vcpu *vcpu)
{
	uint8_t nr_fixed_counters = kvm_cpu_property(X86_PROPERTY_PMU_NR_FIXED_COUNTERS);
	struct __kvm_pmu_event_filter f;
	uint64_t e = ~0ul;
	int r;

	/*
	 * Unfortunately having invalid bits set in event data is expected to
	 * pass when flags == 0 (bits other than eventsel+umask).
	 */
	r = set_pmu_single_event_filter(vcpu, e, 0, KVM_PMU_EVENT_ALLOW);
	TEST_ASSERT(r == 0, "Valid PMU Event Filter is failing");

	r = set_pmu_single_event_filter(vcpu, e,
					KVM_PMU_EVENT_FLAG_MASKED_EVENTS,
					KVM_PMU_EVENT_ALLOW);
	TEST_ASSERT(r != 0, "Invalid PMU Event Filter is expected to fail");

	e = KVM_PMU_ENCODE_MASKED_ENTRY(0xff, 0xff, 0xff, 0xf);
	r = set_pmu_single_event_filter(vcpu, e,
					KVM_PMU_EVENT_FLAG_MASKED_EVENTS,
					KVM_PMU_EVENT_ALLOW);
	TEST_ASSERT(r == 0, "Valid PMU Event Filter is failing");

	f = base_event_filter;
	f.action = PMU_EVENT_FILTER_INVALID_ACTION;
	r = set_pmu_event_filter(vcpu, &f);
	TEST_ASSERT(r, "Set invalid action is expected to fail");

	f = base_event_filter;
	f.flags = PMU_EVENT_FILTER_INVALID_FLAGS;
	r = set_pmu_event_filter(vcpu, &f);
	TEST_ASSERT(r, "Set invalid flags is expected to fail");

	f = base_event_filter;
	f.nevents = PMU_EVENT_FILTER_INVALID_NEVENTS;
	r = set_pmu_event_filter(vcpu, &f);
	TEST_ASSERT(r, "Exceeding the max number of filter events should fail");

	f = base_event_filter;
	f.fixed_counter_bitmap = ~GENMASK_ULL(nr_fixed_counters, 0);
	r = set_pmu_event_filter(vcpu, &f);
	TEST_ASSERT(!r, "Masking non-existent fixed counters should be allowed");
}

static void intel_run_fixed_counter_guest_code(uint8_t idx)
{
	for (;;) {
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
		wrmsr(MSR_CORE_PERF_FIXED_CTR0 + idx, 0);

		/* Only OS_EN bit is enabled for fixed counter[idx]. */
		wrmsr(MSR_CORE_PERF_FIXED_CTR_CTRL, FIXED_PMC_CTRL(idx, FIXED_PMC_KERNEL));
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, FIXED_PMC_GLOBAL_CTRL_ENABLE(idx));
		__asm__ __volatile__("loop ." : "+c"((int){NUM_BRANCHES}));
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);

		GUEST_SYNC(rdmsr(MSR_CORE_PERF_FIXED_CTR0 + idx));
	}
}

static uint64_t test_with_fixed_counter_filter(struct kvm_vcpu *vcpu,
					       uint32_t action, uint32_t bitmap)
{
	struct __kvm_pmu_event_filter f = {
		.action = action,
		.fixed_counter_bitmap = bitmap,
	};
	set_pmu_event_filter(vcpu, &f);

	return run_vcpu_to_sync(vcpu);
}

static uint64_t test_set_gp_and_fixed_event_filter(struct kvm_vcpu *vcpu,
						   uint32_t action,
						   uint32_t bitmap)
{
	struct __kvm_pmu_event_filter f = base_event_filter;

	f.action = action;
	f.fixed_counter_bitmap = bitmap;
	set_pmu_event_filter(vcpu, &f);

	return run_vcpu_to_sync(vcpu);
}

static void __test_fixed_counter_bitmap(struct kvm_vcpu *vcpu, uint8_t idx,
					uint8_t nr_fixed_counters)
{
	unsigned int i;
	uint32_t bitmap;
	uint64_t count;

	TEST_ASSERT(nr_fixed_counters < sizeof(bitmap) * 8,
		    "Invalid nr_fixed_counters");

	/*
	 * Check the fixed performance counter can count normally when KVM
	 * userspace doesn't set any pmu filter.
	 */
	count = run_vcpu_to_sync(vcpu);
	TEST_ASSERT(count, "Unexpected count value: %ld", count);

	for (i = 0; i < BIT(nr_fixed_counters); i++) {
		bitmap = BIT(i);
		count = test_with_fixed_counter_filter(vcpu, KVM_PMU_EVENT_ALLOW,
						       bitmap);
		TEST_ASSERT_EQ(!!count, !!(bitmap & BIT(idx)));

		count = test_with_fixed_counter_filter(vcpu, KVM_PMU_EVENT_DENY,
						       bitmap);
		TEST_ASSERT_EQ(!!count, !(bitmap & BIT(idx)));

		/*
		 * Check that fixed_counter_bitmap has higher priority than
		 * events[] when both are set.
		 */
		count = test_set_gp_and_fixed_event_filter(vcpu,
							   KVM_PMU_EVENT_ALLOW,
							   bitmap);
		TEST_ASSERT_EQ(!!count, !!(bitmap & BIT(idx)));

		count = test_set_gp_and_fixed_event_filter(vcpu,
							   KVM_PMU_EVENT_DENY,
							   bitmap);
		TEST_ASSERT_EQ(!!count, !(bitmap & BIT(idx)));
	}
}

static void test_fixed_counter_bitmap(void)
{
	uint8_t nr_fixed_counters = kvm_cpu_property(X86_PROPERTY_PMU_NR_FIXED_COUNTERS);
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	uint8_t idx;

	/*
	 * Check that pmu_event_filter works as expected when it's applied to
	 * fixed performance counters.
	 */
	for (idx = 0; idx < nr_fixed_counters; idx++) {
		vm = vm_create_with_one_vcpu(&vcpu,
					     intel_run_fixed_counter_guest_code);
		vcpu_args_set(vcpu, 1, idx);
		__test_fixed_counter_bitmap(vcpu, idx, nr_fixed_counters);
		kvm_vm_free(vm);
	}
}

int main(int argc, char *argv[])
{
	void (*guest_code)(void);
	struct kvm_vcpu *vcpu, *vcpu2 = NULL;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_is_pmu_enabled());
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_PMU_EVENT_FILTER));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_PMU_EVENT_MASKED_EVENTS));

	TEST_REQUIRE(use_intel_pmu() || use_amd_pmu());
	guest_code = use_intel_pmu() ? intel_guest_code : amd_guest_code;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	TEST_REQUIRE(sanity_check_pmu(vcpu));

	if (use_amd_pmu())
		test_amd_deny_list(vcpu);

	test_without_filter(vcpu);
	test_member_deny_list(vcpu);
	test_member_allow_list(vcpu);
	test_not_member_deny_list(vcpu);
	test_not_member_allow_list(vcpu);

	if (use_intel_pmu() &&
	    supports_event_mem_inst_retired() &&
	    kvm_cpu_property(X86_PROPERTY_PMU_NR_GP_COUNTERS) >= 3)
		vcpu2 = vm_vcpu_add(vm, 2, intel_masked_events_guest_code);
	else if (use_amd_pmu())
		vcpu2 = vm_vcpu_add(vm, 2, amd_masked_events_guest_code);

	if (vcpu2)
		test_masked_events(vcpu2);
	test_filter_ioctl(vcpu);

	kvm_vm_free(vm);

	test_pmu_config_disable(guest_code);
	test_fixed_counter_bitmap();

	return 0;
}
