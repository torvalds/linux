/*
 * Copyright © 2018 Alexey Dobriyan <adobriyan@gmail.com>
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
// Test /proc/*/fd lookup.

#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "proc.h"

/* lstat(2) has more "coverage" in case non-symlink pops up somehow. */
static void test_lookup_pass(const char *pathname)
{
	struct stat st;
	ssize_t rv;

	memset(&st, 0, sizeof(struct stat));
	rv = lstat(pathname, &st);
	assert(rv == 0);
	assert(S_ISLNK(st.st_mode));
}

static void test_lookup_fail(const char *pathname)
{
	struct stat st;
	ssize_t rv;

	rv = lstat(pathname, &st);
	assert(rv == -1 && errno == ENOENT);
}

static void test_lookup(unsigned int fd)
{
	char buf[64];
	unsigned int c;
	unsigned int u;
	int i;

	snprintf(buf, sizeof(buf), "/proc/self/fd/%u", fd);
	test_lookup_pass(buf);

	/* leading junk */
	for (c = 1; c <= 255; c++) {
		if (c == '/')
			continue;
		snprintf(buf, sizeof(buf), "/proc/self/fd/%c%u", c, fd);
		test_lookup_fail(buf);
	}

	/* trailing junk */
	for (c = 1; c <= 255; c++) {
		if (c == '/')
			continue;
		snprintf(buf, sizeof(buf), "/proc/self/fd/%u%c", fd, c);
		test_lookup_fail(buf);
	}

	for (i = INT_MIN; i < INT_MIN + 1024; i++) {
		snprintf(buf, sizeof(buf), "/proc/self/fd/%d", i);
		test_lookup_fail(buf);
	}
	for (i = -1024; i < 0; i++) {
		snprintf(buf, sizeof(buf), "/proc/self/fd/%d", i);
		test_lookup_fail(buf);
	}
	for (u = INT_MAX - 1024; u <= (unsigned int)INT_MAX + 1024; u++) {
		snprintf(buf, sizeof(buf), "/proc/self/fd/%u", u);
		test_lookup_fail(buf);
	}
	for (u = UINT_MAX - 1024; u != 0; u++) {
		snprintf(buf, sizeof(buf), "/proc/self/fd/%u", u);
		test_lookup_fail(buf);
	}


}

int main(void)
{
	struct dirent *de;
	unsigned int fd, target_fd;

	if (unshare(CLONE_FILES) == -1)
		return 1;

	/* Wipe fdtable. */
	do {
		DIR *d;

		d = opendir("/proc/self/fd");
		if (!d)
			return 1;

		de = xreaddir(d);
		assert(de->d_type == DT_DIR);
		assert(streq(de->d_name, "."));

		de = xreaddir(d);
		assert(de->d_type == DT_DIR);
		assert(streq(de->d_name, ".."));
next:
		de = xreaddir(d);
		if (de) {
			unsigned long long fd_ull;
			unsigned int fd;
			char *end;

			assert(de->d_type == DT_LNK);

			fd_ull = xstrtoull(de->d_name, &end);
			assert(*end == '\0');
			assert(fd_ull == (unsigned int)fd_ull);

			fd = fd_ull;
			if (fd == dirfd(d))
				goto next;
			close(fd);
		}

		closedir(d);
	} while (de);

	/* Now fdtable is clean. */

	fd = open("/", O_PATH|O_DIRECTORY);
	assert(fd == 0);
	test_lookup(fd);
	close(fd);

	/* Clean again! */

	fd = open("/", O_PATH|O_DIRECTORY);
	assert(fd == 0);
	/* Default RLIMIT_NOFILE-1 */
	target_fd = 1023;
	while (target_fd > 0) {
		if (dup2(fd, target_fd) == target_fd)
			break;
		target_fd /= 2;
	}
	assert(target_fd > 0);
	close(fd);
	test_lookup(target_fd);
	close(target_fd);

	return 0;
}
