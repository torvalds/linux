/* SPDX-License-Identifier: GPL-2.0 */
/*
 * perf iostat
 *
 * Copyright (C) 2020, Intel Corporation
 *
 * Authors: Alexander Antonov <alexander.antonov@linux.intel.com>
 */

#ifndef _IOSTAT_H
#define _IOSTAT_H

#include <subcmd/parse-options.h>
#include "util/stat.h"
#include "util/parse-events.h"
#include "util/evlist.h"

struct option;
struct perf_stat_config;
struct evlist;
struct timespec;

enum iostat_mode_t {
	IOSTAT_NONE		= -1,
	IOSTAT_RUN		= 0,
	IOSTAT_LIST		= 1
};

extern enum iostat_mode_t iostat_mode;

typedef void (*iostat_print_counter_t)(struct perf_stat_config *, struct evsel *, const char *);

int iostat_prepare(struct evlist *evlist, struct perf_stat_config *config);
int iostat_parse(const struct option *opt, const char *str,
		 int unset __maybe_unused);
void iostat_list(struct evlist *evlist, struct perf_stat_config *config);
void iostat_release(struct evlist *evlist);
void iostat_prefix(struct evlist *evlist, struct perf_stat_config *config,
		   char *prefix, struct timespec *ts);
void iostat_print_header_prefix(struct perf_stat_config *config);
void iostat_print_metric(struct perf_stat_config *config, struct evsel *evsel,
			 struct perf_stat_output_ctx *out);
void iostat_print_counters(struct evlist *evlist,
			   struct perf_stat_config *config, struct timespec *ts,
			   char *prefix, iostat_print_counter_t print_cnt_cb);

#endif /* _IOSTAT_H */
