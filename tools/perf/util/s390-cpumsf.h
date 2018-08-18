/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2018
 * Auxtrace support for s390 CPU-Measurement Sampling Facility
 *
 * Author(s):  Thomas Richter <tmricht@linux.ibm.com>
 */

#ifndef INCLUDE__PERF_S390_CPUMSF_H
#define INCLUDE__PERF_S390_CPUMSF_H

union perf_event;
struct perf_session;
struct perf_pmu;

struct auxtrace_record *
s390_cpumsf_recording_init(int *err, struct perf_pmu *s390_cpumsf_pmu);

int s390_cpumsf_process_auxtrace_info(union perf_event *event,
				      struct perf_session *session);
#endif
