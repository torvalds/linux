/*	$OpenBSD: pipe.c,v 1.5 2021/10/22 05:03:04 anton Exp $	*/

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

#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pipe.h"

static void sighandler(int);
static __dead void usage(void);

sig_atomic_t gotsigpipe = 0;
int infinity = 0;
int verbose = 0;

int
main(int argc, char *argv[])
{
	struct {
		const char *t_name;
		int (*t_fn)(void);
	} tests[] = {
		{ "close-race",				test_close_race },
		{ "kqueue-read",			test_kqueue_read },
		{ "kqueue-read-eof",			test_kqueue_read_eof },
		{ "kqueue-write",			test_kqueue_write },
		{ "kqueue-write-eof",			test_kqueue_write_eof },
		{ "ping-pong",				test_ping_pong },
		{ "run-down-write-big",			test_run_down_write_big },
		{ "run-down-write-small",		test_run_down_write_small },
		{ "select-hup",				test_select_hup },
		{ "thundering-herd-read-signal",	test_thundering_herd_read_signal },
		{ "thundering-herd-read-wakeup",	test_thundering_herd_read_wakeup },
		{ "thundering-herd-write-signal",	test_thundering_herd_write_signal },
		{ "thundering-herd-write-wakeup",	test_thundering_herd_write_wakeup },

		{ NULL, NULL },
	};
	int ch, i;

	while ((ch = getopt(argc, argv, "iv")) != -1) {
		switch (ch) {
		case 'i':
			infinity = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	if (signal(SIGPIPE, sighandler) == SIG_ERR)
		err(1, "signal");

	for (i = 0; tests[i].t_name != NULL; i++) {
		if (strcmp(argv[0], tests[i].t_name))
			continue;

		return tests[i].t_fn();
	}
	warnx("%s: no such test", argv[0]);

	return 1;
}

int
xwaitpid(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return WTERMSIG(status);
	return 0;
}

static void
sighandler(int signo)
{

	gotsigpipe = signo;
}

static __dead void
usage(void)
{

	fprintf(stderr, "usage: pipe [-iv] test-case\n");
	exit(1);
}
