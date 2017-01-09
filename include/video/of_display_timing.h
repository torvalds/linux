/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * display timings of helpers
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_OF_DISPLAY_TIMING_H
#define __LINUX_OF_DISPLAY_TIMING_H

struct device_node;
struct display_timing;
struct display_timings;

#define OF_USE_NATIVE_MODE -1

#ifdef CONFIG_OF
int of_get_display_timing(const struct device_node *np, const char *name,
		struct display_timing *dt);
struct display_timings *of_get_display_timings(const struct device_node *np);
int of_display_timings_exist(const struct device_node *np);
#else
static inline int of_get_display_timing(const struct device_node *np,
		const char *name, struct display_timing *dt)
{
	return -ENOSYS;
}
static inline struct display_timings *
of_get_display_timings(const struct device_node *np)
{
	return NULL;
}
static inline int of_display_timings_exist(const struct device_node *np)
{
	return -ENOSYS;
}
#endif

#endif
