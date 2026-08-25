// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM nested SVM PMU Host-Only/Guest-Only test
 *
 * Copyright (C) 2026, Google LLC.
 *
 * Test that KVM correctly virtualizes the AMD PMU Host-Only (bit 41) and
 * Guest-Only (bit 40) event selector bits across all SVM state
 * transitions.
 *
 * Programs 4 PMCs simultaneously with all combinations of Host-Only and
 * Guest-Only bits, then verifies correct counting behavior with different
 * combinations of EFER.SVME and host/guest mode -- as well as event filtering.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "pmu.h"

#define EVENTSEL_RETIRED_INSNS	(ARCH_PERFMON_EVENTSEL_OS |	\
				 ARCH_PERFMON_EVENTSEL_USR |	\
				 ARCH_PERFMON_EVENTSEL_ENABLE |	\
				 AMD_ZEN_INSTRUCTIONS_RETIRED)

/* PMC configurations: index corresponds to Host-Only | Guest-Only bits */
#define PMC_NONE	0  /* Neither bit set */
#define PMC_G		1  /* Guest-Only bit set */
#define PMC_H		2  /* Host-Only bit set */
#define PMC_HG		3  /* Both bits set */
#define NR_PMCS		4

#define LOOP_INSNS	1000

static __always_inline void run_instruction_loop(void)
{
	unsigned int i;

	for (i = 0; i < LOOP_INSNS; i++)
		__asm__ __volatile__("nop");
}

static __always_inline void read_counters(uint64_t *counts)
{
	int i;

	for (i = 0; i < NR_PMCS; i++)
		counts[i] = rdmsr(MSR_F15H_PERF_CTR + 2 * i);
}

static __always_inline void run_and_measure(uint64_t *deltas)
{
	uint64_t before[NR_PMCS], after[NR_PMCS];
	int i;

	read_counters(before);
	run_instruction_loop();
	read_counters(after);

	for (i = 0; i < NR_PMCS; i++)
		deltas[i] = after[i] - before[i];
}

static void assert_pmc_counts(uint64_t *deltas, unsigned int expected_counting)
{
	int i;

	for (i = 0; i < NR_PMCS; i++) {
		if (expected_counting & BIT(i))
			GUEST_ASSERT_NE(deltas[i], 0);
		else
			GUEST_ASSERT_EQ(deltas[i], 0);
	}
}

static uint64_t l2_deltas[NR_PMCS];

static void l2_guest_code(void)
{
	run_and_measure(l2_deltas);
	vmmcall();
}

static void l1_guest_code(struct svm_test_data *svm)
{
	struct vmcb *vmcb = svm->vmcb;
	uint64_t deltas[NR_PMCS];
	uint64_t eventsel;
	int i;

	/* Program 4 PMCs with all combinations of Host-Only/Guest-Only bits */
	for (i = 0; i < NR_PMCS; i++) {
		eventsel = EVENTSEL_RETIRED_INSNS;
		if (i & PMC_G)
			eventsel |= AMD64_EVENTSEL_GUESTONLY;
		if (i & PMC_H)
			eventsel |= AMD64_EVENTSEL_HOSTONLY;
		wrmsr(MSR_F15H_PERF_CTL + 2 * i, eventsel);
		wrmsr(MSR_F15H_PERF_CTR + 2 * i, 0);
	}

	/* Step 1: SVME=0 - Only the counter with neither bits set counts */
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) & ~EFER_SVME);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE));

	/* Step 2: Set SVME=1 - In L1 "host mode"; Guest-Only stops */
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SVME);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE) | BIT(PMC_H) | BIT(PMC_HG));

	/* Step 3: VMRUN to L2 - In "guest mode"; Host-Only stops */
	generic_svm_setup(svm, l2_guest_code);
	vmcb->control.intercept &= ~(1ULL << INTERCEPT_MSR_PROT);

	run_guest(vmcb, svm->vmcb_gpa);

	GUEST_ASSERT_EQ(vmcb->control.exit_code, SVM_EXIT_VMMCALL);
	assert_pmc_counts(l2_deltas, BIT(PMC_NONE) | BIT(PMC_G) | BIT(PMC_HG));

	/* Step 4: After VMEXIT to L1 - Back in "host mode"; Guest-Only stops */
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE) | BIT(PMC_H) | BIT(PMC_HG));

	/* Step 5: Set KVM_PMU_EVENT_DENY - all counters stop */
	GUEST_SYNC(KVM_PMU_EVENT_DENY);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, 0);

	/* Step 6: Set KVM_PMU_EVENT_ALLOW - back to all except Guest-only */
	GUEST_SYNC(KVM_PMU_EVENT_ALLOW);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE) | BIT(PMC_H) | BIT(PMC_HG));

	/* Step 7: Clear Host-Only for PMC_HG - counter stops in "host mode" */
	eventsel = rdmsr(MSR_F15H_PERF_CTL + 2 * PMC_HG);
	wrmsr(MSR_F15H_PERF_CTL + 2 * PMC_HG, eventsel & ~AMD64_EVENTSEL_HOSTONLY);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE) | BIT(PMC_H));

	/* Step 8: Restore Host-Only for PMC_HG - counter counts again */
	wrmsr(MSR_F15H_PERF_CTL + 2 * PMC_HG, eventsel);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE) | BIT(PMC_H) | BIT(PMC_HG));

	/* Step 9: Clear SVME - Only the counter with neither bits set counts */
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) & ~EFER_SVME);
	run_and_measure(deltas);
	assert_pmc_counts(deltas, BIT(PMC_NONE));

	GUEST_DONE();
}

static struct kvm_pmu_event_filter *alloc_event_filter(u64 event)
{
	struct kvm_pmu_event_filter *filter;

	filter = malloc(sizeof(*filter) + sizeof(event));
	TEST_ASSERT(filter != NULL, "Filter allocation failed");

	memset(filter, 0, sizeof(*filter));
	memcpy(filter->events, &event, sizeof(event));
	filter->nevents = 1;
	filter->action = KVM_PMU_EVENT_ALLOW;

	return filter;
}

int main(int argc, char *argv[])
{
	struct kvm_pmu_event_filter *filter;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	gva_t svm_gva;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));
	TEST_REQUIRE(kvm_is_pmu_enabled());
	TEST_REQUIRE(kvm_is_mediated_pmu_enabled());

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	vcpu_alloc_svm(vm, &svm_gva);
	vcpu_args_set(vcpu, 1, svm_gva);

	filter = alloc_event_filter(AMD_ZEN_INSTRUCTIONS_RETIRED);

	for (;;) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			goto done;
		case UCALL_DONE:
			goto done;
		case UCALL_SYNC:
			filter->action = uc.args[1];
			vm_ioctl(vm, KVM_SET_PMU_EVENT_FILTER, filter);
			break;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
			goto done;
		}
	}
done:
	kvm_vm_free(vm);
	return 0;
}
