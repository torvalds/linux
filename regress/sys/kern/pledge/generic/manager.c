/*	$OpenBSD: manager.c,v 1.9 2024/06/03 08:02:22 anton Exp $ */
/*
 * Copyright (c) 2015 Sebastien Marie <semarie@openbsd.org>
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

#include <sys/syslimits.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "manager.h"
#include "pty.h"

extern char *__progname;

static const char *
coredump_name()
{
	static char coredump[PATH_MAX] = "";

	if (*coredump)
		return (coredump);

	if (strlcpy(coredump, __progname, sizeof(coredump)) >= sizeof(coredump))
		errx(1, "coredump: strlcpy");

	if (strlcat(coredump, ".core", sizeof(coredump)) >= sizeof(coredump))
		errx(1, "coredump: strlcat");

	return (coredump);
}


static int
check_coredump()
{
	const char *coredump = coredump_name();
	int fd;

	if ((fd = open(coredump, O_RDONLY)) == -1) {
		if (errno == ENOENT)
			return (1); /* coredump not found */
		else
			return (-1); /* error */
	}

	(void)close(fd);
	return (0); /* coredump found */
}


static int
clear_coredump(int *ret, const char *test_name)
{
	int saved_errno = errno;
	int u;

	if (((u = unlink(coredump_name())) != 0) && (errno != ENOENT)) {
		warn("test(%s): clear_coredump", test_name);
		*ret = EXIT_FAILURE;
		return (-1);
	}
	errno = saved_errno;

	return (0);
}


static int
grab_syscall(pid_t pid, char *output)
{
	int		 ret = -1;
	char		*pattern;
	regex_t		 regex;
	regmatch_t	 matches[2];
	int		 error;
	const char	*errstr;

	/* build searched string */
	error = asprintf(&pattern,
	    "%s\\[%d\\]: pledge \"[a-z]+\", syscall ([0-9]+)",
	    __progname, pid);
	if (error <= 0) {
		warn("asprintf pattern");
		return (-1);
	}
	error = regcomp(&regex, pattern, REG_EXTENDED);
	if (error) {
		warnx("regcomp pattern=%s error=%d", pattern, error);
		free(pattern);
		return (-1);
	}
	if (regex.re_nsub != 1) {
		warnx("regcomp pattern=%s nsub=%zu", pattern, regex.re_nsub);
		goto out;
	}

	/* search the string */
	error = regexec(&regex, output, 2, matches, 0);
	if (error == REG_NOMATCH) {
		ret = 0;
		goto out;
	}
	if (error) {
		warnx("regexec pattern=%s output=%s error=%d",
		    pattern, output, error);
		ret = -1;
		goto out;
	}

	/* convert it */
	output[matches[1].rm_eo] = '\0';
	ret = strtonum(&output[matches[1].rm_so], 0, 255, &errstr);
	if (errstr) {
		warnx("strtonum: number=%s error=%s",
		    &output[matches[1].rm_so], errstr);
		ret = -1;
		goto out;
	}

out:
	free(pattern);
	regfree(&regex);
	return (ret);
}

/* mainly stolen from src/bin/cat/cat.c */
static int
drainfd(int rfd, int wfd)
{
	char buf[1024];
	ssize_t nr, nw, off;

	while ((nr = read(rfd, buf, sizeof(buf))) != -1 && nr != 0)
		for (off = 0; nr; nr -= nw, off += nw)
			if ((nw = write(wfd, buf + off, (size_t)nr)) == 0 ||
			    nw == -1)
				return (-1);
	if (nr < 0)
		return (-1);

	return (0);
}

