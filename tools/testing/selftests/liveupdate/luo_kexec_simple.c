// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * A simple selftest to validate the end-to-end lifecycle of a LUO session
 * across a single kexec reboot.
 */

#include "luo_test_utils.h"

#define TEST_SESSION_NAME "test-session"
#define TEST_MEMFD_TOKEN 0x1A
#define TEST_MEMFD_DATA "hello kexec world"

/* Constants for the state-tracking mechanism, specific to this test file. */
#define STATE_SESSION_NAME "kexec_simple_state"
#define STATE_MEMFD_TOKEN 999

/* Stage 1: Executed before the kexec reboot. */
static void run_stage_1(int luo_fd)
{
	int session_fd;

	ksft_print_msg("[STAGE 1] Starting pre-kexec setup...\n");

	ksft_print_msg("[STAGE 1] Creating state file for next stage (2)...\n");
	create_state_file(luo_fd, STATE_SESSION_NAME, STATE_MEMFD_TOKEN, 2);

	ksft_print_msg("[STAGE 1] Creating session '%s' and preserving memfd...\n",
		       TEST_SESSION_NAME);
	session_fd = luo_create_session(luo_fd, TEST_SESSION_NAME);
	if (session_fd < 0)
		fail_exit("luo_create_session for '%s'", TEST_SESSION_NAME);

	if (create_and_preserve_memfd(session_fd, TEST_MEMFD_TOKEN,
				      TEST_MEMFD_DATA) < 0) {
		fail_exit("create_and_preserve_memfd for token %#x",
			  TEST_MEMFD_TOKEN);
	}

	close(luo_fd);
	daemonize_and_wait();
}

/* Stage 2: Executed after the kexec reboot. */
static void run_stage_2(int luo_fd, int state_session_fd)
{
	int session_fd, mfd, stage;

	ksft_print_msg("[STAGE 2] Starting post-kexec verification...\n");

	restore_and_read_stage(state_session_fd, STATE_MEMFD_TOKEN, &stage);
	if (stage != 2)
		fail_exit("Expected stage 2, but state file contains %d", stage);

	ksft_print_msg("[STAGE 2] Retrieving session '%s'...\n", TEST_SESSION_NAME);
	session_fd = luo_retrieve_session(luo_fd, TEST_SESSION_NAME);
	if (session_fd < 0)
		fail_exit("luo_retrieve_session for '%s'", TEST_SESSION_NAME);

	ksft_print_msg("[STAGE 2] Restoring and verifying memfd (token %#x)...\n",
		       TEST_MEMFD_TOKEN);
	mfd = restore_and_verify_memfd(session_fd, TEST_MEMFD_TOKEN,
				       TEST_MEMFD_DATA);
	if (mfd < 0)
		fail_exit("restore_and_verify_memfd for token %#x", TEST_MEMFD_TOKEN);
	close(mfd);

	ksft_print_msg("[STAGE 2] Test data verified successfully.\n");
	ksft_print_msg("[STAGE 2] Finalizing test session...\n");
	if (luo_session_finish(session_fd) < 0)
		fail_exit("luo_session_finish for test session");
	close(session_fd);

	ksft_print_msg("[STAGE 2] Finalizing state session...\n");
	if (luo_session_finish(state_session_fd) < 0)
		fail_exit("luo_session_finish for state session");
	close(state_session_fd);

	ksft_print_msg("\n--- SIMPLE KEXEC TEST PASSED ---\n");
}

int main(int argc, char *argv[])
{
	return luo_test(argc, argv, STATE_SESSION_NAME,
			run_stage_1, run_stage_2);
}
