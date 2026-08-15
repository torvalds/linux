/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ASLR_H
#define __PERF_ASLR_H

#include <linux/perf_event.h>

#define ASLR_SUPPORTED_SAMPLE_TYPE ( \
	PERF_SAMPLE_IDENTIFIER | \
	PERF_SAMPLE_IP | \
	PERF_SAMPLE_TID | \
	PERF_SAMPLE_TIME | \
	PERF_SAMPLE_ADDR | \
	PERF_SAMPLE_ID | \
	PERF_SAMPLE_STREAM_ID | \
	PERF_SAMPLE_CPU | \
	PERF_SAMPLE_PERIOD | \
	PERF_SAMPLE_READ | \
	PERF_SAMPLE_CALLCHAIN | \
	PERF_SAMPLE_RAW | \
	PERF_SAMPLE_BRANCH_STACK | \
	PERF_SAMPLE_STACK_USER | \
	PERF_SAMPLE_WEIGHT_TYPE | \
	PERF_SAMPLE_DATA_SRC | \
	PERF_SAMPLE_TRANSACTION | \
	PERF_SAMPLE_PHYS_ADDR | \
	PERF_SAMPLE_CGROUP | \
	PERF_SAMPLE_DATA_PAGE_SIZE | \
	PERF_SAMPLE_CODE_PAGE_SIZE | \
	PERF_SAMPLE_AUX)

struct perf_tool;
struct evsel;
struct evlist;
union perf_event;

struct perf_tool *aslr_tool__new(struct perf_tool *delegate);
void aslr_tool__delete(struct perf_tool *tool);

void aslr_tool__strip_attr_event(union perf_event *event, struct evlist *evlist);
int aslr_tool__cache_orig_attrs(struct perf_tool *tool, struct evsel *evsel);
void aslr_tool__strip_evlist(const struct perf_tool *tool, struct evlist *evlist);
void aslr_tool__restore_evlist(const struct perf_tool *tool, struct evlist *evlist);

#endif /* __PERF_ASLR_H */
