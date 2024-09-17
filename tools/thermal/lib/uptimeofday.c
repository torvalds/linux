// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <stdio.h>
#include <sys/time.h>
#include <linux/sysinfo.h>
#include "thermal-tools.h"

static unsigned long __offset;
static struct timeval __tv;

int uptimeofday_init(void)
{
	struct sysinfo info;

	if (sysinfo(&info))
		return -1;

	gettimeofday(&__tv, NULL);

	__offset = __tv.tv_sec - info.uptime;

	return 0;
}

unsigned long getuptimeofday_ms(void)
{
	gettimeofday(&__tv, NULL);

	return ((__tv.tv_sec - __offset) * 1000) + (__tv.tv_usec / 1000);
}

struct timespec msec_to_timespec(int msec)
{
	struct timespec tv = {
		.tv_sec = (msec / 1000),
		.tv_nsec = (msec % 1000) * 1000000,
	};

	return tv;
}
