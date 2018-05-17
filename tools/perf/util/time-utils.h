/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TIME_UTILS_H_
#define _TIME_UTILS_H_

#include <stddef.h>
#include <linux/types.h>

struct perf_time_interval {
	u64 start, end;
};

int parse_nsec_time(const char *str, u64 *ptime);

int perf_time__parse_str(struct perf_time_interval *ptime, const char *ostr);

int perf_time__percent_parse_str(struct perf_time_interval *ptime_buf, int num,
				 const char *ostr, u64 start, u64 end);

struct perf_time_interval *perf_time__range_alloc(const char *ostr, int *size);

bool perf_time__skip_sample(struct perf_time_interval *ptime, u64 timestamp);

bool perf_time__ranges_skip_sample(struct perf_time_interval *ptime_buf,
				   int num, u64 timestamp);

int timestamp__scnprintf_usec(u64 timestamp, char *buf, size_t sz);

int fetch_current_timestamp(char *buf, size_t sz);

#endif
