// SPDX-License-Identifier: GPL-2.0
#ifndef __PERF_EVSEL_CONFIG_H
#define __PERF_EVSEL_CONFIG_H 1

#include <linux/types.h>
#include <stdbool.h>

/*
 * The 'struct evsel_config_term' is used to pass event
 * specific configuration data to evsel__config routine.
 * It is allocated within event parsing and attached to
 * evsel::config_terms list head.
*/
enum evsel_term_type {
	EVSEL__CONFIG_TERM_PERIOD,
	EVSEL__CONFIG_TERM_FREQ,
	EVSEL__CONFIG_TERM_TIME,
	EVSEL__CONFIG_TERM_CALLGRAPH,
	EVSEL__CONFIG_TERM_STACK_USER,
	EVSEL__CONFIG_TERM_INHERIT,
	EVSEL__CONFIG_TERM_MAX_STACK,
	EVSEL__CONFIG_TERM_MAX_EVENTS,
	EVSEL__CONFIG_TERM_OVERWRITE,
	EVSEL__CONFIG_TERM_DRV_CFG,
	EVSEL__CONFIG_TERM_BRANCH,
	EVSEL__CONFIG_TERM_PERCORE,
	EVSEL__CONFIG_TERM_AUX_OUTPUT,
	EVSEL__CONFIG_TERM_AUX_ACTION,
	EVSEL__CONFIG_TERM_AUX_SAMPLE_SIZE,
	EVSEL__CONFIG_TERM_CFG_CHG,
};

struct evsel_config_term {
	struct list_head      list;
	enum evsel_term_type  type;
	bool		      free_str;
	union {
		u64	      period;
		u64	      freq;
		bool	      time;
		u64	      stack_user;
		int	      max_stack;
		bool	      inherit;
		bool	      overwrite;
		unsigned long max_events;
		bool	      percore;
		bool	      aux_output;
		u32	      aux_sample_size;
		u64	      cfg_chg;
		char	      *str;
	} val;
	bool weak;
};

struct evsel;

struct evsel_config_term *__evsel__get_config_term(struct evsel *evsel, enum evsel_term_type type);

#define evsel__get_config_term(evsel, type) \
	__evsel__get_config_term(evsel, EVSEL__CONFIG_TERM_ ## type)

#endif // __PERF_EVSEL_CONFIG_H
