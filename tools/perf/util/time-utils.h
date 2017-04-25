#ifndef _TIME_UTILS_H_
#define _TIME_UTILS_H_

struct perf_time_interval {
	u64 start, end;
};

int parse_nsec_time(const char *str, u64 *ptime);

int perf_time__parse_str(struct perf_time_interval *ptime, const char *ostr);

bool perf_time__skip_sample(struct perf_time_interval *ptime, u64 timestamp);

#endif
