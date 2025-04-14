/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpebs.h: Intel TEPBS support
 */
#ifndef __INTEL_TPEBS_H
#define __INTEL_TPEBS_H

struct evlist;
struct evsel;

extern bool tpebs_recording;

int evsel__tpebs_open(struct evsel *evsel);
void evsel__tpebs_close(struct evsel *evsel);
int tpebs_set_evsel(struct evsel *evsel, int cpu_map_idx, int thread);

#endif /* __INTEL_TPEBS_H */
