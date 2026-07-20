/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Utility functions for LUO kselftests.
 */

#ifndef SELFTESTS_LIVEUPDATE_LIB_LIVEUPDATE_H
#define SELFTESTS_LIVEUPDATE_LIB_LIVEUPDATE_H

#include <errno.h>
#include <string.h>
#include <linux/liveupdate.h>
#include "../../../kselftest.h"

#define LUO_DEVICE "/dev/liveupdate"

#define fail_exit(fmt, ...)						\
	ksft_exit_fail_msg("[%s:%d] " fmt " (errno: %s)\n",	\
			   __func__, __LINE__, ##__VA_ARGS__, strerror(errno))

int luo_open_device(void);
int luo_create_session(int luo_fd, const char *name);
int luo_retrieve_session(int luo_fd, const char *name);
int luo_session_finish(int session_fd);
int luo_get_session_name(int session_fd, char *name, size_t name_len);

int luo_ensure_nofile_limit(long min_limit);
int luo_session_preserve_fd(int session_fd, int fd, __u64 token);
int luo_session_retrieve_fd(int session_fd, __u64 token);

int create_and_preserve_memfd(int session_fd, int token, const char *data);
int restore_and_verify_memfd(int session_fd, int token, const char *expected_data);

void create_state_file(int luo_fd, const char *session_name, int token,
		       int next_stage);
void restore_and_read_stage(int state_session_fd, int token, int *stage);

void daemonize_and_wait(void);

typedef void (*luo_test_stage1_fn)(int luo_fd);
typedef void (*luo_test_stage2_fn)(int luo_fd, int state_session_fd);

int luo_test(int argc, char *argv[], const char *state_session_name,
	     luo_test_stage1_fn stage1, luo_test_stage2_fn stage2);

#endif /* SELFTESTS_LIVEUPDATE_LIB_LIVEUPDATE_H */
