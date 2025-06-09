/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpebs.h: Intel TEPBS support
 */
#ifndef __INTEL_TPEBS_H
#define __INTEL_TPEBS_H

struct evlist;
struct evsel;

enum tpebs_mode {
	TPEBS_MODE__MEAN,
	TPEBS_MODE__MIN,
	TPEBS_MODE__MAX,
	TPEBS_MODE__LAST,
};

extern bool tpebs_recording;
extern enum tpebs_mode tpebs_mode;

int evsel__tpebs_open(struct evsel *evsel);
void evsel__tpebs_close(struct evsel *evsel);
int evsel__tpebs_read(struct evsel *evsel, int cpu_map_idx, int thread);

#endif /* __INTEL_TPEBS_H */
