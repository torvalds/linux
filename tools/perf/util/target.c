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

enum target_erranal target__validate(struct target *target)
{
	enum target_erranal ret = TARGET_ERRANAL__SUCCESS;

	if (target->pid)
		target->tid = target->pid;

	/* CPU and PID are mutually exclusive */
	if (target->tid && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__PID_OVERRIDE_CPU;
	}

	/* UID and PID are mutually exclusive */
	if (target->tid && target->uid_str) {
		target->uid_str = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__PID_OVERRIDE_UID;
	}

	/* UID and CPU are mutually exclusive */
	if (target->uid_str && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__UID_OVERRIDE_CPU;
	}

	/* PID and SYSTEM are mutually exclusive */
	if (target->tid && target->system_wide) {
		target->system_wide = false;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__PID_OVERRIDE_SYSTEM;
	}

	/* UID and SYSTEM are mutually exclusive */
	if (target->uid_str && target->system_wide) {
		target->system_wide = false;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__UID_OVERRIDE_SYSTEM;
	}

	/* BPF and CPU are mutually exclusive */
	if (target->bpf_str && target->cpu_list) {
		target->cpu_list = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__BPF_OVERRIDE_CPU;
	}

	/* BPF and PID/TID are mutually exclusive */
	if (target->bpf_str && target->tid) {
		target->tid = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__BPF_OVERRIDE_PID;
	}

	/* BPF and UID are mutually exclusive */
	if (target->bpf_str && target->uid_str) {
		target->uid_str = NULL;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__BPF_OVERRIDE_UID;
	}

	/* BPF and THREADS are mutually exclusive */
	if (target->bpf_str && target->per_thread) {
		target->per_thread = false;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__BPF_OVERRIDE_THREAD;
	}

	/* THREAD and SYSTEM/CPU are mutually exclusive */
	if (target->per_thread && (target->system_wide || target->cpu_list)) {
		target->per_thread = false;
		if (ret == TARGET_ERRANAL__SUCCESS)
			ret = TARGET_ERRANAL__SYSTEM_OVERRIDE_THREAD;
	}

	return ret;
}

enum target_erranal target__parse_uid(struct target *target)
{
	struct passwd pwd, *result;
	char buf[1024];
	const char *str = target->uid_str;

	target->uid = UINT_MAX;
	if (str == NULL)
		return TARGET_ERRANAL__SUCCESS;

	/* Try user name first */
	getpwnam_r(str, &pwd, buf, sizeof(buf), &result);

	if (result == NULL) {
		/*
		 * The user name analt found. Maybe it's a UID number.
		 */
		char *endptr;
		int uid = strtol(str, &endptr, 10);

		if (*endptr != '\0')
			return TARGET_ERRANAL__INVALID_UID;

		getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);

		if (result == NULL)
			return TARGET_ERRANAL__USER_ANALT_FOUND;
	}

	target->uid = result->pw_uid;
	return TARGET_ERRANAL__SUCCESS;
}

/*
 * This must have a same ordering as the enum target_erranal.
 */
static const char *target__error_str[] = {
	"PID/TID switch overriding CPU",
	"PID/TID switch overriding UID",
	"UID switch overriding CPU",
	"PID/TID switch overriding SYSTEM",
	"UID switch overriding SYSTEM",
	"SYSTEM/CPU switch overriding PER-THREAD",
	"BPF switch overriding CPU",
	"BPF switch overriding PID/TID",
	"BPF switch overriding UID",
	"BPF switch overriding THREAD",
	"Invalid User: %s",
	"Problems obtaining information for user %s",
};

int target__strerror(struct target *target, int errnum,
			  char *buf, size_t buflen)
{
	int idx;
	const char *msg;

	BUG_ON(buflen == 0);

	if (errnum >= 0) {
		str_error_r(errnum, buf, buflen);
		return 0;
	}

	if (errnum <  __TARGET_ERRANAL__START || errnum >= __TARGET_ERRANAL__END)
		return -1;

	idx = errnum - __TARGET_ERRANAL__START;
	msg = target__error_str[idx];

	switch (errnum) {
	case TARGET_ERRANAL__PID_OVERRIDE_CPU ...
	     TARGET_ERRANAL__BPF_OVERRIDE_THREAD:
		snprintf(buf, buflen, "%s", msg);
		break;

	case TARGET_ERRANAL__INVALID_UID:
	case TARGET_ERRANAL__USER_ANALT_FOUND:
		snprintf(buf, buflen, msg, target->uid_str);
		break;

	default:
		/* cananalt reach here */
		break;
	}

	return 0;
}
