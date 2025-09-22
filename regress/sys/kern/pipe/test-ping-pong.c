/*	$OpenBSD: test-ping-pong.c,v 1.2 2021/10/22 05:03:57 anton Exp $	*/

/*
 * Copyright (c) 2019 Anton Lindqvist <anton@openbsd.org>
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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pipe.h"

/*
 * Basic read/write test invovling two processes, P1 and P2. P1 writes "ping" on
 * the pipe which is received and verified by P2. P2 writes "pong" as a reply
 * which is received and verified P1. After N rounds, P1 closes the pipe and
 * SIGPIPE is expected to be delivered to P2.
 */
int
test_ping_pong(void)
{
	const char ping[] = "ping";
	const char pong[] = "pong";
	char buf[5];
	int pip[2][2], rp, wp;
	int nrounds = 10;
	ssize_t n;
	pid_t pid;

	if (pipe(pip[0]) == -1)
		err(1, "pipe");
	if (pipe(pip[1]) == -1)
		err(1, "pipe");

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		rp = pip[0][0];
		close(pip[0][1]);

		wp = pip[1][1];
		close(pip[1][0]);

		for (;;) {
			n = read(rp, buf, sizeof(buf));
			if (n == -1)
				err(1, "[c] read");
			if (n == 0)
				break;
			if (n != sizeof(buf))
				errx(1, "[c] read: %ld < %zu", n, sizeof(buf));
			if (strcmp(ping, buf))
				errx(1, "[c] read: %s != %s\n", ping, buf);

			n = write(wp, pong, sizeof(pong));
			if (n == -1)
				err(1, "[c] write");
			if (n != sizeof(pong))
				errx(1, "[c] write: %ld < %zu",
				    n, sizeof(pong));

			nrounds--;
		}
		if (nrounds != 0)
			errx(1, "[c] nrounds: %d > 0", nrounds);

		/*
		 * Writing on the pipe must cause delivery of SIGPIPE at this
		 * point since the read end is gone.
		 */
		n = write(wp, pong, sizeof(pong));
		if (n != -1)
			errx(1, "[c] write: %ld != -1", n);
		else if (errno != EPIPE)
			errx(1, "[c] write: %d != %d", errno, EPIPE);
		else if (!gotsigpipe)
			errx(1, "[c] write: no SIGPIPE");

		_exit(0);
	} else {
		rp = pip[1][0];
		close(pip[1][1]);

		wp = pip[0][1];
		close(pip[0][0]);

		for (;;) {
			n = write(wp, ping, sizeof(ping));
			if (n == -1)
				err(1, "[p] write");
			if (n != sizeof(ping))
				errx(1, "[p] write: %ld < %zu",
				    n, sizeof(ping));

			n = read(rp, buf, sizeof(buf));
			if (n == -1)
				err(1, "[p] read");
			if (n != sizeof(buf))
				errx(1, "[p] read: %ld < %zu", n, sizeof(buf));
			if (strcmp(pong, buf))
				errx(1, "[p] read: %s != %s\n", pong, buf);

			if (--nrounds == 0)
				break;
		}

		/* Signal shutdown to the child. */
		close(rp);
		close(wp);

		return xwaitpid(pid);
	}

	return 0;
}
