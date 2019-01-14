/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_TOP_H
#define __PERF_TOP_H 1

#include "tool.h"
#include "annotate.h"
#include <linux/types.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/ioctl.h>

struct perf_evlist;
struct perf_evsel;
struct perf_session;

struct perf_top {
	struct perf_tool   tool;
	struct perf_evlist *evlist;
	struct record_opts record_opts;
	struct annotation_options annotation_opts;
	/*
	 * Symbols will be added here in perf_event__process_sample and will
	 * get out after decayed.
	 */
	u64		   samples;
	u64		   kernel_samples, us_samples;
	u64		   exact_samples;
	u64		   guest_us_samples, guest_kernel_samples;
	int		   print_entries, count_filter, delay_secs;
	int		   max_stack;
	bool		   hide_kernel_symbols, hide_user_symbols, zero;
	bool		   use_tui, use_stdio;
	bool		   vmlinux_warned;
	bool		   dump_symtab;
	struct hist_entry  *sym_filter_entry;
	struct perf_evsel  *sym_evsel;
	struct perf_session *session;
	struct winsize	   winsize;
	int		   realtime_prio;
	const char	   *sym_filter;
	float		   min_percent;
	unsigned int	   nr_threads_synthesize;
};

#define CONSOLE_CLEAR "[H[2J"

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size);
void perf_top__reset_sample_counters(struct perf_top *top);
#endif /* __PERF_TOP_H */
