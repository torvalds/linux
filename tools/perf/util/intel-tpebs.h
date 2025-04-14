/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpebs.h: Intel TEPBS support
 */
#ifndef __INTEL_TPEBS_H
#define __INTEL_TPEBS_H

struct evlist;
struct evsel;

extern bool tpebs_recording;

int tpebs_start(struct evlist *evsel_list);
void tpebs_delete(void);
int tpebs_set_evsel(struct evsel *evsel, int cpu_map_idx, int thread);

#endif /* __INTEL_TPEBS_H */
