/*
 * Copyright (c) 2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <errno.h>
#include "fido.h"

static int
timespec_to_ms(const struct timespec *ts)
{
	int64_t x, y;

	if (ts->tv_sec < 0 || ts->tv_nsec < 0 ||
	    ts->tv_nsec >= 1000000000LL)
		return -1;

	if ((uint64_t)ts->tv_sec >= INT64_MAX / 1000LL)
		return -1;

	x = ts->tv_sec * 1000LL;
	y = ts->tv_nsec / 1000000LL;

	if (INT64_MAX - x < y || x + y > INT_MAX)
		return -1;

	return (int)(x + y);
}

int
fido_time_now(struct timespec *ts_now)
{
	if (clock_gettime(CLOCK_MONOTONIC, ts_now) != 0) {
		fido_log_error(errno, "%s: clock_gettime", __func__);
		return -1;
	}

	return 0;
}

int
fido_time_delta(const struct timespec *ts_start, int *ms_remain)
{
	struct timespec ts_end, ts_delta;
	int ms;

	if (*ms_remain < 0)
		return 0;

	if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0) {
		fido_log_error(errno, "%s: clock_gettime", __func__);
		return -1;
	}

	if (timespeccmp(&ts_end, ts_start, <)) {
		fido_log_debug("%s: timespeccmp", __func__);
		return -1;
	}

	timespecsub(&ts_end, ts_start, &ts_delta);

	if ((ms = timespec_to_ms(&ts_delta)) < 0) {
		fido_log_debug("%s: timespec_to_ms", __func__);
		return -1;
	}

	if (ms > *ms_remain)
		ms = *ms_remain;

	*ms_remain -= ms;

	return 0;
}
