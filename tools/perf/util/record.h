/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_RECORD_H
#define _PERF_RECORD_H

#include <time.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include "util/target.h"

struct option;

struct record_opts {
	struct target target;
	bool	      group;
	bool	      inherit_stat;
	bool	      no_buffering;
	bool	      no_inherit;
	bool	      no_inherit_set;
	bool	      no_samples;
	bool	      raw_samples;
	bool	      sample_address;
	bool	      sample_phys_addr;
	bool	      sample_data_page_size;
	bool	      sample_code_page_size;
	bool	      sample_weight;
	bool	      sample_time;
	bool	      sample_time_set;
	bool	      sample_cpu;
	bool	      period;
	bool	      period_set;
	bool	      running_time;
	bool	      full_auxtrace;
	bool	      auxtrace_snapshot_mode;
	bool	      auxtrace_snapshot_on_exit;
	bool	      auxtrace_sample_mode;
	bool	      record_namespaces;
	bool	      record_cgroup;
	bool	      record_switch_events;
	bool	      record_switch_events_set;
	bool	      all_kernel;
	bool	      all_user;
	bool	      kernel_callchains;
	bool	      user_callchains;
	bool	      tail_synthesize;
	bool	      overwrite;
	bool	      ignore_missing_thread;
	bool	      strict_freq;
	bool	      sample_id;
	bool	      no_bpf_event;
	bool	      kcore;
	bool	      text_poke;
	bool	      build_id;
	unsigned int  freq;
	unsigned int  mmap_pages;
	unsigned int  auxtrace_mmap_pages;
	unsigned int  user_freq;
	u64	      branch_stack;
	u64	      sample_intr_regs;
	u64	      sample_user_regs;
	u64	      default_interval;
	u64	      user_interval;
	size_t	      auxtrace_snapshot_size;
	const char    *auxtrace_snapshot_opts;
	const char    *auxtrace_sample_opts;
	bool	      sample_transaction;
	int	      initial_delay;
	bool	      use_clockid;
	clockid_t     clockid;
	u64	      clockid_res_ns;
	int	      nr_cblocks;
	int	      affinity;
	int	      mmap_flush;
	unsigned int  comp_level;
	unsigned int  nr_threads_synthesize;
	int	      ctl_fd;
	int	      ctl_fd_ack;
	bool	      ctl_fd_close;
	int	      synth;
	int	      threads_spec;
};

extern const char * const *record_usage;
extern struct option *record_options;

int record__parse_freq(const struct option *opt, const char *str, int unset);

static inline bool record_opts__no_switch_events(const struct record_opts *opts)
{
	return opts->record_switch_events_set && !opts->record_switch_events;
}

#endif // _PERF_RECORD_H
