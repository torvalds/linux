/*
 * Helper functions for handling target threads/cpus
 *
 * Copyright (C) 2012, LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 *
 * Released under the GPL v2.
 */

#include "target.h"
#include "debug.h"


void perf_target__validate(struct perf_target *target)
{
	if (target->pid)
		target->tid = target->pid;

	/* CPU and PID are mutually exclusive */
	if (target->tid && target->cpu_list) {
		ui__warning("WARNING: PID switch overriding CPU\n");
		sleep(1);
		target->cpu_list = NULL;
	}

	/* UID and PID are mutually exclusive */
	if (target->tid && target->uid_str) {
		ui__warning("PID/TID switch overriding UID\n");
		sleep(1);
		target->uid_str = NULL;
	}

	/* UID and CPU are mutually exclusive */
	if (target->uid_str && target->cpu_list) {
		ui__warning("UID switch overriding CPU\n");
		sleep(1);
		target->cpu_list = NULL;
	}

	/* PID/UID and SYSTEM are mutually exclusive */
	if ((target->tid || target->uid_str) && target->system_wide) {
		ui__warning("PID/TID/UID switch overriding CPU\n");
		sleep(1);
		target->system_wide = false;
	}
}
