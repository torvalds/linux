/*
 * Copyright (c) 2021 Alexey Dobriyan <adobriyan@gmail.com>
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
/*
 * Test that "mount -t proc -o subset=pid" hides everything but pids,
 * /proc/self and /proc/thread-self.
 */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>

static inline bool streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

static void make_private_proc(void)
{
	if (unshare(CLONE_NEWNS) == -1) {
		if (errno == ENOSYS || errno == EPERM) {
			exit(4);
		}
		exit(1);
	}
	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL) == -1) {
		exit(1);
	}
	if (mount(NULL, "/proc", "proc", 0, "subset=pid") == -1) {
		exit(1);
	}
}

static bool string_is_pid(const char *s)
{
	while (1) {
		switch (*s++) {
		case '0':case '1':case '2':case '3':case '4':
		case '5':case '6':case '7':case '8':case '9':
			continue;

		case '\0':
			return true;

		default:
			return false;
		}
	}
}

int main(void)
{
	make_private_proc();

	DIR *d = opendir("/proc");
	assert(d);

	struct dirent *de;

	bool dot = false;
	bool dot_dot = false;
	bool self = false;
	bool thread_self = false;

	while ((de = readdir(d))) {
		if (streq(de->d_name, ".")) {
			assert(!dot);
			dot = true;
			assert(de->d_type == DT_DIR);
		} else if (streq(de->d_name, "..")) {
			assert(!dot_dot);
			dot_dot = true;
			assert(de->d_type == DT_DIR);
		} else if (streq(de->d_name, "self")) {
			assert(!self);
			self = true;
			assert(de->d_type == DT_LNK);
		} else if (streq(de->d_name, "thread-self")) {
			assert(!thread_self);
			thread_self = true;
			assert(de->d_type == DT_LNK);
		} else {
			if (!string_is_pid(de->d_name)) {
				fprintf(stderr, "d_name '%s'\n", de->d_name);
				assert(0);
			}
			assert(de->d_type == DT_DIR);
		}
	}

	char c;
	int rv = readlink("/proc/cpuinfo", &c, 1);
	assert(rv == -1 && errno == ENOENT);

	int fd = open("/proc/cpuinfo", O_RDONLY);
	assert(fd == -1 && errno == ENOENT);

	return 0;
}
