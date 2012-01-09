#ifndef __PERF_TOP_H
#define __PERF_TOP_H 1

#include "types.h"
#include "../perf.h"
#include <stddef.h>

struct perf_evlist;
struct perf_evsel;
struct perf_session;

struct perf_top {
	struct perf_evlist *evlist;
	/*
	 * Symbols will be added here in perf_event__process_sample and will
	 * get out after decayed.
	 */
	u64		   samples;
	u64		   kernel_samples, us_samples;
	u64		   exact_samples;
	u64		   guest_us_samples, guest_kernel_samples;
	int		   print_entries, count_filter, delay_secs;
	int		   freq;
	pid_t		   target_pid, target_tid;
	bool		   hide_kernel_symbols, hide_user_symbols, zero;
	const char	   *cpu_list;
	struct hist_entry  *sym_filter_entry;
	struct perf_evsel  *sym_evsel;
	struct perf_session *session;
};

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size);
void perf_top__reset_sample_counters(struct perf_top *top);
#endif /* __PERF_TOP_H */