void
_start_test(int *ret, const char *test_name, const char *request,
    void (*test_func)(void))
{
	struct pty pty = {0};
	int fildes[2];
	pid_t pid;
	int status;

	/* early print testname */
	printf("test(%s): pledge=", test_name);
	if (request) {
		printf("(\"%s\",", request);
		printf("NULL)");
	} else
		printf("skip");

	/* unlink previous coredump (if exists) */
	if (clear_coredump(ret, test_name) == -1)
		return;

	/* flush outputs (for STDOUT_FILENO manipulation) */
	if (fflush(NULL) != 0) {
		warn("test(%s) fflush", test_name);
		*ret = EXIT_FAILURE;
		return;
	}

	/* make pipe to grab output */
	if (pipe(fildes) != 0) {
		warn("test(%s) pipe", test_name);
		*ret = EXIT_FAILURE;
		return;
	}

	if (pty_open(&pty)) {
		*ret = EXIT_FAILURE;
		return;
	}

	/* fork and launch the test */
	switch (pid = fork()) {
	case -1:
		(void)close(fildes[0]);
		(void)close(fildes[1]);

		warn("test(%s) fork", test_name);
		*ret = EXIT_FAILURE;
		return;

	case 0:
		/* output to pipe */
		(void)close(fildes[0]);
		while (dup2(fildes[1], STDOUT_FILENO) == -1)
			if (errno != EINTR)
				err(errno, "dup2");

		if (pty_detach(&pty)) {
			*ret = EXIT_FAILURE;
			return;
		}

		/* create a new session (for kill) */
		setsid();

		if (pty_attach(&pty)) {
			*ret = EXIT_FAILURE;
			return;
		}

		/* set pledge policy */
		if (request && pledge(request, NULL) != 0)
			err(errno, "pledge");

		/* reset errno and launch test */
		errno = 0;
		test_func();

		if (errno != 0)
			_exit(errno);

		_exit(EXIT_SUCCESS);
		/* NOTREACHED */
	}

	if (pty_drain(&pty)) {
		*ret = EXIT_FAILURE;
		return;
	}

	/* copy pipe to output */
	(void)close(fildes[1]);
	if (drainfd(fildes[0], STDOUT_FILENO) != 0) {
		warn("test(%s): drainfd", test_name);
		*ret = EXIT_FAILURE;
		return;
	}
	if (close(fildes[0]) != 0) {
		warn("test(%s): close", test_name);
		*ret = EXIT_FAILURE;
		return;
	}

	/* wait for test to terminate */
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EAGAIN)
			continue;
		warn("test(%s): waitpid", test_name);
		*ret = EXIT_FAILURE;
		return;
	}

	/* show status and details */
	printf(" status=%d", status);

	if (WIFCONTINUED(status))
		printf(" continued");

	if (WIFEXITED(status)) {
		int e = WEXITSTATUS(status);
		printf(" exit=%d", e);
		if (e > 0 && e <= ELAST)
			printf(" (errno: \"%s\")", strerror(e));
	}

	if (WIFSIGNALED(status)) {
		int signal = WTERMSIG(status);
		printf(" signal=%d", signal);

		/* check if core file is really here ? */
		if (WCOREDUMP(status)) {
			int coredump = check_coredump();

			switch(coredump) {
			case -1: /* error */
				warn("test(%s): check_coredump", test_name);
				*ret = EXIT_FAILURE;
				return;

			case 0: /* found */
				printf(" coredump=present");
				break;

			case 1:	/* not found */
				printf(" coredump=absent");
				break;

			default:
				warnx("test(%s): unknown coredump code %d",
				    test_name, coredump);
				*ret = EXIT_FAILURE;
				return;
			}

		}

		/* grab pledged syscall from dmesg */
		if (signal == SIGKILL || signal == SIGABRT) {
			int syscall = grab_syscall(pid, pty_buffer(&pty));
			switch (syscall) {
			case -1:	/* error */
				warn("test(%s): grab_syscall pid=%d", test_name,
				    pid);
				*ret = EXIT_FAILURE;
				return;

			case 0:		/* not found */
				printf(" pledged_syscall=not_found");
				break;

			default:
				printf(" pledged_syscall=%d", syscall);
			}
		}
	}

	if (WIFSTOPPED(status))
		printf(" stop=%d", WSTOPSIG(status));

	pty_close(&pty);

	printf("\n");
}
