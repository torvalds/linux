// SPDX-License-Identifier: GPL-2.0
#ifndef __PERF_EVSEL_CONFIG_H
#define __PERF_EVSEL_CONFIG_H 1

#include <linux/types.h>
#include <stdbool.h>

/*
 * The 'struct perf_evsel_config_term' is used to pass event
 * specific configuration data to perf_evsel__config routine.
 * It is allocated within event parsing and attached to
 * perf_evsel::config_terms list head.
*/
enum evsel_term_type {
	PERF_EVSEL__CONFIG_TERM_PERIOD,
	PERF_EVSEL__CONFIG_TERM_FREQ,
	PERF_EVSEL__CONFIG_TERM_TIME,
	PERF_EVSEL__CONFIG_TERM_CALLGRAPH,
	PERF_EVSEL__CONFIG_TERM_STACK_USER,
	PERF_EVSEL__CONFIG_TERM_INHERIT,
	PERF_EVSEL__CONFIG_TERM_MAX_STACK,
	PERF_EVSEL__CONFIG_TERM_MAX_EVENTS,
	PERF_EVSEL__CONFIG_TERM_OVERWRITE,
	PERF_EVSEL__CONFIG_TERM_DRV_CFG,
	PERF_EVSEL__CONFIG_TERM_BRANCH,
	PERF_EVSEL__CONFIG_TERM_PERCORE,
	PERF_EVSEL__CONFIG_TERM_AUX_OUTPUT,
	PERF_EVSEL__CONFIG_TERM_AUX_SAMPLE_SIZE,
	PERF_EVSEL__CONFIG_TERM_CFG_CHG,
};

struct perf_evsel_config_term {
	struct list_head      list;
	enum evsel_term_type  type;
	union {
		u64	      period;
		u64	      freq;
		bool	      time;
		char	      *callgraph;
		char	      *drv_cfg;
		u64	      stack_user;
		int	      max_stack;
		bool	      inherit;
		bool	      overwrite;
		char	      *branch;
		unsigned long max_events;
		bool	      percore;
		bool	      aux_output;
		u32	      aux_sample_size;
		u64	      cfg_chg;
	} val;
	bool weak;
};

struct evsel;

struct perf_evsel_config_term *__perf_evsel__get_config_term(struct evsel *evsel,
							     enum evsel_term_type type);

#define perf_evsel__get_config_term(evsel, type) \
	__perf_evsel__get_config_term(evsel, PERF_EVSEL__CONFIG_TERM_ ## type)

#endif // __PERF_EVSEL_CONFIG_H
