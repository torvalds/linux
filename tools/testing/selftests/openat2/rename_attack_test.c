// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syscall.h>
#include <limits.h>
#include <unistd.h>

#include "../kselftest.h"
#include "helpers.h"

/* Construct a test directory with the following structure:
 *
 * root/
 * |-- a/
 * |   `-- c/
 * `-- b/
 */
int setup_testdir(void)
{
	int dfd;
	char dirname[] = "/tmp/ksft-openat2-rename-attack.XXXXXX";

	/* Make the top-level directory. */
	if (!mkdtemp(dirname))
		ksft_exit_fail_msg("setup_testdir: failed to create tmpdir\n");
	dfd = open(dirname, O_PATH | O_DIRECTORY);
	if (dfd < 0)
		ksft_exit_fail_msg("setup_testdir: failed to open tmpdir\n");

	E_mkdirat(dfd, "a", 0755);
	E_mkdirat(dfd, "b", 0755);
	E_mkdirat(dfd, "a/c", 0755);

	return dfd;
}

/* Swap @dirfd/@a and @dirfd/@b constantly. Parent must kill this process. */
pid_t spawn_attack(int dirfd, char *a, char *b)
{
	pid_t child = fork();
	if (child != 0)
		return child;

	/* If the parent (the test process) dies, kill ourselves too. */
	E_prctl(PR_SET_PDEATHSIG, SIGKILL);

	/* Swap @a and @b. */
	for (;;)
		renameat2(dirfd, a, dirfd, b, RENAME_EXCHANGE);
	exit(1);
}

#define NUM_RENAME_TESTS 2
#define ROUNDS 400000

const char *flagname(int resolve)
{
	switch (resolve) {
	case RESOLVE_IN_ROOT:
		return "RESOLVE_IN_ROOT";
	case RESOLVE_BENEATH:
		return "RESOLVE_BENEATH";
	}
	return "(unknown)";
}

void test_rename_attack(int resolve)
{
	int dfd, afd;
	pid_t child;
	void (*resultfn)(const char *msg, ...) = ksft_test_result_pass;
	int escapes = 0, other_errs = 0, exdevs = 0, eagains = 0, successes = 0;

	struct open_how how = {
		.flags = O_PATH,
		.resolve = resolve,
	};

	if (!openat2_supported) {
		how.resolve = 0;
		ksft_print_msg("openat2(2) unsupported -- using openat(2) instead\n");
	}

	dfd = setup_testdir();
	afd = openat(dfd, "a", O_PATH);
	if (afd < 0)
		ksft_exit_fail_msg("test_rename_attack: failed to open 'a'\n");

	child = spawn_attack(dfd, "a/c", "b");

	for (int i = 0; i < ROUNDS; i++) {
		int fd;
		char *victim_path = "c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../../c/../..";

		if (openat2_supported)
			fd = sys_openat2(afd, victim_path, &how);
		else
			fd = sys_openat(afd, victim_path, &how);

		if (fd < 0) {
			if (fd == -EAGAIN)
				eagains++;
			else if (fd == -EXDEV)
				exdevs++;
			else if (fd == -ENOENT)
				escapes++; /* escaped outside and got ENOENT... */
			else
				other_errs++; /* unexpected error */
		} else {
			if (fdequal(fd, afd, NULL))
				successes++;
			else
				escapes++; /* we got an unexpected fd */
		}
		close(fd);
	}

	if (escapes > 0)
		resultfn = ksft_test_result_fail;
	ksft_print_msg("non-escapes: EAGAIN=%d EXDEV=%d E<other>=%d success=%d\n",
		       eagains, exdevs, other_errs, successes);
	resultfn("rename attack with %s (%d runs, got %d escapes)\n",
		 flagname(resolve), ROUNDS, escapes);

	/* Should be killed anyway, but might as well make sure. */
	E_kill(child, SIGKILL);
}

#define NUM_TESTS NUM_RENAME_TESTS

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(NUM_TESTS);

	test_rename_attack(RESOLVE_BENEATH);
	test_rename_attack(RESOLVE_IN_ROOT);

	if (ksft_get_fail_cnt() + ksft_get_error_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
