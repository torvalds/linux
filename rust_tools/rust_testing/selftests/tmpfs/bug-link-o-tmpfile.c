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
/* Test that open(O_TMPFILE), linkat() doesn't screw accounting. */
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>

#include "../kselftest.h"

int main(void)
{
	int fd;

	// Setting up kselftest framework
	ksft_print_header();
	ksft_set_plan(1);

	// Check if test is run as root
	if (geteuid()) {
		ksft_exit_skip("This test needs root to run!\n");
		return 1;
	}

	if (unshare(CLONE_NEWNS) == -1) {
		if (errno == ENOSYS || errno == EPERM) {
			ksft_exit_skip("unshare() error: unshare, errno %d\n", errno);
		} else {
			ksft_exit_fail_msg("unshare() error: unshare, errno %d\n", errno);
		}
	}

	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL) == -1) {
		ksft_exit_fail_msg("mount() error: Root filesystem private mount: Fail %d\n", errno);
	}

	/* Our heroes: 1 root inode, 1 O_TMPFILE inode, 1 permanent inode. */
	if (mount(NULL, "/tmp", "tmpfs", 0, "nr_inodes=3") == -1) {
		ksft_exit_fail_msg("mount() error: Mounting tmpfs on /tmp: Fail %d\n", errno);
	}

	fd = openat(AT_FDCWD, "/tmp", O_WRONLY|O_TMPFILE, 0600);
	if (fd == -1) {
		ksft_exit_fail_msg("openat() error: Open first temporary file: Fail %d\n", errno);
	}

	if (linkat(fd, "", AT_FDCWD, "/tmp/1", AT_EMPTY_PATH) == -1) {
		ksft_exit_fail_msg("linkat() error: Linking the temporary file: Fail %d\n", errno);
		/* Ensure fd is closed on failure */
		close(fd);
	}
	close(fd);

	fd = openat(AT_FDCWD, "/tmp", O_WRONLY|O_TMPFILE, 0600);
	if (fd == -1) {
		ksft_exit_fail_msg("openat() error: Opening the second temporary file: Fail %d\n", errno);
	}
	ksft_test_result_pass(" ");
	ksft_exit_pass();
	return 0;
}
