/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Refactored from builtin-top.c, see that files for further copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "cpumap.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "symbol.h"
#include "top.h"
#include <inttypes.h>

#define SNPRINTF(buf, size, fmt, args...) \
({ \
	size_t r = snprintf(buf, size, fmt, ## args); \
	r > size ?  size : r; \
})

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size)
{
	float samples_per_sec = top->samples / top->delay_secs;
	float ksamples_per_sec = top->kernel_samples / top->delay_secs;
	float esamples_percent = (100.0 * top->exact_samples) / top->samples;
	size_t ret = 0;

	if (!perf_guest) {
		ret = SNPRINTF(bf, size,
			       "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%%"
			       "  exact: %4.1f%% [", samples_per_sec,
			       100.0 - (100.0 * ((samples_per_sec - ksamples_per_sec) /
					samples_per_sec)),
				esamples_percent);
	} else {
		float us_samples_per_sec = top->us_samples / top->delay_secs;
		float guest_kernel_samples_per_sec = top->guest_kernel_samples / top->delay_secs;
		float guest_us_samples_per_sec = top->guest_us_samples / top->delay_secs;

		ret = SNPRINTF(bf, size,
			       "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%% us:%4.1f%%"
			       " guest kernel:%4.1f%% guest us:%4.1f%%"
			       " exact: %4.1f%% [", samples_per_sec,
			       100.0 - (100.0 * ((samples_per_sec - ksamples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec - us_samples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec -
						  guest_kernel_samples_per_sec) /
						 samples_per_sec)),
			       100.0 - (100.0 * ((samples_per_sec -
						  guest_us_samples_per_sec) /
						 samples_per_sec)),
			       esamples_percent);
	}

	if (top->evlist->nr_entries == 1) {
		struct perf_evsel *first;
		first = list_entry(top->evlist->entries.next, struct perf_evsel, node);
		ret += SNPRINTF(bf + ret, size - ret, "%" PRIu64 "%s ",
				(uint64_t)first->attr.sample_period,
				top->freq ? "Hz" : "");
	}

	ret += SNPRINTF(bf + ret, size - ret, "%s", event_name(top->sym_evsel));

	ret += SNPRINTF(bf + ret, size - ret, "], ");

	if (top->target.pid)
		ret += SNPRINTF(bf + ret, size - ret, " (target_pid: %s",
				top->target.pid);
	else if (top->target.tid)
		ret += SNPRINTF(bf + ret, size - ret, " (target_tid: %s",
				top->target.tid);
	else if (top->target.uid_str != NULL)
		ret += SNPRINTF(bf + ret, size - ret, " (uid: %s",
				top->target.uid_str);
	else
		ret += SNPRINTF(bf + ret, size - ret, " (all");

	if (top->target.cpu_list)
		ret += SNPRINTF(bf + ret, size - ret, ", CPU%s: %s)",
				top->evlist->cpus->nr > 1 ? "s" : "",
				top->target.cpu_list);
	else {
		if (top->target.tid)
			ret += SNPRINTF(bf + ret, size - ret, ")");
		else
			ret += SNPRINTF(bf + ret, size - ret, ", %d CPU%s)",
					top->evlist->cpus->nr,
					top->evlist->cpus->nr > 1 ? "s" : "");
	}

	return ret;
}

void perf_top__reset_sample_counters(struct perf_top *top)
{
	top->samples = top->us_samples = top->kernel_samples =
	top->exact_samples = top->guest_kernel_samples =
	top->guest_us_samples = 0;
}
