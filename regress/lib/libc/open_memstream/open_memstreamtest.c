/*	$OpenBSD: open_memstreamtest.c,v 1.6 2019/05/13 02:54:54 bluhm Exp $ */

/*
 * Copyright (c) 2011 Martin Pieuchot <mpi@openbsd.org>
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OFFSET 16384

const char start[] = "start";
const char hello[] = "hello";

int
main(void)
{
	FILE	*fp;
	char	*buf = (char *)0xff;
	size_t	 size = 0;
	off_t	 off;
	int	 i, failures = 0;

	if ((fp = open_memstream(&buf, &size)) == NULL) {
		warn("open_memstream failed");
		return (1);
	}

	off = ftello(fp);
	if (off != 0) {
		warnx("ftello failed. (1)");
		failures++;
	}

	if (fflush(fp) != 0) {
		warnx("fflush failed. (2)");
		failures++;
	}

	if (size != 0) {
		warnx("string should be empty. (3)");
		failures++;
	}

	if (buf == (char *)0xff) {
		warnx("buf not updated. (4)");
		failures++;
	}

	if (fseek(fp, OFFSET, SEEK_SET) != 0) {
		warnx("failed to fseek. (5)");
		failures++;
	}

	if (fprintf(fp, hello) == EOF) {
		warnx("fprintf failed. (6)");
		failures++;
	}

	if (fflush(fp) == EOF) {
		warnx("fflush failed. (7)");
		failures++;
	}

	if (size != OFFSET + sizeof(hello)-1) {
		warnx("failed, size %zu should be %zu. (8)",
		    size, OFFSET + sizeof(hello)-1);
		failures++;
	}

	if (fseek(fp, 0, SEEK_SET) != 0) {
		warnx("failed to fseek. (9)");
		failures++;
	}

	if (fprintf(fp, start) == EOF) {
		warnx("fprintf failed. (10)");
		failures++;
	}

	if (fflush(fp) == EOF) {
		warnx("fflush failed. (11)");
		failures++;
	}

	if (size != sizeof(start)-1) {
		warnx("failed, size %zu should be %zu. (12)",
		    size, sizeof(start)-1);
		failures++;
	}

	/* Needed for sparse files */
	if (strncmp(buf, start, sizeof(start)-1) != 0) {
		warnx("failed, buffer didn't start with '%s'. (13)", start);
		failures++;
	}
	for (i = sizeof(start)-1; i < OFFSET; i++)
		if (buf[i] != '\0') {
			warnx("failed, buffer non zero (offset %d). (14)", i);
			failures++;
			break;
		}

	if (memcmp(buf + OFFSET, hello, sizeof(hello)-1) != 0) {
		warnx("written string incorrect. (15)");
		failures++;
	}

	/* verify that simply seeking past the end doesn't increase the size */
	if (fseek(fp, 100, SEEK_END) != 0) {
		warnx("failed to fseek. (16)");
		failures++;
	}

	if (fflush(fp) == EOF) {
		warnx("fflush failed. (17)");
		failures++;
	}

	if (size != OFFSET + sizeof(hello)-1) {
		warnx("failed, size %zu should be %zu. (18)",
		    size, OFFSET + sizeof(hello)-1);
		failures++;
	}

	if (fseek(fp, -1, SEEK_END) != 0) {
		warnx("failed to fseek. (19)");
		failures++;
	}

	if (fseek(fp, 8, SEEK_SET) != 0) {
		warnx("failed to fseek. (20)");
		failures++;
	}

	if (ftell(fp) != 8) {
		warnx("failed seek test. (21)");
		failures++;
	}

	/* Try to seek backward */
	if (fseek(fp, -1, SEEK_CUR) != 0) {
		warnx("failed to fseek. (22)");
		failures++;
	}

	if (ftell(fp) != 7) {
		warnx("failed seeking backward. (23)");
		failures++;
	}

	if (fseek(fp, 5, SEEK_CUR) != 0) {
		warnx("failed to fseek. (24)");
		failures++;
	}

	if (fclose(fp) == EOF) {
		warnx("fclose failed. (25)");
		failures++;
	}

	if (size != 12) {
		warnx("failed, size %zu should be %u.  (26)",
		    size, 12);
		failures++;
	}

	free(buf);

	return (failures);
}
