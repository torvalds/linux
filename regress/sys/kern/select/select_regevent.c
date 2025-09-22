/*	$OpenBSD: select_regevent.c,v 1.1 2021/11/29 16:11:46 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
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
 * Test that poll/select does not block if a pending event is found
 * during registering.
 */

#include <sys/select.h>
#include <assert.h>
#include <err.h>
#include <unistd.h>

int
main(void)
{
	fd_set rfd;
	fd_set wfd;
	int p1[2];
	int p2[2];
	int ret;

	/* Enforce test timeout. */
	alarm(10);

	if (pipe(p1) == -1)
		err(1, "pipe");
	if (pipe(p2) == -1)
		err(1, "pipe");

	close(p2[0]);

	/* fd without event */
	FD_ZERO(&rfd);
	FD_SET(p1[0], &rfd);

	/* fd with event */
	FD_ZERO(&wfd);
	FD_SET(p2[1], &wfd);

	assert(p1[0] < p2[1]);

	ret = select(p2[1] + 1, &rfd, &wfd, NULL, NULL);
	assert(ret == 1);
	assert(FD_ISSET(p1[0], &rfd) == 0);
	assert(FD_ISSET(p2[1], &wfd) != 0);

	return 0;
}
