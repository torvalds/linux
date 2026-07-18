/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2026 Google LLC */

#ifndef __WAKEUP_SOURCE_H__
#define __WAKEUP_SOURCE_H__

#define WAKEUP_NAME_LEN 128

struct wakeup_event_t {
	unsigned long active_count;
	long long active_time_ns;
	unsigned long event_count;
	unsigned long expire_count;
	long long last_time_ns;
	long long max_time_ns;
	long long prevent_sleep_time_ns;
	long long total_time_ns;
	unsigned long wakeup_count;
	char name[WAKEUP_NAME_LEN];
};

#endif /* __WAKEUP_SOURCE_H__ */
