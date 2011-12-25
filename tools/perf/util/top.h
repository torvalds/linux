#ifndef __PERF_TOP_H
#define __PERF_TOP_H 1

#include "tool.h"
#include "types.h"
#include <stddef.h>
#include <stdbool.h>

struct perf_evlist;
struct perf_evsel;
struct perf_session;

struct perf_top {
	struct perf_tool   tool;
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
	bool		   system_wide;
	bool		   use_tui, use_stdio;
	bool		   sort_has_symbols;
	bool		   dont_use_callchains;
	bool		   kptr_restrict_warned;
	bool		   vmlinux_warned;
	bool		   inherit;
	bool		   group;
	bool		   sample_id_all_avail;
	bool		   dump_symtab;
	const char	   *cpu_list;
	struct hist_entry  *sym_filter_entry;
	struct perf_evsel  *sym_evsel;
	struct perf_session *session;
	struct winsize	   winsize;
	unsigned int	   mmap_pages;
	int		   default_interval;
	int		   realtime_prio;
	int		   sym_pcnt_filter;
	const char	   *sym_filter;
};

size_t perf_top__header_snprintf(struct perf_top *top, char *bf, size_t size);
void perf_top__reset_sample_counters(struct perf_top *top);
#endif /* __PERF_TOP_H */
