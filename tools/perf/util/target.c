/*
 * Helper functions for handling target threads/cpus
 *
 * Copyright (C) 2012, LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 *
 * Released under the GPL v2.
 */

#include "target.h"
#include "debug.h"


enum perf_target_errno perf_target__validate(struct perf_target *target)
{
	enum perf_target_errno ret = PERF_ERRNO_TARGET__SUCCESS;

	if (target->pid)
		target->tid = target->pid;

	/* CPU and PID are mutually exclusive */
	if (target->tid && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == PERF_ERRNO_TARGET__SUCCESS)
			ret = PERF_ERRNO_TARGET__PID_OVERRIDE_CPU;
	}

	/* UID and PID are mutually exclusive */
	if (target->tid && target->uid_str) {
		target->uid_str = NULL;
		if (ret == PERF_ERRNO_TARGET__SUCCESS)
			ret = PERF_ERRNO_TARGET__PID_OVERRIDE_UID;
	}

	/* UID and CPU are mutually exclusive */
	if (target->uid_str && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == PERF_ERRNO_TARGET__SUCCESS)
			ret = PERF_ERRNO_TARGET__UID_OVERRIDE_CPU;
	}

	/* PID and SYSTEM are mutually exclusive */
	if (target->tid && target->system_wide) {
		target->system_wide = false;
		if (ret == PERF_ERRNO_TARGET__SUCCESS)
			ret = PERF_ERRNO_TARGET__PID_OVERRIDE_SYSTEM;
	}

	/* UID and SYSTEM are mutually exclusive */
	if (target->uid_str && target->system_wide) {
		target->system_wide = false;
		if (ret == PERF_ERRNO_TARGET__SUCCESS)
			ret = PERF_ERRNO_TARGET__UID_OVERRIDE_SYSTEM;
	}

	return ret;
}
