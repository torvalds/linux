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

#define _GNU_SOURCE /* for program_invocation_short_name */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/*
 * In lieu of copying perf_event.h into tools...
 */
#define ARCH_PERFMON_EVENTSEL_OS			(1ULL << 17)
#define ARCH_PERFMON_EVENTSEL_ENABLE			(1ULL << 22)

union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union cpuid10_ebx {
	struct {
		unsigned int no_unhalted_core_cycles:1;
		unsigned int no_instructions_retired:1;
		unsigned int no_unhalted_reference_cycles:1;
		unsigned int no_llc_reference:1;
		unsigned int no_llc_misses:1;
		unsigned int no_branch_instruction_retired:1;
		unsigned int no_branch_misses_retired:1;
	} split;
	unsigned int full;
};

/* End of stuff taken from perf_event.h. */

/* Oddly, this isn't in perf_event.h. */
#define ARCH_PERFMON_BRANCHES_RETIRED		5

#define VCPU_ID 0
#define NUM_BRANCHES 42

/*
 * This is how the event selector and unit mask are stored in an AMD
 * core performance event-select register. Intel's format is similar,
 * but the event selector is only 8 bits.
 */
#define EVENT(select, umask) ((select & 0xf00UL) << 24 | (select & 0xff) | \
			      (umask & 0xff) << 8)

/*
 * "Branch instructions retired", from the Intel SDM, volume 3,
 * "Pre-defined Architectural Performance Events."
 */

#define INTEL_BR_RETIRED EVENT(0xc4, 0)

/*
 * "Retired branch instructions", from Processor Programming Reference
 * (PPR) for AMD Family 17h Model 01h, Revision B1 Processors,
 * Preliminary Processor Programming Reference (PPR) for AMD Family
 * 17h Model 31h, Revision B0 Processors, and Preliminary Processor
 * Programming Reference (PPR) for AMD Family 19h Model 01h, Revision
 * B1 Processors Volume 1 of 2.
 */

#define AMD_ZEN_BR_RETIRED EVENT(0xc2, 0)

/*
 * This event list comprises Intel's eight architectural events plus
 * AMD's "retired branch instructions" for Zen[123] (and possibly
 * other AMD CPUs).
 */
static const uint64_t event_list[] = {
	EVENT(0x3c, 0),
	EVENT(0xc0, 0),
	EVENT(0x3c, 1),
	EVENT(0x2e, 0x4f),
	EVENT(0x2e, 0x41),
	EVENT(0xc4, 0),
	EVENT(0xc5, 0),
	EVENT(0xa4, 1),
	AMD_ZEN_BR_RETIRED,
};

/*
 * If we encounter a #GP during the guest PMU sanity check, then the guest
 * PMU is not functional. Inform the hypervisor via GUEST_SYNC(0).
 */
static void guest_gp_handler(struct ex_regs *regs)
{
	GUEST_SYNC(0);
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
		GUEST_SYNC(0);

	v ^= bits_to_flip;
	wrmsr(msr, v);
	if (rdmsr(msr) != v)
		GUEST_SYNC(0);
}

static void intel_guest_code(void)
{
	check_msr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
	check_msr(MSR_P6_EVNTSEL0, 0xffff);
	check_msr(MSR_IA32_PMC0, 0xffff);
	GUEST_SYNC(1);

	for (;;) {
		uint64_t br0, br1;

		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
		wrmsr(MSR_P6_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | INTEL_BR_RETIRED);
		wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
		br0 = rdmsr(MSR_IA32_PMC0);
		__asm__ __volatile__("loop ." : "+c"((int){NUM_BRANCHES}));
		br1 = rdmsr(MSR_IA32_PMC0);
		GUEST_SYNC(br1 - br0);
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
	GUEST_SYNC(1);

	for (;;) {
		uint64_t br0, br1;

		wrmsr(MSR_K7_EVNTSEL0, 0);
		wrmsr(MSR_K7_EVNTSEL0, ARCH_PERFMON_EVENTSEL_ENABLE |
		      ARCH_PERFMON_EVENTSEL_OS | AMD_ZEN_BR_RETIRED);
		br0 = rdmsr(MSR_K7_PERFCTR0);
		__asm__ __volatile__("loop ." : "+c"((int){NUM_BRANCHES}));
		br1 = rdmsr(MSR_K7_PERFCTR0);
		GUEST_SYNC(br1 - br0);
	}
}

/*
 * Run the VM to the next GUEST_SYNC(value), and return the value passed
 * to the sync. Any other exit from the guest is fatal.
 */
static uint64_t run_vm_to_sync(struct kvm_vm *vm)
{
	struct kvm_run *run = vcpu_state(vm, VCPU_ID);
	struct ucall uc;

	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Exit_reason other than KVM_EXIT_IO: %u (%s)\n",
		    run->exit_reason,
		    exit_reason_str(run->exit_reason));
	get_ucall(vm, VCPU_ID, &uc);
	TEST_ASSERT(uc.cmd == UCALL_SYNC,
		    "Received ucall other than UCALL_SYNC: %lu", uc.cmd);
	return uc.args[1];
}

/*
 * In a nested environment or if the vPMU is disabled, the guest PMU
 * might not work as architected (accessing the PMU MSRs may raise
 * #GP, or writes could simply be discarded). In those situations,
 * there is no point in running these tests. The guest code will perform
 * a sanity check and then GUEST_SYNC(success). In the case of failure,
 * the behavior of the guest on resumption is undefined.
 */
static bool sanity_check_pmu(struct kvm_vm *vm)
{
	bool success;

	vm_install_exception_handler(vm, GP_VECTOR, guest_gp_handler);
	success = run_vm_to_sync(vm);
	vm_install_exception_handler(vm, GP_VECTOR, NULL);

	return success;
}

