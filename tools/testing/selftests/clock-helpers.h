/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CLOCK_HELPERS_H
#define __CLOCK_HELPERS_H

#include <sys/types.h>
#include <time.h>

#define MSEC_PER_SEC	1000LL
#define USEC_PER_MSEC	1000LL
#define NSEC_PER_USEC	1000LL
#define NSEC_PER_MSEC	1000000LL
#define USEC_PER_SEC	1000000LL
#define NSEC_PER_SEC	1000000000LL
#define PSEC_PER_SEC	1000000000000LL
#define FSEC_PER_SEC	1000000000000000LL

#ifndef CLOCK_AUX
#define CLOCK_AUX	16
#endif

#ifndef MAX_AUX_CLOCKS
#define MAX_AUX_CLOCKS	8
#endif

#ifndef CLOCK_AUX_LAST
#define CLOCK_AUX_LAST	(CLOCK_AUX + MAX_AUX_CLOCKS - 1)
#endif

__attribute__((unused))
static inline const char *clock_name(clockid_t clockid)
{
	switch (clockid) {
	case CLOCK_REALTIME:
		return "CLOCK_REALTIME";
	case CLOCK_MONOTONIC:
		return "CLOCK_MONOTONIC";
	case CLOCK_PROCESS_CPUTIME_ID:
		return "CLOCK_PROCESS_CPUTIME_ID";
	case CLOCK_THREAD_CPUTIME_ID:
		return "CLOCK_THREAD_CPUTIME_ID";
	case CLOCK_MONOTONIC_RAW:
		return "CLOCK_MONOTONIC_RAW";
	case CLOCK_REALTIME_COARSE:
		return "CLOCK_REALTIME_COARSE";
	case CLOCK_MONOTONIC_COARSE:
		return "CLOCK_MONOTONIC_COARSE";
	case CLOCK_BOOTTIME:
		return "CLOCK_BOOTTIME";
	case CLOCK_REALTIME_ALARM:
		return "CLOCK_REALTIME_ALARM";
	case CLOCK_BOOTTIME_ALARM:
		return "CLOCK_BOOTTIME_ALARM";
	case CLOCK_TAI:
		return "CLOCK_TAI";
	case CLOCK_AUX + 0:
		return "CLOCK_AUX0";
	case CLOCK_AUX + 1:
		return "CLOCK_AUX1";
	case CLOCK_AUX + 2:
		return "CLOCK_AUX2";
	case CLOCK_AUX + 3:
		return "CLOCK_AUX3";
	case CLOCK_AUX + 4:
		return "CLOCK_AUX4";
	case CLOCK_AUX + 5:
		return "CLOCK_AUX5";
	case CLOCK_AUX + 6:
		return "CLOCK_AUX6";
	case CLOCK_AUX + 7:
		return "CLOCK_AUX7";
	};
	return "UNKNOWN_CLOCKID";
}

#endif /* __CLOCK_HELPERS_H */
