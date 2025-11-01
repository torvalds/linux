// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Tencent, Inc.
 */

#include <stdint.h>

#include <linux/kernel.h>

#include "kvm_util.h"
#include "processor.h"
#include "pmu.h"

const uint64_t intel_pmu_arch_events[] = {
	INTEL_ARCH_CPU_CYCLES,
	INTEL_ARCH_INSTRUCTIONS_RETIRED,
	INTEL_ARCH_REFERENCE_CYCLES,
	INTEL_ARCH_LLC_REFERENCES,
	INTEL_ARCH_LLC_MISSES,
	INTEL_ARCH_BRANCHES_RETIRED,
	INTEL_ARCH_BRANCHES_MISPREDICTED,
	INTEL_ARCH_TOPDOWN_SLOTS,
	INTEL_ARCH_TOPDOWN_BE_BOUND,
	INTEL_ARCH_TOPDOWN_BAD_SPEC,
	INTEL_ARCH_TOPDOWN_FE_BOUND,
	INTEL_ARCH_TOPDOWN_RETIRING,
	INTEL_ARCH_LBR_INSERTS,
};
kvm_static_assert(ARRAY_SIZE(intel_pmu_arch_events) == NR_INTEL_ARCH_EVENTS);

const uint64_t amd_pmu_zen_events[] = {
	AMD_ZEN_CORE_CYCLES,
	AMD_ZEN_INSTRUCTIONS_RETIRED,
	AMD_ZEN_BRANCHES_RETIRED,
	AMD_ZEN_BRANCHES_MISPREDICTED,
};
kvm_static_assert(ARRAY_SIZE(amd_pmu_zen_events) == NR_AMD_ZEN_EVENTS);

/*
 * For Intel Atom CPUs, the PMU events "Instruction Retired" or
 * "Branch Instruction Retired" may be overcounted for some certain
 * instructions, like FAR CALL/JMP, RETF, IRET, VMENTRY/VMEXIT/VMPTRLD
 * and complex SGX/SMX/CSTATE instructions/flows.
 *
 * The detailed information can be found in the errata (section SRF7):
 * https://edc.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/sierra-forest/xeon-6700-series-processor-with-e-cores-specification-update/errata-details/
 *
 * For the Atom platforms before Sierra Forest (including Sierra Forest),
 * Both 2 events "Instruction Retired" and "Branch Instruction Retired" would
 * be overcounted on these certain instructions, but for Clearwater Forest
 * only "Instruction Retired" event is overcounted on these instructions.
 */
static uint64_t get_pmu_errata(void)
{
	if (!this_cpu_is_intel())
		return 0;

	if (this_cpu_family() != 0x6)
		return 0;

	switch (this_cpu_model()) {
	case 0xDD: /* Clearwater Forest */
		return BIT_ULL(INSTRUCTIONS_RETIRED_OVERCOUNT);
	case 0xAF: /* Sierra Forest */
	case 0x4D: /* Avaton, Rangely */
	case 0x5F: /* Denverton */
	case 0x86: /* Jacobsville */
		return BIT_ULL(INSTRUCTIONS_RETIRED_OVERCOUNT) |
		       BIT_ULL(BRANCHES_RETIRED_OVERCOUNT);
	default:
		return 0;
	}
}

uint64_t pmu_errata_mask;

void kvm_init_pmu_errata(void)
{
	pmu_errata_mask = get_pmu_errata();
}
