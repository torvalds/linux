/*	$OpenBSD: unp-write-closed.c,v 1.2 2024/07/14 18:49:32 anton Exp $	*/
/*
 * Copyright (c) 2024 Vitaliy Makkoveev <mvs@openbsd.org>
 * Copyright (c) 2024 Alenander Bluhm <bluhm@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

sig_atomic_t done = 0;

static void
handler(int sigraised)
{
	done = 1;
}

int
main(int argc, char *argv[])
{
	int i, s[2], status;
	pid_t pid;

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		err(1, "signal pipe");
	if (signal(SIGALRM, handler) == SIG_ERR)
		err(1, "signal alrm");
	alarm(30);

	while (!done) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0)
			err(1, "socketpair");

		switch ((pid = fork())) {
		case -1:
			err(1, "fork");
		case 0:
			if (close(s[0]) < 0)
				err(1, "child close 0");
			if (close(s[1]) < 0)
				err(1, "child close 1");
			return 0;
		default:
			if (close(s[1]) < 0)
				err(1, "parent close 1");
			for (i = 1000000; i > 0; i--) {
				if (write(s[0], "1", 1) < 0)
					break;
			}
			if (i <= 0)
				errx(1, "write did not fail");
			if (errno != EPIPE)
				err(1, "write");
			if (close(s[0]) < 0)
				err(1, "parent close 1");
			if (waitpid(pid, &status, 0) < 0)
				err(1, "waitpid");
			if (status != 0)
				errx(1, "child status %d", status);
			break;
		}
	}

	return 0;
}
