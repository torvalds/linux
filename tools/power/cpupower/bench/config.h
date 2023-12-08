/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 */

/* initial loop count for the load calibration */
#define GAUGECOUNT	1500

/* default scheduling policy SCHED_OTHER */
#define SCHEDULER	SCHED_OTHER

#define PRIORITY_DEFAULT 0
#define PRIORITY_HIGH	 sched_get_priority_max(SCHEDULER)
#define PRIORITY_LOW	 sched_get_priority_min(SCHEDULER)

/* enable further debug messages */
#ifdef DEBUG
#define dprintf printf
#else
#define dprintf(...) do { } while (0)
#endif

