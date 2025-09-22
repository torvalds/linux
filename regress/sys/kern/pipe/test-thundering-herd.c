/*	$OpenBSD: test-thundering-herd.c,v 1.2 2019/11/14 21:17:00 anton Exp $	*/

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

/*
 * Verify correctness when multiple threads are waiting on I/O in either
 * pipe_read(9) or pipe_write(9).
 *
 * Multiple threads are put in this waiting state. The parent thread then
 * performs a read/write operation on the pipe causing all sleeping threads to
 * be woken up; only to end up sleeping in pipelock(9). The parent thread then
 * either closes the pipe or sends a signal to all threads, which must cause all
 * threads to abort its pipe operation.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pipe.h"

#define NCHILD	4

struct context {
	const char *c_ident;
	int c_id;
	int c_read;

	int c_pipe[2];
	int c_cv[2];

	char *c_buf;
	size_t c_bufsiz;
};

static int test_thundering_herd(int, int);

static pid_t block_proc(const struct context *);

int
test_thundering_herd_read_signal(void)
{

	return test_thundering_herd(1, 1);
}

int
test_thundering_herd_read_wakeup(void)
{

	return test_thundering_herd(1, 0);
}

int
test_thundering_herd_write_signal(void)
{

	return test_thundering_herd(0, 1);
}

int
test_thundering_herd_write_wakeup(void)
{

	return test_thundering_herd(0, 0);
}

static int
test_thundering_herd(int doread, int dosignal)
{
	pid_t pids[NCHILD];
	struct context ctx;
	ssize_t n;
	int i;
	unsigned char c = 'c';

	ctx.c_ident = doread ? "read" : "write";
	ctx.c_read = doread;
	if (pipe(ctx.c_pipe) == -1)
		err(1, "pipe");
	if (pipe(ctx.c_cv) == -1)
		err(1, "pipe");

	ctx.c_bufsiz = ctx.c_read ? 1 : BIG_PIPE_SIZE;
	ctx.c_buf = malloc(ctx.c_bufsiz);
	if (ctx.c_buf == NULL)
		err(1, NULL);

	for (i = 0; i < NCHILD; i++) {
		ctx.c_id = i + 1;
		pids[i] = block_proc(&ctx);
	}

	/*
	 * Let one child wakeup and force the other children into sleeping in
	 * pipelock(9).
	 */
	if (ctx.c_read)
		n = write(ctx.c_pipe[1], &c, 1);
	else
		n = read(ctx.c_pipe[0], &c, 1);
	if (n == -1)
		err(1, "%s", ctx.c_ident);
	if (n != 1)
		errx(1, "%s: %ld != 1", ctx.c_ident, n);

	/* Wait for signal from woken up child. */
	(void)read(ctx.c_cv[0], &c, 1);
	if (verbose)
		fprintf(stderr, "[p] got signal from child\n");

	if (dosignal) {
		for (i = 0; i < NCHILD; i++) {
			if (verbose)
				fprintf(stderr, "[p] kill %d\n", i + 1);
			if (kill(pids[i], SIGUSR1) == -1)
				err(1, "kill");
		}
	} else {
		if (ctx.c_read)
			close(ctx.c_pipe[1]);
		else
			close(ctx.c_pipe[0]);
	}

	for (i = 0; i < NCHILD; i++) {
		int ex;

		if (verbose)
			fprintf(stderr, "[p] wait %d\n", i + 1);
		ex = xwaitpid(pids[i]);
		if (ex == 0 || ex == SIGUSR1)
			continue;
		errx(1, "waitpid: %d != 0", ex);
	}

	return 0;
}

static pid_t
block_proc(const struct context *ctx)
{
	pid_t pid;

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		int rp = ctx->c_pipe[0];
		int wp = ctx->c_pipe[1];

		if (ctx->c_read)
			close(wp);
		else
			close(rp);

		for (;;) {
			ssize_t n;
			unsigned char c = 'c';

			if (ctx->c_read)
				n = read(rp, ctx->c_buf, ctx->c_bufsiz);
			else
				n = write(wp, ctx->c_buf, ctx->c_bufsiz);
			if (verbose)
				fprintf(stderr, "[%d] %s = %ld\n",
				    ctx->c_id, ctx->c_ident, n);
			if (n == -1) {
				if (errno == EPIPE)
					break;
				err(1, "[%d] %s", ctx->c_id, ctx->c_ident);
			}
			if (n == 0)
				break;

			/* Send signal to parent. */
			(void)write(ctx->c_cv[1], &c, 1);
		}

		_exit(0);
	}

	return pid;
}
