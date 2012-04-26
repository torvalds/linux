#ifndef _PERF_TARGET_H
#define _PERF_TARGET_H

#include <stdbool.h>
#include <sys/types.h>

struct perf_target {
	const char   *pid;
	const char   *tid;
	const char   *cpu_list;
	const char   *uid_str;
	uid_t	     uid;
	bool	     system_wide;
};

void perf_target__validate(struct perf_target *target);

#endif /* _PERF_TARGET_H */
