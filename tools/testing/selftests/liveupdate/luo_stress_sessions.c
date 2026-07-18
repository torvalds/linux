// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2026, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Validate that LUO can handle a large number of sessions across a kexec
 * reboot.
 */

#include <stdio.h>
#include <unistd.h>
#include "luo_test_utils.h"

#define NUM_SESSIONS 2000
#define STATE_SESSION_NAME "kexec_many_state"
#define STATE_MEMFD_TOKEN 999

/* Stage 1: Executed before the kexec reboot. */
static void run_stage_1(int luo_fd)
{
	int ret, i;

	ksft_print_msg("[STAGE 1] Increasing ulimit for open files...\n");
	ret = luo_ensure_nofile_limit(NUM_SESSIONS);
	if (ret == -EPERM)
		ksft_exit_skip("Insufficient privileges to set RLIMIT_NOFILE\n");
	if (ret < 0)
		ksft_exit_fail_msg("luo_ensure_nofile_limit failed: %s\n", strerror(-ret));

	ksft_print_msg("[STAGE 1] Creating state file for next stage (2)...\n");
	create_state_file(luo_fd, STATE_SESSION_NAME, STATE_MEMFD_TOKEN, 2);

	ksft_print_msg("[STAGE 1] Creating %d sessions...\n", NUM_SESSIONS);

	for (i = 0; i < NUM_SESSIONS; i++) {
		char name[LIVEUPDATE_SESSION_NAME_LENGTH];
		int s_fd;

		snprintf(name, sizeof(name), "many-test-%d", i);
		s_fd = luo_create_session(luo_fd, name);
		if (s_fd < 0) {
			fail_exit("luo_create_session for '%s' at index %d",
				  name, i);
		}
	}

	ksft_print_msg("[STAGE 1] Successfully created %d sessions.\n",
		       NUM_SESSIONS);

	close(luo_fd);
	daemonize_and_wait();
}

/* Stage 2: Executed after the kexec reboot. */
static void run_stage_2(int luo_fd, int state_session_fd)
{
	int i, stage;

	ksft_print_msg("[STAGE 2] Starting post-kexec verification...\n");

	restore_and_read_stage(state_session_fd, STATE_MEMFD_TOKEN, &stage);
	if (stage != 2) {
		fail_exit("Expected stage 2, but state file contains %d",
			  stage);
	}

	ksft_print_msg("[STAGE 2] Retrieving and finishing %d sessions...\n",
		       NUM_SESSIONS);

	for (i = 0; i < NUM_SESSIONS; i++) {
		char name[LIVEUPDATE_SESSION_NAME_LENGTH];
		int s_fd;

		snprintf(name, sizeof(name), "many-test-%d", i);
		s_fd = luo_retrieve_session(luo_fd, name);
		if (s_fd < 0) {
			fail_exit("luo_retrieve_session for '%s' at index %d",
				  name, i);
		}

		if (luo_session_finish(s_fd) < 0) {
			fail_exit("luo_session_finish for '%s' at index %d",
				  name, i);
		}
		close(s_fd);
	}

	ksft_print_msg("[STAGE 2] Finalizing state session...\n");
	if (luo_session_finish(state_session_fd) < 0)
		fail_exit("luo_session_finish for state session");
	close(state_session_fd);

	ksft_print_msg("\n--- MANY-SESSIONS KEXEC TEST PASSED (%d sessions) ---\n",
		       NUM_SESSIONS);
}

int main(int argc, char *argv[])
{
	return luo_test(argc, argv, STATE_SESSION_NAME,
			run_stage_1, run_stage_2);
}
