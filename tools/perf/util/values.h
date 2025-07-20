/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_VALUES_H
#define __PERF_VALUES_H

#include <stdio.h>
#include <linux/types.h>

struct evsel;

struct perf_read_values {
	int threads;
	int threads_max;
	u32 *pid, *tid;
	int num_counters;
	int counters_max;
	struct evsel **counters;
	u64 **value;
};

int perf_read_values_init(struct perf_read_values *values);
void perf_read_values_destroy(struct perf_read_values *values);

int perf_read_values_add_value(struct perf_read_values *values,
				u32 pid, u32 tid,
				struct evsel *evsel, u64 value);

void perf_read_values_display(FILE *fp, struct perf_read_values *values,
			      int raw);

#endif /* __PERF_VALUES_H */
