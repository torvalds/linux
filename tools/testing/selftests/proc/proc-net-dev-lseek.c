/*
 * Copyright (c) 2025 Alexey Dobriyan <adobriyan@gmail.com>
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
#undef _GNU_SOURCE
#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
/*
 * Test that lseek("/proc/net/dev/", 0, SEEK_SET)
 * a) works,
 * b) does what you think it does.
 */
int main(void)
{
	/* /proc/net/dev output is deterministic in fresh netns only. */
	if (unshare(CLONE_NEWNET) == -1) {
		if (errno == ENOSYS || errno == EPERM) {
			return 4;
		}
		return 1;
	}

	const int fd = open("/proc/net/dev", O_RDONLY);
	assert(fd >= 0);

	char buf1[4096];
	const ssize_t rv1 = read(fd, buf1, sizeof(buf1));
	/*
	 * Not "<=", this file can't be empty:
	 * there is header, "lo" interface with some zeroes.
	 */
	assert(0 < rv1);
	assert(rv1 <= sizeof(buf1));

	/* Believe it or not, this line broke one day. */
	assert(lseek(fd, 0, SEEK_SET) == 0);

	char buf2[4096];
	const ssize_t rv2 = read(fd, buf2, sizeof(buf2));
	/* Not "<=", see above. */
	assert(0 < rv2);
	assert(rv2 <= sizeof(buf2));

	/* Test that lseek rewinds to the beginning of the file. */
	assert(rv1 == rv2);
	assert(memcmp(buf1, buf2, rv1) == 0);

	/* Contents of the file is not validated: this test is about lseek(). */

	return 0;
}
