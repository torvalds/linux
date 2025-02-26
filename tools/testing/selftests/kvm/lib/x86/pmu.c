// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Tencent, Inc.
 */

#include <stdint.h>

#include <linux/kernel.h>

#include "kvm_util.h"
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
};
kvm_static_assert(ARRAY_SIZE(intel_pmu_arch_events) == NR_INTEL_ARCH_EVENTS);

const uint64_t amd_pmu_zen_events[] = {
	AMD_ZEN_CORE_CYCLES,
	AMD_ZEN_INSTRUCTIONS_RETIRED,
	AMD_ZEN_BRANCHES_RETIRED,
	AMD_ZEN_BRANCHES_MISPREDICTED,
};
kvm_static_assert(ARRAY_SIZE(amd_pmu_zen_events) == NR_AMD_ZEN_EVENTS);
