/*	$OpenBSD: dev-limit.c,v 1.3 2023/07/12 18:21:39 anton Exp $ */

/*
 * Copyright (c) 2023 Alexandr Nedvedicky <sashan@openbsd.org>
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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <err.h>
#include <sys/wait.h>

static int sigchild;

static void
usage(const char *progname)
{
	fprintf(stderr,
	    "%s [-d] [-s success_count] [-c child_count] [-t timeout]\n"
	    "if no options are specified program opens '/dev/pf'\n"
	    "and waits for 5s  before it exits\n"
	    "\t-s how many children should successfully open /dev/pf\n"
	    "\t-c children to fork, each child opens /dev/pf\n"
	    "\t-t timeout in seconds each child should wait\n"
	    "after successfully opening /dev/pf. Child exits immediately\n"
	    "if /dev/pf can not be opened\n", progname);
	exit(1);
}

static void
handle_sigchild(int signum)
{
	if (signum == SIGCHLD)
		sigchild = 1;
}

static void
open_pf_and_exit(unsigned int sleep_time)
{
	if (open("/dev/pf", O_RDONLY) == -1)
		exit(1);

	sleep(sleep_time);
	exit(0);
}

int
main(int argc, char *const argv[])
{
	pid_t *pids;
	unsigned int chld_count = 0;
	unsigned int sleep_time = 5;
	unsigned int expect_success = 0;
	unsigned int success, errors, i;
	const char *errstr, *sleep_arg;
	int status;
	int c;

	while ((c = getopt(argc, argv, "t:c:s:")) != -1) {
		switch (c) {
		case 't':
			sleep_arg = (char *const)optarg;
			sleep_time = strtonum(optarg, 1, 60, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "%s invalid sleep time %s: %s, must be in "
				    "range <1, 60>\n", argv[0], errstr, optarg);
				usage(argv[0]);
			}
			break;
		case 'c':
			chld_count = strtonum(optarg, 1, 32768, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "%s invalid children count %s: %s, must be "
				    "in range <1, 32768>\n", argv[0], optarg,
				    errstr);
				usage(argv[0]);
			}
			break;
		case 's':
			expect_success = strtonum(optarg, 0, 32768, &errstr);
			if (errstr != NULL) {
				fprintf(stderr,
				    "%s invalid expect success count %s: %s "
				    "must be in range <1, 32768>\n", argv[0],
				    optarg, errstr);
				usage(argv[0]);
			}
			break;
		default:
			usage(argv[0]);
		}
	}

	if (chld_count == 0)
		open_pf_and_exit(sleep_time);

	signal(SIGCHLD, handle_sigchild);
	pids = (pid_t *)malloc(sizeof(pid_t) * chld_count);
	if (pids == 0)
		err(1, NULL);

	i = 0;
	while ((sigchild == 0) && (i < chld_count)) {
		pid_t pid;

		pid = fork();
		pids[i++] = pid;
		if (pid == -1)
			warn("fork");
		else if (pid == 0)
			execl(argv[0], argv[0], "-t", sleep_arg, NULL);
	}
	chld_count = i;

	success = 0;
	errors = 0;
	for (i = 0; i < chld_count; i++) {
		waitpid(pids[i], &status, 0);
		if (status == 0)
			success++;
		else
			errors++;
	}

	free(pids);

	if (success != expect_success) {
		printf("Successful opens: %u\n", success);
		printf("Failures: %u\n", errors);
		printf("Expected opens: %u\n", expect_success);
		printf("%u vs %u = %u + %u\n",
		    chld_count, errors + success, errors, success);
		return (1);
	}

	return (0);
}
