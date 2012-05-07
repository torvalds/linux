/*
 * Helper functions for handling target threads/cpus
 *
 * Copyright (C) 2012, LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 *
 * Released under the GPL v2.
 */

#include "target.h"
#include "debug.h"

#include <pwd.h>


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

enum perf_target_errno perf_target__parse_uid(struct perf_target *target)
{
	struct passwd pwd, *result;
	char buf[1024];
	const char *str = target->uid_str;

	target->uid = UINT_MAX;
	if (str == NULL)
		return PERF_ERRNO_TARGET__SUCCESS;

	/* Try user name first */
	getpwnam_r(str, &pwd, buf, sizeof(buf), &result);

	if (result == NULL) {
		/*
		 * The user name not found. Maybe it's a UID number.
		 */
		char *endptr;
		int uid = strtol(str, &endptr, 10);

		if (*endptr != '\0')
			return PERF_ERRNO_TARGET__INVALID_UID;

		getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);

		if (result == NULL)
			return PERF_ERRNO_TARGET__USER_NOT_FOUND;
	}

	target->uid = result->pw_uid;
	return PERF_ERRNO_TARGET__SUCCESS;
}
