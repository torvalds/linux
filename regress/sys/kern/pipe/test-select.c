/*	$OpenBSD: test-select.c,v 1.1 2021/10/22 05:03:04 anton Exp $	*/

/*
 * Copyright (c) 2021 Anton Lindqvist <anton@openbsd.org>
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

#include <sys/select.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

#include "pipe.h"

/*
 * Verify select(2) hangup semantics.
 */
int
test_select_hup(void)
{
	fd_set writefds;
	ssize_t n;
	int pip[2];
	int nready;
	char c = 'c';

	if (pipe(pip) == -1)
		err(1, "pipe");
	close(pip[0]);

	FD_ZERO(&writefds);
	FD_SET(pip[1], &writefds);
	nready = select(pip[1] + 1, NULL, &writefds, NULL, NULL);
	if (nready == -1)
		err(1, "select");
	if (nready != 1)
		errx(1, "select: want 1, got %d", nready);
	n = write(pip[1], &c, sizeof(c));
	if (n != -1)
		errx(1, "read: want -1, got %zd", n);
	if (errno != EPIPE)
		errx(1, "errno: want %d, got %d", EPIPE, errno);

	return 0;
}
