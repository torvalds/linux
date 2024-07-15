/*
 * Copyright (c) 2019 Alexey Dobriyan <adobriyan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* Test that pointing #! script interpreter to self doesn't recurse. */
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>
#include "../kselftest.h"

int main(void)
{
	int fd, rv;

	ksft_print_header();
	ksft_set_plan(1);

	if (unshare(CLONE_NEWNS) == -1) {
		if (errno == ENOSYS || errno == EPERM) {
			ksft_test_result_skip("error: unshare, errno %d\n", errno);
			ksft_finished();
		}
		ksft_exit_fail_msg("error: unshare, errno %d\n", errno);
	}

	if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) == -1)
		ksft_exit_fail_msg("error: mount '/', errno %d\n", errno);

	/* Require "exec" filesystem. */
	if (mount(NULL, "/tmp", "ramfs", 0, NULL) == -1)
		ksft_exit_fail_msg("error: mount ramfs, errno %d\n", errno);

#define FILENAME "/tmp/1"

	fd = creat(FILENAME, 0700);
	if (fd == -1)
		ksft_exit_fail_msg("error: creat, errno %d\n", errno);

#define S "#!" FILENAME "\n"
	if (write(fd, S, strlen(S)) != strlen(S))
		ksft_exit_fail_msg("error: write, errno %d\n", errno);

	close(fd);

	rv = execve(FILENAME, NULL, NULL);
	ksft_test_result(rv == -1 && errno == ELOOP,
			 "execve failed as expected (ret %d, errno %d)\n", rv, errno);
	ksft_finished();
}
