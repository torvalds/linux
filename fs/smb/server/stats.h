/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2025, LG Electronics.
 *   Author(s): Hyunchul Lee <hyc.lee@gmail.com>
 *   Copyright (C) 2025, Samsung Electronics.
 *   Author(s): Vedansh Bhardwaj <v.bhardwaj@samsung.com>
 */

#ifndef __KSMBD_STATS_H__
#define __KSMBD_STATS_H__

#include "../common/smb2status.h"

#define KSMBD_COUNTER_MAX_REQS	19

enum {
	KSMBD_COUNTER_SESSIONS = 0,
	KSMBD_COUNTER_TREE_CONNS,
	KSMBD_COUNTER_REQUESTS,
	KSMBD_COUNTER_STATUS_SUCCESS,
	KSMBD_COUNTER_STATUS_INFORMATIONAL,
	KSMBD_COUNTER_STATUS_WARNING,
	KSMBD_COUNTER_STATUS_ERROR,
	KSMBD_COUNTER_ERROR_ACCESS_DENIED,
	KSMBD_COUNTER_ERROR_NOT_FOUND,
	KSMBD_COUNTER_ERROR_INVALID_PARAMETER,
	KSMBD_COUNTER_ERROR_SHARING_VIOLATION,
	KSMBD_COUNTER_ERROR_NOT_SUPPORTED,
	KSMBD_COUNTER_ERROR_OTHER,
	KSMBD_COUNTER_READ_BYTES,
	KSMBD_COUNTER_WRITE_BYTES,
	KSMBD_COUNTER_FIRST_REQ,
	KSMBD_COUNTER_LAST_REQ = KSMBD_COUNTER_FIRST_REQ +
				KSMBD_COUNTER_MAX_REQS - 1,
	KSMBD_COUNTER_MAX,
};

#ifdef CONFIG_PROC_FS
extern struct ksmbd_counters ksmbd_counters;

struct ksmbd_counters {
	struct percpu_counter	counters[KSMBD_COUNTER_MAX];
};

static inline void ksmbd_counter_inc(int type)
{
	percpu_counter_inc(&ksmbd_counters.counters[type]);
}

static inline void ksmbd_counter_dec(int type)
{
	percpu_counter_dec(&ksmbd_counters.counters[type]);
}

static inline void ksmbd_counter_add(int type, s64 value)
{
	percpu_counter_add(&ksmbd_counters.counters[type], value);
}

static inline void ksmbd_counter_sub(int type, s64 value)
{
	percpu_counter_sub(&ksmbd_counters.counters[type], value);
}

static inline void ksmbd_counter_inc_reqs(unsigned int cmd, __le32 status)
{
	unsigned int severity = le32_to_cpu(status) >> 30;
	int type;

	switch (severity) {
	case 0:
		type = KSMBD_COUNTER_STATUS_SUCCESS;
		break;
	case 1:
		type = KSMBD_COUNTER_STATUS_INFORMATIONAL;
		break;
	case 2:
		type = KSMBD_COUNTER_STATUS_WARNING;
		break;
	default:
		type = KSMBD_COUNTER_STATUS_ERROR;
		break;
	}
	percpu_counter_inc(&ksmbd_counters.counters[type]);

	if (severity == 3) {
		if (status == STATUS_ACCESS_DENIED)
			type = KSMBD_COUNTER_ERROR_ACCESS_DENIED;
		else if (status == STATUS_OBJECT_NAME_NOT_FOUND ||
			 status == STATUS_NO_SUCH_FILE)
			type = KSMBD_COUNTER_ERROR_NOT_FOUND;
		else if (status == STATUS_INVALID_PARAMETER)
			type = KSMBD_COUNTER_ERROR_INVALID_PARAMETER;
		else if (status == STATUS_SHARING_VIOLATION)
			type = KSMBD_COUNTER_ERROR_SHARING_VIOLATION;
		else if (status == STATUS_NOT_SUPPORTED ||
			 status == STATUS_NOT_IMPLEMENTED)
			type = KSMBD_COUNTER_ERROR_NOT_SUPPORTED;
		else
			type = KSMBD_COUNTER_ERROR_OTHER;
		percpu_counter_inc(&ksmbd_counters.counters[type]);
	}

	if (cmd < KSMBD_COUNTER_MAX_REQS) {
		percpu_counter_inc(&ksmbd_counters.counters[KSMBD_COUNTER_REQUESTS]);
		percpu_counter_inc(&ksmbd_counters.counters[KSMBD_COUNTER_FIRST_REQ + cmd]);
	}
}

static inline s64 ksmbd_counter_sum(int type)
{
	return percpu_counter_sum_positive(&ksmbd_counters.counters[type]);
}
#else

static inline void ksmbd_counter_inc(int type) {}
static inline void ksmbd_counter_dec(int type) {}
static inline void ksmbd_counter_add(int type, s64 value) {}
static inline void ksmbd_counter_sub(int type, s64 value) {}
static inline void ksmbd_counter_inc_reqs(unsigned int cmd, __le32 status) {}
static inline s64 ksmbd_counter_sum(int type) { return 0; }
#endif

#endif
