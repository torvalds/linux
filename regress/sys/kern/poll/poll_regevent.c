/*	$OpenBSD: poll_regevent.c,v 1.1 2021/11/29 16:11:46 visa Exp $	*/

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

#include <assert.h>
#include <err.h>
#include <poll.h>
#include <unistd.h>

int
main(void)
{
	struct pollfd pfd[2];
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
	pfd[0].fd = p1[0];
	pfd[0].events = POLLIN;

	/* fd with event */
	pfd[1].fd = p2[1];
	pfd[1].events = POLLOUT;

	ret = poll(pfd, 2, INFTIM);
	assert(ret == 1);
	assert(pfd[0].revents == 0);
	assert(pfd[1].revents != 0);

	return 0;
}
