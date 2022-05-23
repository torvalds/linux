/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org> */
#ifndef __THERMAL_TOOLS_UPTIMEOFDAY_H
#define __THERMAL_TOOLS_UPTIMEOFDAY_H
#include <sys/sysinfo.h>
#include <sys/time.h>

int uptimeofday_init(void);
unsigned long getuptimeofday_ms(void);
struct timespec msec_to_timespec(int msec);

#endif
