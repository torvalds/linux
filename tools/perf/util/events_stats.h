/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVENTS_STATS_
#define __PERF_EVENTS_STATS_

#include <stdio.h>
#include <perf/event.h>
#include <linux/types.h>
#include "auxtrace.h"

/*
 * The kernel collects the number of events it couldn't send in a stretch and
 * when possible sends this number in a PERF_RECORD_LOST event. The number of
 * such "chunks" of lost events is stored in .nr_events[PERF_EVENT_LOST] while
 * total_lost tells exactly how many events the kernel in fact lost, i.e. it is
 * the sum of all struct perf_record_lost.lost fields reported.
 *
 * The kernel discards mixed up samples and sends the number in a
 * PERF_RECORD_LOST_SAMPLES event. The number of lost-samples events is stored
 * in .nr_events[PERF_RECORD_LOST_SAMPLES] while total_lost_samples tells
 * exactly how many samples the kernel in fact dropped, i.e. it is the sum of
 * all struct perf_record_lost_samples.lost fields reported.
 *
 * The total_period is needed because by default auto-freq is used, so
 * multiplying nr_events[PERF_EVENT_SAMPLE] by a frequency isn't possible to get
 * the total number of low level events, it is necessary to sum all struct
 * perf_record_sample.period and stash the result in total_period.
 */
struct events_stats {
	u64 total_lost;
	u64 total_lost_samples;
	u64 total_aux_lost;
	u64 total_aux_partial;
	u64 total_aux_collision;
	u64 total_invalid_chains;
	u32 nr_events[PERF_RECORD_HEADER_MAX];
	u32 nr_lost_warned;
	u32 nr_unknown_events;
	u32 nr_invalid_chains;
	u32 nr_unknown_id;
	u32 nr_unprocessable_samples;
	u32 nr_auxtrace_errors[PERF_AUXTRACE_ERROR_MAX];
	u32 nr_proc_map_timeout;
};

struct hists_stats {
	u64 total_period;
	u64 total_non_filtered_period;
	u32 nr_samples;
	u32 nr_non_filtered_samples;
	u32 nr_lost_samples;
};

void events_stats__inc(struct events_stats *stats, u32 type);

size_t events_stats__fprintf(struct events_stats *stats, FILE *fp,
			     bool skip_empty);

#endif /* __PERF_EVENTS_STATS_ */
