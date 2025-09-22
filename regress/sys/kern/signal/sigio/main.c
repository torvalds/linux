/*	$OpenBSD: main.c,v 1.1 2020/09/16 14:02:23 mpi Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

static struct {
	const char	*t_name;
	int		(*t_func)(void);
} tests[] = {
	{ "pipe_badpgid",	test_pipe_badpgid },
	{ "pipe_badsession",	test_pipe_badsession },
	{ "pipe_cansigio",	test_pipe_cansigio },
	{ "pipe_getown",	test_pipe_getown },
	{ "pipe_read",		test_pipe_read },
	{ "pipe_write",		test_pipe_write },
	{ "socket_badpgid",	test_socket_badpgid },
	{ "socket_badsession",	test_socket_badsession },
	{ "socket_cansigio",	test_socket_cansigio },
	{ "socket_getown",	test_socket_getown },
	{ "socket_inherit",	test_socket_inherit },
	{ "socket_read",	test_socket_read },
	{ "socket_write",	test_socket_write },
	{ NULL,			NULL }
};

int
main(int argc, char *argv[])
{
	const char *t_name;
	int (*t_func)(void) = NULL;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s testname\n", getprogname());
		exit(1);
	}
	t_name = argv[1];

	for (i = 0; tests[i].t_name != NULL; i++) {
		if (strcmp(tests[i].t_name, t_name) == 0) {
			t_func = tests[i].t_func;
			break;
		}
	}
	if (t_func == NULL)
		errx(1, "unknown test: %s", t_name);

	test_init();

	return t_func();
}
