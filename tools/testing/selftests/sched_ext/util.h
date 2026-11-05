/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <void@manifault.com>
 */

#ifndef __SCX_TEST_UTIL_H__
#define __SCX_TEST_UTIL_H__

#include <stdbool.h>
#include <sys/types.h>

struct scx_test_gated_worker {
	pid_t pid;
	int start_fd;
};

long file_read_long(const char *path);
int file_write_long(const char *path, long val);
struct scx_test_gated_worker scx_test_spawn_gated_worker(int cpu, bool set_sched_ext);
bool scx_test_start_gated_worker(struct scx_test_gated_worker *worker);
void scx_test_stop_gated_worker(struct scx_test_gated_worker *worker);
int scx_test_preempt_lazy_mode(void);

#endif // __SCX_TEST_UTIL_H__
