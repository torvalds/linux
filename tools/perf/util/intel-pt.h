/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_pt.h: Intel Processor Trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 */

#ifndef INCLUDE__PERF_INTEL_PT_H__
#define INCLUDE__PERF_INTEL_PT_H__

#define INTEL_PT_PMU_NAME "intel_pt"

enum {
	INTEL_PT_PMU_TYPE,
	INTEL_PT_TIME_SHIFT,
	INTEL_PT_TIME_MULT,
	INTEL_PT_TIME_ZERO,
	INTEL_PT_CAP_USER_TIME_ZERO,
	INTEL_PT_TSC_BIT,
	INTEL_PT_NORETCOMP_BIT,
	INTEL_PT_HAVE_SCHED_SWITCH,
	INTEL_PT_SNAPSHOT_MODE,
	INTEL_PT_PER_CPU_MMAPS,
	INTEL_PT_MTC_BIT,
	INTEL_PT_MTC_FREQ_BITS,
	INTEL_PT_TSC_CTC_N,
	INTEL_PT_TSC_CTC_D,
	INTEL_PT_CYC_BIT,
	INTEL_PT_MAX_NONTURBO_RATIO,
	INTEL_PT_FILTER_STR_LEN,
	INTEL_PT_AUXTRACE_PRIV_MAX,
};

struct auxtrace_record;
struct perf_tool;
union perf_event;
struct perf_session;
struct perf_event_attr;
struct perf_pmu;

struct auxtrace_record *intel_pt_recording_init(int *err);

int intel_pt_process_auxtrace_info(union perf_event *event,
				   struct perf_session *session);

struct perf_event_attr *intel_pt_pmu_default_config(struct perf_pmu *pmu);

#endif
