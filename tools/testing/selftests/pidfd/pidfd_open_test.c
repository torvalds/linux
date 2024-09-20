// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "../kselftest.h"

static int safe_int(const char *numstr, int *converted)
{
	char *err = NULL;
	long sli;

	errno = 0;
	sli = strtol(numstr, &err, 0);
	if (errno == ERANGE && (sli == LONG_MAX || sli == LONG_MIN))
		return -ERANGE;

	if (errno != 0 && sli == 0)
		return -EINVAL;

	if (err == numstr || *err != '\0')
		return -EINVAL;

	if (sli > INT_MAX || sli < INT_MIN)
		return -ERANGE;

	*converted = (int)sli;
	return 0;
}

static int char_left_gc(const char *buffer, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buffer[i] == ' ' ||
		    buffer[i] == '\t')
			continue;

		return i;
	}

	return 0;
}

static int char_right_gc(const char *buffer, size_t len)
{
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (buffer[i] == ' '  ||
		    buffer[i] == '\t' ||
		    buffer[i] == '\n' ||
		    buffer[i] == '\0')
			continue;

		return i + 1;
	}

	return 0;
}

static char *trim_whitespace_in_place(char *buffer)
{
	buffer += char_left_gc(buffer, strlen(buffer));
	buffer[char_right_gc(buffer, strlen(buffer))] = '\0';
	return buffer;
}

static pid_t get_pid_from_fdinfo_file(int pidfd, const char *key, size_t keylen)
{
	int ret;
	char path[512];
	FILE *f;
	size_t n = 0;
	pid_t result = -1;
	char *line = NULL;

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", pidfd);

	f = fopen(path, "re");
	if (!f)
		return -1;

	while (getline(&line, &n, f) != -1) {
		char *numstr;

		if (strncmp(line, key, keylen))
			continue;

		numstr = trim_whitespace_in_place(line + 4);
		ret = safe_int(numstr, &result);
		if (ret < 0)
			goto out;

		break;
	}

out:
	free(line);
	fclose(f);
	return result;
}

int main(int argc, char **argv)
{
	int pidfd = -1, ret = 1;
	pid_t pid;

	ksft_set_plan(3);

	pidfd = sys_pidfd_open(-1, 0);
	if (pidfd >= 0) {
		ksft_print_msg(
			"%s - succeeded to open pidfd for invalid pid -1\n",
			strerror(errno));
		goto on_error;
	}
	ksft_test_result_pass("do not allow invalid pid test: passed\n");

	pidfd = sys_pidfd_open(getpid(), 1);
	if (pidfd >= 0) {
		ksft_print_msg(
			"%s - succeeded to open pidfd with invalid flag value specified\n",
			strerror(errno));
		goto on_error;
	}
	ksft_test_result_pass("do not allow invalid flag test: passed\n");

	pidfd = sys_pidfd_open(getpid(), 0);
	if (pidfd < 0) {
		ksft_print_msg("%s - failed to open pidfd\n", strerror(errno));
		goto on_error;
	}
	ksft_test_result_pass("open a new pidfd test: passed\n");

	pid = get_pid_from_fdinfo_file(pidfd, "Pid:", sizeof("Pid:") - 1);
	ksft_print_msg("pidfd %d refers to process with pid %d\n", pidfd, pid);

	ret = 0;

on_error:
	if (pidfd >= 0)
		close(pidfd);

	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
}