static struct kvm_pmu_event_filter *make_pmu_event_filter(uint32_t nevents)
{
	struct kvm_pmu_event_filter *f;
	int size = sizeof(*f) + nevents * sizeof(f->events[0]);

	f = malloc(size);
	TEST_ASSERT(f, "Out of memory");
	memset(f, 0, size);
	f->nevents = nevents;
	return f;
}

static struct kvm_pmu_event_filter *event_filter(uint32_t action)
{
	struct kvm_pmu_event_filter *f;
	int i;

	f = make_pmu_event_filter(ARRAY_SIZE(event_list));
	f->action = action;
	for (i = 0; i < ARRAY_SIZE(event_list); i++)
		f->events[i] = event_list[i];

	return f;
}

/*
 * Remove the first occurrence of 'event' (if any) from the filter's
 * event list.
 */
static struct kvm_pmu_event_filter *remove_event(struct kvm_pmu_event_filter *f,
						 uint64_t event)
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
	return f;
}

static void test_without_filter(struct kvm_vm *vm)
{
	uint64_t count = run_vm_to_sync(vm);

	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static uint64_t test_with_filter(struct kvm_vm *vm,
				 struct kvm_pmu_event_filter *f)
{
	vm_ioctl(vm, KVM_SET_PMU_EVENT_FILTER, (void *)f);
	return run_vm_to_sync(vm);
}

static void test_member_deny_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_DENY);
	uint64_t count = test_with_filter(vm, f);

	free(f);
	if (count)
		pr_info("%s: Branch instructions retired = %lu (expected 0)\n",
			__func__, count);
	TEST_ASSERT(!count, "Disallowed PMU Event is counting");
}

static void test_member_allow_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_ALLOW);
	uint64_t count = test_with_filter(vm, f);

	free(f);
	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static void test_not_member_deny_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_DENY);
	uint64_t count;

	remove_event(f, INTEL_BR_RETIRED);
	remove_event(f, AMD_ZEN_BR_RETIRED);
	count = test_with_filter(vm, f);
	free(f);
	if (count != NUM_BRANCHES)
		pr_info("%s: Branch instructions retired = %lu (expected %u)\n",
			__func__, count, NUM_BRANCHES);
	TEST_ASSERT(count, "Allowed PMU event is not counting");
}

static void test_not_member_allow_list(struct kvm_vm *vm)
{
	struct kvm_pmu_event_filter *f = event_filter(KVM_PMU_EVENT_ALLOW);
	uint64_t count;

	remove_event(f, INTEL_BR_RETIRED);
	remove_event(f, AMD_ZEN_BR_RETIRED);
	count = test_with_filter(vm, f);
	free(f);
	if (count)
		pr_info("%s: Branch instructions retired = %lu (expected 0)\n",
			__func__, count);
	TEST_ASSERT(!count, "Disallowed PMU Event is counting");
}

/*
 * Check for a non-zero PMU version, at least one general-purpose
 * counter per logical processor, an EBX bit vector of length greater
 * than 5, and EBX[5] clear.
 */
static bool check_intel_pmu_leaf(struct kvm_cpuid_entry2 *entry)
{
	union cpuid10_eax eax = { .full = entry->eax };
	union cpuid10_ebx ebx = { .full = entry->ebx };

	return eax.split.version_id && eax.split.num_counters > 0 &&
		eax.split.mask_length > ARCH_PERFMON_BRANCHES_RETIRED &&
		!ebx.split.no_branch_instruction_retired;
}

/*
 * Note that CPUID leaf 0xa is Intel-specific. This leaf should be
 * clear on AMD hardware.
 */
static bool use_intel_pmu(void)
{
	struct kvm_cpuid_entry2 *entry;

	entry = kvm_get_supported_cpuid_index(0xa, 0);
	return is_intel_cpu() && entry && check_intel_pmu_leaf(entry);
}

static bool is_zen1(uint32_t eax)
{
	return x86_family(eax) == 0x17 && x86_model(eax) <= 0x0f;
}

static bool is_zen2(uint32_t eax)
{
	return x86_family(eax) == 0x17 &&
		x86_model(eax) >= 0x30 && x86_model(eax) <= 0x3f;
}

static bool is_zen3(uint32_t eax)
{
	return x86_family(eax) == 0x19 && x86_model(eax) <= 0x0f;
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
	struct kvm_cpuid_entry2 *entry;

	entry = kvm_get_supported_cpuid_index(1, 0);
	return is_amd_cpu() && entry &&
		(is_zen1(entry->eax) ||
		 is_zen2(entry->eax) ||
		 is_zen3(entry->eax));
}

int main(int argc, char *argv[])
{
	void (*guest_code)(void) = NULL;
	struct kvm_vm *vm;
	int r;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	r = kvm_check_cap(KVM_CAP_PMU_EVENT_FILTER);
	if (!r) {
		print_skip("KVM_CAP_PMU_EVENT_FILTER not supported");
		exit(KSFT_SKIP);
	}

	if (use_intel_pmu())
		guest_code = intel_guest_code;
	else if (use_amd_pmu())
		guest_code = amd_guest_code;

	if (!guest_code) {
		print_skip("Don't know how to test this guest PMU");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, guest_code);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	if (!sanity_check_pmu(vm)) {
		print_skip("Guest PMU is not functional");
		exit(KSFT_SKIP);
	}

	test_without_filter(vm);
	test_member_deny_list(vm);
	test_member_allow_list(vm);
	test_not_member_deny_list(vm);
	test_not_member_allow_list(vm);

	kvm_vm_free(vm);

	return 0;
}
