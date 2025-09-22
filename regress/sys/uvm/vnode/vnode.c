/*	$OpenBSD: vnode.c,v 1.3 2020/12/03 19:16:57 anton Exp $	*/

/*
 * Copyright (c) 2020 Anton Lindqvist <anton@openbsd.org>
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
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static void sighandler(int);
static void __dead usage(void);

int loglevel = 0;

static int gotsig;

int
main(int argc, char *argv[])
{
	struct {
		const char *t_name;
		int (*t_func)(struct context *);
	} tests[] = {
		{ "deadlock",	test_deadlock },

		{ NULL,	NULL },
	};
	struct context ctx;
	int ch, i;

	memset(&ctx, 0, sizeof(ctx));
	ctx.c_iterations = 100;

	while ((ch = getopt(argc, argv, "If:v")) != -1) {
		switch (ch) {
		case 'I':
			ctx.c_iterations = -1;
			break;
		case 'f':
			ctx.c_path = optarg;
			break;
		case 'v':
			loglevel++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1 || ctx.c_path == NULL)
		usage();

	if (signal(SIGINT, sighandler) == SIG_ERR)
		err(1, "signal");

	for (i = 0;; i++) {
		if (tests[i].t_name == NULL)
			err(1, "%s: no such test", *argv);

		if (strcmp(tests[i].t_name, *argv))
			continue;

		return tests[i].t_func(&ctx);
	}

	return 0;
}

int
ctx_abort(struct context *ctx)
{
	if (gotsig)
		return 1;
	if (ctx->c_iterations > 0 && --ctx->c_iterations == 0)
		return 1;
	return 0;
}

void
logit(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	char *p = buf;
	ssize_t siz = sizeof(buf);
	int n;

	n = snprintf(p, siz, "[%d] ", getpid());
	if (n < 0 || n >= siz)
		errc(1, ENAMETOOLONG, "%s", __func__);
	p += n;
	siz -= n;

	va_start(ap, fmt);
	n = vsnprintf(p, siz, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= siz)
		errc(1, ENAMETOOLONG, "%s", __func__);
	p += n;
	siz -= n;

	fprintf(stderr, "%s\n", buf);
}

static void
sighandler(int signo)
{
	gotsig = signo;
}

static void __dead
usage(void)
{
	fprintf(stderr, "usage: vnode [-Iv] -f path test-case\n");
	exit(1);
}
