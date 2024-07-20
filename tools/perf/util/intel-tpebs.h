/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpebs.h: Intel TEPBS support
 */
#ifndef INCLUDE__PERF_INTEL_TPEBS_H__
#define INCLUDE__PERF_INTEL_TPEBS_H__

#include "stat.h"
#include "evsel.h"

#ifdef HAVE_ARCH_X86_64_SUPPORT

extern bool tpebs_recording;
int tpebs_start(struct evlist *evsel_list);
void tpebs_delete(void);
int tpebs_set_evsel(struct evsel *evsel, int cpu_map_idx, int thread);

#else

static inline int tpebs_start(struct evlist *evsel_list __maybe_unused)
{
	return 0;
}

static inline void tpebs_delete(void) {};

static inline int tpebs_set_evsel(struct evsel *evsel  __maybe_unused,
				int cpu_map_idx  __maybe_unused,
				int thread  __maybe_unused)
{
	return 0;
}

#endif
#endif
