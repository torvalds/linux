/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#ifndef INCLUDE__PERF_ARM_SPE_H__
#define INCLUDE__PERF_ARM_SPE_H__

#define ARM_SPE_PMU_NAME "arm_spe_"

enum {
	ARM_SPE_PMU_TYPE,
	ARM_SPE_PER_CPU_MMAPS,
	ARM_SPE_AUXTRACE_PRIV_MAX,
};

#define ARM_SPE_AUXTRACE_PRIV_SIZE (ARM_SPE_AUXTRACE_PRIV_MAX * sizeof(u64))

union perf_event;
struct perf_session;
struct perf_pmu;

struct auxtrace_record *arm_spe_recording_init(int *err,
					       struct perf_pmu *arm_spe_pmu);

int arm_spe_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session);

struct perf_event_attr *arm_spe_pmu_default_config(struct perf_pmu *arm_spe_pmu);
#endif
