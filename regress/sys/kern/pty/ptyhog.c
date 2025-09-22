/*
 * Copyright (c) 2013 Todd C. Miller <millert@openbsd.org>
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
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#define BUF_SIZE 1024

/*
 * Exercise a bug in ptcwrite() when we hit the TTYHOG limit if
 * the tty is in raw mode.
 */
static void sigalrm(int signo)
{
	/* just return */
	return;
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
        unsigned char buf[BUF_SIZE];
        int mfd, sfd, status;
        struct termios term;
        size_t i, nwritten = 0, nread= 0;
        ssize_t n;

	/*
	 * Open pty and set slave to raw mode.
	 */
        if (openpty(&mfd, &sfd, NULL, NULL, NULL) == -1)
		err(1, "openpty");
        if (tcgetattr(sfd, &term) == -1)
                err(1, "tcgetattr");
        cfmakeraw(&term);
        if (tcsetattr(sfd, TCSAFLUSH, &term) == -1)
                err(1, "tcsetattr");

	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		/* prevent a hang if the bug is present */
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sigalrm;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGALRM, &sa, NULL);
		alarm(5);

		/* child, read data from slave */
		do {
			n = read(sfd, buf + nread, sizeof(buf) - nread);
			if (n == -1) {
				if (errno == EINTR)
					errx(1, "timed out @ %zd", nread);
				err(1, "read @ %zd", nread);
			}
			nread += n;
		} while (nread != sizeof(buf));
		for (i = 0; i < sizeof(buf); i++) {
			if (buf[i] != (i & 0xff)) {
				errx(1, "buffer corrupted at %zd "
				    "(got %u, expected %zd)", i,
				    buf[i], (i & 0xff));
			}
		}
		printf("all data received\n");
		exit(0);
	default:
		/* parent, write data to master */
		for (i = 0; i < sizeof(buf); i++)
			buf[i] = (i & 0xff);
		do {
			n = write(mfd, buf + nwritten, sizeof(buf) - nwritten);
			if (n == -1)
				err(1, "write @ %zd", nwritten);
			nwritten += n;
		} while (nwritten != sizeof(buf));
		wait(&status);
		exit(WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status) + 128);
	}
}
