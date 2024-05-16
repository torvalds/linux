// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>

#include "../kselftest.h"

#define MIN_TTY_PATH_LEN 8

static bool tty_valid(char *tty)
{
	if (strlen(tty) < MIN_TTY_PATH_LEN)
		return false;

	if (strncmp(tty, "/dev/tty", MIN_TTY_PATH_LEN) == 0 ||
	    strncmp(tty, "/dev/pts", MIN_TTY_PATH_LEN) == 0)
		return true;

	return false;
}

static int write_dev_tty(void)
{
	FILE *f;
	int r = 0;

	f = fopen("/dev/tty", "r+");
	if (!f)
		return -errno;

	r = fprintf(f, "hello, world!\n");
	if (r != strlen("hello, world!\n"))
		r = -EIO;

	fclose(f);
	return r;
}

int main(int argc, char **argv)
{
	int r;
	char tty[PATH_MAX] = {};
	struct stat st1, st2;

	ksft_print_header();
	ksft_set_plan(1);

	r = readlink("/proc/self/fd/0", tty, PATH_MAX);
	if (r < 0)
		ksft_exit_fail_msg("readlink on /proc/self/fd/0 failed: %m\n");

	if (!tty_valid(tty))
		ksft_exit_skip("invalid tty path '%s'\n", tty);

	r = stat(tty, &st1);
	if (r < 0)
		ksft_exit_fail_msg("stat failed on tty path '%s': %m\n", tty);

	/* We need to wait at least 8 seconds in order to observe timestamp change */
	/* https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=fbf47635315ab308c9b58a1ea0906e711a9228de */
	sleep(10);

	r = write_dev_tty();
	if (r < 0)
		ksft_exit_fail_msg("failed to write to /dev/tty: %s\n",
				   strerror(-r));

	r = stat(tty, &st2);
	if (r < 0)
		ksft_exit_fail_msg("stat failed on tty path '%s': %m\n", tty);

	/* We wrote to the terminal so timestamps should have been updated */
	if (st1.st_atim.tv_sec == st2.st_atim.tv_sec &&
	    st1.st_mtim.tv_sec == st2.st_mtim.tv_sec) {
		ksft_test_result_fail("tty timestamps not updated\n");
		ksft_exit_fail();
	}

	ksft_test_result_pass(
		"timestamps of terminal '%s' updated after write to /dev/tty\n", tty);
	return EXIT_SUCCESS;
}
