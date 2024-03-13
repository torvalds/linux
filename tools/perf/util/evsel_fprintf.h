// SPDX-License-Identifier: GPL-2.0
#ifndef __PERF_EVSEL_FPRINTF_H
#define __PERF_EVSEL_FPRINTF_H 1

#include <stdio.h>
#include <stdbool.h>

struct evsel;

struct perf_attr_details {
	bool freq;
	bool verbose;
	bool event_group;
	bool force;
	bool trace_fields;
};

int evsel__fprintf(struct evsel *evsel, struct perf_attr_details *details, FILE *fp);

#define EVSEL__PRINT_IP			(1<<0)
#define EVSEL__PRINT_SYM		(1<<1)
#define EVSEL__PRINT_DSO		(1<<2)
#define EVSEL__PRINT_SYMOFFSET		(1<<3)
#define EVSEL__PRINT_ONELINE		(1<<4)
#define EVSEL__PRINT_SRCLINE		(1<<5)
#define EVSEL__PRINT_UNKNOWN_AS_ADDR	(1<<6)
#define EVSEL__PRINT_CALLCHAIN_ARROW	(1<<7)
#define EVSEL__PRINT_SKIP_IGNORED	(1<<8)

struct addr_location;
struct perf_event_attr;
struct perf_sample;
struct callchain_cursor;
struct strlist;

int sample__fprintf_callchain(struct perf_sample *sample, int left_alignment,
			      unsigned int print_opts, struct callchain_cursor *cursor,
			      struct strlist *bt_stop_list, FILE *fp);

int sample__fprintf_sym(struct perf_sample *sample, struct addr_location *al,
			int left_alignment, unsigned int print_opts,
			struct callchain_cursor *cursor,
			struct strlist *bt_stop_list, FILE *fp);

typedef int (*attr__fprintf_f)(FILE *, const char *, const char *, void *);

int perf_event_attr__fprintf(FILE *fp, struct perf_event_attr *attr,
			     attr__fprintf_f attr__fprintf, void *priv);
#endif // __PERF_EVSEL_H
