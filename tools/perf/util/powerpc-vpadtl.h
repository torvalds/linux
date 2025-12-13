/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VPA DTL PMU Support
 */

#ifndef INCLUDE__PERF_POWERPC_VPADTL_H__
#define INCLUDE__PERF_POWERPC_VPADTL_H__

enum {
	POWERPC_VPADTL_TYPE,
	VPADTL_AUXTRACE_PRIV_MAX,
};

#define VPADTL_AUXTRACE_PRIV_SIZE (VPADTL_AUXTRACE_PRIV_MAX * sizeof(u64))

union perf_event;
struct perf_session;
struct perf_pmu;

int powerpc_vpadtl_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session);

#endif
