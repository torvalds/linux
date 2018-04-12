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
// Test
// 1) read of every file in /proc
// 2) readlink of every symlink in /proc
// 3) recursively (1) + (2) for every directory in /proc
// 4) write to /proc/*/clear_refs and /proc/*/task/*/clear_refs
// 5) write to /proc/sysrq-trigger
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static inline bool streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

static struct dirent *xreaddir(DIR *d)
{
	struct dirent *de;

	errno = 0;
	de = readdir(d);
	if (!de && errno != 0) {
		exit(1);
	}
	return de;
}

static void f_reg(DIR *d, const char *filename)
{
	char buf[4096];
	int fd;
	ssize_t rv;

	/* read from /proc/kmsg can block */
	fd = openat(dirfd(d), filename, O_RDONLY|O_NONBLOCK);
	if (fd == -1)
		return;
	rv = read(fd, buf, sizeof(buf));
	assert((0 <= rv && rv <= sizeof(buf)) || rv == -1);
	close(fd);
}

static void f_reg_write(DIR *d, const char *filename, const char *buf, size_t len)
{
	int fd;
	ssize_t rv;

	fd = openat(dirfd(d), filename, O_WRONLY);
	if (fd == -1)
		return;
	rv = write(fd, buf, len);
	assert((0 <= rv && rv <= len) || rv == -1);
	close(fd);
}

static void f_lnk(DIR *d, const char *filename)
{
	char buf[4096];
	ssize_t rv;

	rv = readlinkat(dirfd(d), filename, buf, sizeof(buf));
	assert((0 <= rv && rv <= sizeof(buf)) || rv == -1);
}

static void f(DIR *d, unsigned int level)
{
	struct dirent *de;

	de = xreaddir(d);
	assert(de->d_type == DT_DIR);
	assert(streq(de->d_name, "."));

	de = xreaddir(d);
	assert(de->d_type == DT_DIR);
	assert(streq(de->d_name, ".."));

	while ((de = xreaddir(d))) {
		assert(!streq(de->d_name, "."));
		assert(!streq(de->d_name, ".."));

		switch (de->d_type) {
			DIR *dd;
			int fd;

		case DT_REG:
			if (level == 0 && streq(de->d_name, "sysrq-trigger")) {
				f_reg_write(d, de->d_name, "h", 1);
			} else if (level == 1 && streq(de->d_name, "clear_refs")) {
				f_reg_write(d, de->d_name, "1", 1);
			} else if (level == 3 && streq(de->d_name, "clear_refs")) {
				f_reg_write(d, de->d_name, "1", 1);
			} else {
				f_reg(d, de->d_name);
			}
			break;
		case DT_DIR:
			fd = openat(dirfd(d), de->d_name, O_DIRECTORY|O_RDONLY);
			if (fd == -1)
				continue;
			dd = fdopendir(fd);
			if (!dd)
				continue;
			f(dd, level + 1);
			closedir(dd);
			break;
		case DT_LNK:
			f_lnk(d, de->d_name);
			break;
		default:
			assert(0);
		}
	}
}

int main(void)
{
	DIR *d;

	d = opendir("/proc");
	if (!d)
		return 2;
	f(d, 0);
	return 0;
}
