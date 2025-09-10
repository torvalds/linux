/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SAMPLE_RAW_H
#define __SAMPLE_RAW_H 1

struct evlist;
union perf_event;
struct perf_sample;

void evlist__s390_sample_raw(struct evlist *evlist, union perf_event *event,
			     struct perf_sample *sample);
bool evlist__has_amd_ibs(struct evlist *evlist);
void evlist__amd_sample_raw(struct evlist *evlist, union perf_event *event,
			    struct perf_sample *sample);
void evlist__init_trace_event_sample_raw(struct evlist *evlist, struct perf_env *env);
#endif /* __PERF_EVLIST_H */
