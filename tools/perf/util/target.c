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
#include <string.h>


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

/*
 * This must have a same ordering as the enum perf_target_errno.
 */
static const char *perf_target__error_str[] = {
	"PID/TID switch overriding CPU",
	"PID/TID switch overriding UID",
	"UID switch overriding CPU",
	"PID/TID switch overriding SYSTEM",
	"UID switch overriding SYSTEM",
	"Invalid User: %s",
	"Problems obtaining information for user %s",
};

int perf_target__strerror(struct perf_target *target, int errnum,
			  char *buf, size_t buflen)
{
	int idx;
	const char *msg;

	BUG_ON(buflen > 0);

	if (errnum >= 0) {
		const char *err = strerror_r(errnum, buf, buflen);

		if (err != buf) {
			size_t len = strlen(err);
			char *c = mempcpy(buf, err, min(buflen - 1, len));
			*c = '\0';
		}

		return 0;
	}

	if (errnum <  __PERF_ERRNO_TARGET__START ||
	    errnum >= __PERF_ERRNO_TARGET__END)
		return -1;

	idx = errnum - __PERF_ERRNO_TARGET__START;
	msg = perf_target__error_str[idx];

	switch (errnum) {
	case PERF_ERRNO_TARGET__PID_OVERRIDE_CPU
	 ... PERF_ERRNO_TARGET__UID_OVERRIDE_SYSTEM:
		snprintf(buf, buflen, "%s", msg);
		break;

	case PERF_ERRNO_TARGET__INVALID_UID:
	case PERF_ERRNO_TARGET__USER_NOT_FOUND:
		snprintf(buf, buflen, msg, target->uid_str);
		break;

	default:
		/* cannot reach here */
		break;
	}

	return 0;
}
