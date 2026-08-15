// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2026, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Validate that LUO can handle a large number of files per session across
 * a kexec reboot.
 */

#include <stdio.h>
#include <unistd.h>
#include "luo_test_utils.h"

#define NUM_FILES 500
#define STATE_SESSION_NAME "kexec_many_files_state"
#define STATE_MEMFD_TOKEN 9999
#define TEST_SESSION_NAME "many_files_session"

/* Stage 1: Executed before the kexec reboot. */
static void run_stage_1(int luo_fd)
{
	int session_fd, i;

	ksft_print_msg("[STAGE 1] Creating state file for next stage (2)...\n");
	create_state_file(luo_fd, STATE_SESSION_NAME, STATE_MEMFD_TOKEN, 2);

	ksft_print_msg("[STAGE 1] Creating test session '%s'...\n", TEST_SESSION_NAME);
	session_fd = luo_create_session(luo_fd, TEST_SESSION_NAME);
	if (session_fd < 0)
		fail_exit("luo_create_session");

	ksft_print_msg("[STAGE 1] Preserving %d files...\n", NUM_FILES);
	for (i = 0; i < NUM_FILES; i++) {
		char data[64];

		snprintf(data, sizeof(data), "file-data-%d", i);
		if (create_and_preserve_memfd(session_fd, i, data) < 0)
			fail_exit("create_and_preserve_memfd for index %d", i);
	}

	ksft_print_msg("[STAGE 1] Successfully preserved %d files.\n", NUM_FILES);

	close(luo_fd);
	daemonize_and_wait();
}

/* Stage 2: Executed after the kexec reboot. */
static void run_stage_2(int luo_fd, int state_session_fd)
{
	int session_fd;
	int i, stage;

	ksft_print_msg("[STAGE 2] Starting post-kexec verification...\n");

	restore_and_read_stage(state_session_fd, STATE_MEMFD_TOKEN, &stage);
	if (stage != 2) {
		fail_exit("Expected stage 2, but state file contains %d",
			  stage);
	}

	ksft_print_msg("[STAGE 2] Retrieving test session '%s'...\n", TEST_SESSION_NAME);
	session_fd = luo_retrieve_session(luo_fd, TEST_SESSION_NAME);
	if (session_fd < 0)
		fail_exit("luo_retrieve_session");

	ksft_print_msg("[STAGE 2] Verifying %d files...\n", NUM_FILES);
	for (i = 0; i < NUM_FILES; i++) {
		char data[64];
		int fd;

		snprintf(data, sizeof(data), "file-data-%d", i);
		fd = restore_and_verify_memfd(session_fd, i, data);
		if (fd < 0)
			fail_exit("restore_and_verify_memfd for index %d", i);
		close(fd);
	}

	ksft_print_msg("[STAGE 2] Finishing test session...\n");
	if (luo_session_finish(session_fd) < 0)
		fail_exit("luo_session_finish for test session");
	close(session_fd);

	ksft_print_msg("[STAGE 2] Finalizing state session...\n");
	if (luo_session_finish(state_session_fd) < 0)
		fail_exit("luo_session_finish for state session");
	close(state_session_fd);

	ksft_print_msg("\n--- MANY-FILES KEXEC TEST PASSED (%d files) ---\n",
		       NUM_FILES);
}

int main(int argc, char *argv[])
{
	return luo_test(argc, argv, STATE_SESSION_NAME,
			run_stage_1, run_stage_2);
}
