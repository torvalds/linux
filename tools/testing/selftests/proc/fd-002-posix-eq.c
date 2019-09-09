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
// Test that open(/proc/*/fd/*) opens the same file.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	int fd0, fd1, fd2;
	struct stat st0, st1, st2;
	char buf[64];
	int rv;

	fd0 = open("/", O_DIRECTORY|O_RDONLY);
	assert(fd0 >= 0);

	snprintf(buf, sizeof(buf), "/proc/self/fd/%u", fd0);
	fd1 = open(buf, O_RDONLY);
	assert(fd1 >= 0);

	snprintf(buf, sizeof(buf), "/proc/thread-self/fd/%u", fd0);
	fd2 = open(buf, O_RDONLY);
	assert(fd2 >= 0);

	rv = fstat(fd0, &st0);
	assert(rv == 0);
	rv = fstat(fd1, &st1);
	assert(rv == 0);
	rv = fstat(fd2, &st2);
	assert(rv == 0);

	assert(st0.st_dev == st1.st_dev);
	assert(st0.st_ino == st1.st_ino);

	assert(st0.st_dev == st2.st_dev);
	assert(st0.st_ino == st2.st_ino);

	return 0;
}
