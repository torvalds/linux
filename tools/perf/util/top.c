// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Refactored from builtin-top.c, see that files for further copyright notes.
 */

#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "symbol.h"
#include "top.h"
#include "../perf.h"
#include <inttypes.h>

#define SNPRINTF(buf, size, fmt, args...) \
({ \
	size_t r = snprintf(buf, size, fmt, ## args); \
	r > size ?  size : r; \
})

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size)
{
	float samples_per_sec;
	float ksamples_per_sec;
	float esamples_percent;
	struct record_opts *opts = &top->record_opts;
	struct target *target = &opts->target;
	size_t ret = 0;

	if (top->samples) {
		samples_per_sec = top->samples / top->delay_secs;
		ksamples_per_sec = top->kernel_samples / top->delay_secs;
		esamples_percent = (100.0 * top->exact_samples) / top->samples;
	} else {
		samples_per_sec = ksamples_per_sec = esamples_percent = 0.0;
	}

	if (!perf_guest) {
		float ksamples_percent = 0.0;

		if (samples_per_sec)
			ksamples_percent = (100.0 * ksamples_per_sec) /
							samples_per_sec;
		ret = SNPRINTF(bf, size,
			       "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%%"
			       "  exact: %4.1f%% lost: %" PRIu64 "/%" PRIu64 " drop: %" PRIu64 "/%" PRIu64 " [",
			       samples_per_sec, ksamples_percent, esamples_percent,
			       top->lost, top->lost_total, top->drop, top->drop_total);
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

	if (top->evlist->core.nr_entries == 1) {
		struct evsel *first = evlist__first(top->evlist);
		ret += SNPRINTF(bf + ret, size - ret, "%" PRIu64 "%s ",
				(uint64_t)first->core.attr.sample_period,
				opts->freq ? "Hz" : "");
	}

	ret += SNPRINTF(bf + ret, size - ret, "%s", evsel__name(top->sym_evsel));

	ret += SNPRINTF(bf + ret, size - ret, "], ");

	if (target->pid)
		ret += SNPRINTF(bf + ret, size - ret, " (target_pid: %s",
				target->pid);
	else if (target->tid)
		ret += SNPRINTF(bf + ret, size - ret, " (target_tid: %s",
				target->tid);
	else if (target->uid_str != NULL)
		ret += SNPRINTF(bf + ret, size - ret, " (uid: %s",
				target->uid_str);
	else
		ret += SNPRINTF(bf + ret, size - ret, " (all");

	if (target->cpu_list)
		ret += SNPRINTF(bf + ret, size - ret, ", CPU%s: %s)",
				perf_cpu_map__nr(top->evlist->core.user_requested_cpus) > 1
				? "s" : "",
				target->cpu_list);
	else {
		if (target->tid)
			ret += SNPRINTF(bf + ret, size - ret, ")");
		else
			ret += SNPRINTF(bf + ret, size - ret, ", %d CPU%s)",
					perf_cpu_map__nr(top->evlist->core.user_requested_cpus),
					perf_cpu_map__nr(top->evlist->core.user_requested_cpus) > 1
					? "s" : "");
	}

	perf_top__reset_sample_counters(top);
	return ret;
}

void perf_top__reset_sample_counters(struct perf_top *top)
{
	top->samples = top->us_samples = top->kernel_samples =
	top->exact_samples = top->guest_kernel_samples =
	top->guest_us_samples = top->lost = top->drop = 0;
}
