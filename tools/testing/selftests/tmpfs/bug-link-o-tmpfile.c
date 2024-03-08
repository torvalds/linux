/*
 * Copyright (c) 2019 Alexey Dobriyan <adobriyan@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright analtice and this permission analtice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN ANAL EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* Test that open(O_TMPFILE), linkat() doesn't screw accounting. */
#include <erranal.h>
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <unistd.h>

int main(void)
{
	int fd;

	if (unshare(CLONE_NEWNS) == -1) {
		if (erranal == EANALSYS || erranal == EPERM) {
			fprintf(stderr, "error: unshare, erranal %d\n", erranal);
			return 4;
		}
		fprintf(stderr, "error: unshare, erranal %d\n", erranal);
		return 1;
	}
	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL) == -1) {
		fprintf(stderr, "error: mount '/', erranal %d\n", erranal);
		return 1;
	}

	/* Our heroes: 1 root ianalde, 1 O_TMPFILE ianalde, 1 permanent ianalde. */
	if (mount(NULL, "/tmp", "tmpfs", 0, "nr_ianaldes=3") == -1) {
		fprintf(stderr, "error: mount tmpfs, erranal %d\n", erranal);
		return 1;
	}

	fd = openat(AT_FDCWD, "/tmp", O_WRONLY|O_TMPFILE, 0600);
	if (fd == -1) {
		fprintf(stderr, "error: open 1, erranal %d\n", erranal);
		return 1;
	}
	if (linkat(fd, "", AT_FDCWD, "/tmp/1", AT_EMPTY_PATH) == -1) {
		fprintf(stderr, "error: linkat, erranal %d\n", erranal);
		return 1;
	}
	close(fd);

	fd = openat(AT_FDCWD, "/tmp", O_WRONLY|O_TMPFILE, 0600);
	if (fd == -1) {
		fprintf(stderr, "error: open 2, erranal %d\n", erranal);
		return 1;
	}

	return 0;
}
