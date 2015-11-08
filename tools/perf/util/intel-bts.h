/*
 * intel-bts.h: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef INCLUDE__PERF_INTEL_BTS_H__
#define INCLUDE__PERF_INTEL_BTS_H__

#define INTEL_BTS_PMU_NAME "intel_bts"

enum {
	INTEL_BTS_PMU_TYPE,
	INTEL_BTS_TIME_SHIFT,
	INTEL_BTS_TIME_MULT,
	INTEL_BTS_TIME_ZERO,
	INTEL_BTS_CAP_USER_TIME_ZERO,
	INTEL_BTS_SNAPSHOT_MODE,
	INTEL_BTS_AUXTRACE_PRIV_MAX,
};

#define INTEL_BTS_AUXTRACE_PRIV_SIZE (INTEL_BTS_AUXTRACE_PRIV_MAX * sizeof(u64))

struct auxtrace_record;
struct perf_tool;
union perf_event;
struct perf_session;

struct auxtrace_record *intel_bts_recording_init(int *err);

int intel_bts_process_auxtrace_info(union perf_event *event,
				    struct perf_session *session);

#endif
