/* SPDX-License-Identifier: LGPL-2.1 */
#ifndef __PERF_RLIMIT_H_
#define __PERF_RLIMIT_H_

enum rlimit_action {
	NO_CHANGE,
	SET_TO_MAX,
	INCREASED_MAX
};

void rlimit__bump_memlock(void);

bool rlimit__increase_nofile(enum rlimit_action *set_rlimit);

#endif // __PERF_RLIMIT_H_
