// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helper functions for handling target threads/cpus
 *
 * Copyright (C) 2012, LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 */

#include "target.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/string.h>

enum target_errno target__validate(struct target *target)
{
	enum target_errno ret = TARGET_ERRNO__SUCCESS;

	if (target->pid)
		target->tid = target->pid;

	/* CPU and PID are mutually exclusive */
	if (target->tid && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__PID_OVERRIDE_CPU;
	}

	/* PID and SYSTEM are mutually exclusive */
	if (target->tid && target->system_wide) {
		target->system_wide = false;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__PID_OVERRIDE_SYSTEM;
	}

	/* BPF and CPU are mutually exclusive */
	if (target->bpf_str && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__BPF_OVERRIDE_CPU;
	}

	/* BPF and PID/TID are mutually exclusive */
	if (target->bpf_str && target->tid) {
		target->tid = NULL;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__BPF_OVERRIDE_PID;
	}

	/* BPF and THREADS are mutually exclusive */
	if (target->bpf_str && target->per_thread) {
		target->per_thread = false;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__BPF_OVERRIDE_THREAD;
	}

	/* THREAD and SYSTEM/CPU are mutually exclusive */
	if (target->per_thread && (target->system_wide || target->cpu_list)) {
		target->per_thread = false;
		if (ret == TARGET_ERRNO__SUCCESS)
			ret = TARGET_ERRNO__SYSTEM_OVERRIDE_THREAD;
	}

	return ret;
}

uid_t parse_uid(const char *str)
{
	struct passwd pwd, *result;
	char buf[1024];

	if (str == NULL)
		return UINT_MAX;

	/* Try user name first */
	getpwnam_r(str, &pwd, buf, sizeof(buf), &result);

	if (result == NULL) {
		/*
		 * The user name not found. Maybe it's a UID number.
		 */
		char *endptr;
		int uid = strtol(str, &endptr, 10);

		if (*endptr != '\0')
			return UINT_MAX;

		getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);

		if (result == NULL)
			return UINT_MAX;
	}

	return result->pw_uid;
}

/*
 * This must have a same ordering as the enum target_errno.
 */
static const char *target__error_str[] = {
	"PID/TID switch overriding CPU",
	"PID/TID switch overriding SYSTEM",
	"SYSTEM/CPU switch overriding PER-THREAD",
	"BPF switch overriding CPU",
	"BPF switch overriding PID/TID",
	"BPF switch overriding THREAD",
};

int target__strerror(struct target *target __maybe_unused, int errnum,
			  char *buf, size_t buflen)
{
	int idx;
	const char *msg;

	BUG_ON(buflen == 0);

	if (errnum >= 0) {
		str_error_r(errnum, buf, buflen);
		return 0;
	}

	if (errnum <  __TARGET_ERRNO__START || errnum >= __TARGET_ERRNO__END)
		return -1;

	idx = errnum - __TARGET_ERRNO__START;
	msg = target__error_str[idx];

	switch (errnum) {
	case TARGET_ERRNO__PID_OVERRIDE_CPU ...
	     TARGET_ERRNO__BPF_OVERRIDE_THREAD:
		snprintf(buf, buflen, "%s", msg);
		break;

	default:
		/* cannot reach here */
		break;
	}

	return 0;
}
