/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023, Tencent, Inc.
 */
#ifndef SELFTEST_KVM_PMU_H
#define SELFTEST_KVM_PMU_H

#include <stdint.h>

#define KVM_PMU_EVENT_FILTER_MAX_EVENTS			300

/*
 * Encode an eventsel+umask pair into event-select MSR format.  Note, this is
 * technically AMD's format, as Intel's format only supports 8 bits for the
 * event selector, i.e. doesn't use bits 24:16 for the selector.  But, OR-ing
 * in '0' is a nop and won't clobber the CMASK.
 */
#define RAW_EVENT(eventsel, umask) (((eventsel & 0xf00UL) << 24) |	\
				    ((eventsel) & 0xff) |		\
				    ((umask) & 0xff) << 8)

/*
 * These are technically Intel's definitions, but except for CMASK (see above),
 * AMD's layout is compatible with Intel's.
 */
#define ARCH_PERFMON_EVENTSEL_EVENT		GENMASK_ULL(7, 0)
#define ARCH_PERFMON_EVENTSEL_UMASK		GENMASK_ULL(15, 8)
#define ARCH_PERFMON_EVENTSEL_USR		BIT_ULL(16)
#define ARCH_PERFMON_EVENTSEL_OS		BIT_ULL(17)
#define ARCH_PERFMON_EVENTSEL_EDGE		BIT_ULL(18)
#define ARCH_PERFMON_EVENTSEL_PIN_CONTROL	BIT_ULL(19)
#define ARCH_PERFMON_EVENTSEL_INT		BIT_ULL(20)
#define ARCH_PERFMON_EVENTSEL_ANY		BIT_ULL(21)
#define ARCH_PERFMON_EVENTSEL_ENABLE		BIT_ULL(22)
#define ARCH_PERFMON_EVENTSEL_INV		BIT_ULL(23)
#define ARCH_PERFMON_EVENTSEL_CMASK		GENMASK_ULL(31, 24)

/* RDPMC control flags, Intel only. */
#define INTEL_RDPMC_METRICS			BIT_ULL(29)
#define INTEL_RDPMC_FIXED			BIT_ULL(30)
#define INTEL_RDPMC_FAST			BIT_ULL(31)

/* Fixed PMC controls, Intel only. */
#define FIXED_PMC_GLOBAL_CTRL_ENABLE(_idx)	BIT_ULL((32 + (_idx)))

#define FIXED_PMC_KERNEL			BIT_ULL(0)
#define FIXED_PMC_USER				BIT_ULL(1)
#define FIXED_PMC_ANYTHREAD			BIT_ULL(2)
#define FIXED_PMC_ENABLE_PMI			BIT_ULL(3)
#define FIXED_PMC_NR_BITS			4
#define FIXED_PMC_CTRL(_idx, _val)		((_val) << ((_idx) * FIXED_PMC_NR_BITS))

#define PMU_CAP_FW_WRITES			BIT_ULL(13)
#define PMU_CAP_LBR_FMT				0x3f

#define	INTEL_ARCH_CPU_CYCLES			RAW_EVENT(0x3c, 0x00)
#define	INTEL_ARCH_INSTRUCTIONS_RETIRED		RAW_EVENT(0xc0, 0x00)
#define	INTEL_ARCH_REFERENCE_CYCLES		RAW_EVENT(0x3c, 0x01)
#define	INTEL_ARCH_LLC_REFERENCES		RAW_EVENT(0x2e, 0x4f)
#define	INTEL_ARCH_LLC_MISSES			RAW_EVENT(0x2e, 0x41)
#define	INTEL_ARCH_BRANCHES_RETIRED		RAW_EVENT(0xc4, 0x00)
#define	INTEL_ARCH_BRANCHES_MISPREDICTED	RAW_EVENT(0xc5, 0x00)
#define	INTEL_ARCH_TOPDOWN_SLOTS		RAW_EVENT(0xa4, 0x01)

#define	AMD_ZEN_CORE_CYCLES			RAW_EVENT(0x76, 0x00)
#define	AMD_ZEN_INSTRUCTIONS_RETIRED		RAW_EVENT(0xc0, 0x00)
#define	AMD_ZEN_BRANCHES_RETIRED		RAW_EVENT(0xc2, 0x00)
#define	AMD_ZEN_BRANCHES_MISPREDICTED		RAW_EVENT(0xc3, 0x00)

/*
 * Note!  The order and thus the index of the architectural events matters as
 * support for each event is enumerated via CPUID using the index of the event.
 */
enum intel_pmu_architectural_events {
	INTEL_ARCH_CPU_CYCLES_INDEX,
	INTEL_ARCH_INSTRUCTIONS_RETIRED_INDEX,
	INTEL_ARCH_REFERENCE_CYCLES_INDEX,
	INTEL_ARCH_LLC_REFERENCES_INDEX,
	INTEL_ARCH_LLC_MISSES_INDEX,
	INTEL_ARCH_BRANCHES_RETIRED_INDEX,
	INTEL_ARCH_BRANCHES_MISPREDICTED_INDEX,
	INTEL_ARCH_TOPDOWN_SLOTS_INDEX,
	NR_INTEL_ARCH_EVENTS,
};

enum amd_pmu_zen_events {
	AMD_ZEN_CORE_CYCLES_INDEX,
	AMD_ZEN_INSTRUCTIONS_INDEX,
	AMD_ZEN_BRANCHES_INDEX,
	AMD_ZEN_BRANCH_MISSES_INDEX,
	NR_AMD_ZEN_EVENTS,
};

extern const uint64_t intel_pmu_arch_events[];
extern const uint64_t amd_pmu_zen_events[];

#endif /* SELFTEST_KVM_PMU_H */
