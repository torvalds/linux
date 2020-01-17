/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * display timings of helpers
 */

#ifndef __LINUX_OF_DISPLAY_TIMING_H
#define __LINUX_OF_DISPLAY_TIMING_H

struct device_yesde;
struct display_timing;
struct display_timings;

#define OF_USE_NATIVE_MODE -1

#ifdef CONFIG_OF
int of_get_display_timing(const struct device_yesde *np, const char *name,
		struct display_timing *dt);
struct display_timings *of_get_display_timings(const struct device_yesde *np);
#else
static inline int of_get_display_timing(const struct device_yesde *np,
		const char *name, struct display_timing *dt)
{
	return -ENOSYS;
}
static inline struct display_timings *
of_get_display_timings(const struct device_yesde *np)
{
	return NULL;
}
#endif

#endif
