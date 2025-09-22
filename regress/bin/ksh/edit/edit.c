/* $OpenBSD: edit.c,v 1.8 2021/07/10 07:10:31 anton Exp $ */
/*
 * Copyright (c) 2017 Anton Lindqvist <anton@openbsd.org>
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

#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#define	WRTIM	50	/* input write timeout */
#define	PRTIM	5000	/* prompt read timeout */

static size_t		findprompt(const char *, const char *);
static void		sighandler(int);
static void __dead	usage(void);

static volatile sig_atomic_t	gotsig;

int
main(int argc, char *argv[])
{
	const char	*linefeed = "\003\004\n\r";
	const char	*prompt = "";
	char		 in[BUFSIZ], out[BUFSIZ];
	struct pollfd	 pfd;
	struct winsize	 ws;
	pid_t		 pid;
	ssize_t		 n;
	size_t		 nin, nprompt, nread, nwrite;
	int		 c, nready, ptyfd, readprompt, ret, status, timeout;

	while ((c = getopt(argc, argv, "p:")) != -1) {
		switch (c) {
		case 'p':
			prompt = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 || strlen(prompt) == 0)
		usage();

	nin = 0;
	for (;;) {
		if (nin == sizeof(in))
			errx(1, "input buffer too small");

		n = read(0, in + nin, sizeof(in) - nin);
		if (n == -1)
			err(1, "read");
		if (n == 0)
			break;

		nin += n;
	}

	if (signal(SIGCHLD, sighandler) == SIG_ERR)
		err(1, "signal: SIGCHLD");
	if (signal(SIGINT, sighandler) == SIG_ERR)
		err(1, "signal: SIGINT");

	memset(&ws, 0, sizeof(ws));
	ws.ws_col = 80;
	ws.ws_row = 24;

	pid = forkpty(&ptyfd, NULL, NULL, &ws);
	if (pid == -1)
		err(1, "forkpty");
	if (pid == 0) {
		execvp(*argv, argv);
		err(1, "%s", *argv);
	}

	nprompt = nread = nwrite = ret = 0;
	readprompt = 1;
	while (!gotsig) {
		pfd.fd = ptyfd;
		if (!readprompt && nwrite < nin)
			pfd.events = POLLOUT;
		else
			pfd.events = POLLIN;
		timeout = readprompt ? PRTIM : WRTIM;
		nready = poll(&pfd, 1, timeout);
		if (nready == -1) {
			if (errno == EINTR)
				continue;
			err(1, "poll");
		}
		if (nready == 0) {
			if (timeout == PRTIM) {
				warnx("timeout waiting from prompt");
				ret = 1;
			}
			break;
		}
		if (pfd.revents & (POLLERR | POLLNVAL))
			errc(1, EBADF, NULL);

		if (pfd.revents & (POLLIN | POLLHUP)) {
			if (nread == sizeof(out))
				errx(1, "output buffer too small");

			n = read(ptyfd, out + nread, sizeof(out) - 1 - nread);
			if (n == -1)
				err(1, "read");
			nread += n;
			out[nread] = '\0';

			if (readprompt &&
			    (n = findprompt(out + nprompt, prompt)) > 0) {
				nprompt += n;
				readprompt = 0;
			}
		} else if (pfd.revents & POLLOUT) {
			if (strchr(linefeed, in[nwrite]) != NULL)
				readprompt = 1;

			n = write(ptyfd, in + nwrite, 1);
			if (n == -1)
				err(1, "write");
			nwrite += n;
		}
	}
	close(ptyfd);
	while (waitpid(pid, &status, 0) == -1)
		if (errno != EINTR)
			err(1, "waitpid");
	if (WIFSIGNALED(status) && WTERMSIG(status) != SIGHUP) {
		warnx("%s: terminated by signal %d", *argv, WTERMSIG(status));
		ret = 128 + WTERMSIG(status);
	}

	printf("%.*s", (int)nread, out);

	return ret;
}

static size_t
findprompt(const char *str, const char *prompt)
{
	char	*cp;
	size_t	 len;

	if ((cp = strstr(str, prompt)) == NULL)
		return 0;
	len = strlen(prompt);

	return (cp - str) + len;
}

static void
sighandler(int sig)
{
	gotsig = sig;
}

static void __dead
usage(void)
{
	fprintf(stderr, "usage: edit -p prompt command [args]\n");
	exit(1);
}
