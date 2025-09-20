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
	ARM_SPE_AUXTRACE_V1_PRIV_MAX,
};

#define ARM_SPE_AUXTRACE_V1_PRIV_SIZE	\
	(ARM_SPE_AUXTRACE_V1_PRIV_MAX * sizeof(u64))

enum {
	/*
	 * The old metadata format (defined above) does not include a
	 * field for version number. Version 1 is reserved and starts
	 * from version 2.
	 */
	ARM_SPE_HEADER_VERSION,
	/* Number of sizeof(u64) */
	ARM_SPE_HEADER_SIZE,
	/* PMU type shared by CPUs */
	ARM_SPE_PMU_TYPE_V2,
	/* Number of CPUs */
	ARM_SPE_CPUS_NUM,
	ARM_SPE_AUXTRACE_PRIV_MAX,
};

enum {
	/* Magic number */
	ARM_SPE_MAGIC,
	/* CPU logical number in system */
	ARM_SPE_CPU,
	/* Number of parameters */
	ARM_SPE_CPU_NR_PARAMS,
	/* CPU MIDR */
	ARM_SPE_CPU_MIDR,
	/* Associated PMU type */
	ARM_SPE_CPU_PMU_TYPE,
	/* Minimal interval */
	ARM_SPE_CAP_MIN_IVAL,
	ARM_SPE_CPU_PRIV_MAX,
};

#define ARM_SPE_HEADER_CURRENT_VERSION	2


union perf_event;
struct perf_session;
struct perf_pmu;

struct auxtrace_record *arm_spe_recording_init(int *err,
					       struct perf_pmu *arm_spe_pmu);

int arm_spe_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session);

void arm_spe_pmu_default_config(const struct perf_pmu *arm_spe_pmu,
				struct perf_event_attr *attr);

#endif
