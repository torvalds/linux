/*	$OpenBSD: test-run-down.c,v 1.3 2021/10/22 05:03:57 anton Exp $	*/

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
#include <pthread.h>
#include <unistd.h>

#include "pipe.h"

struct context {
	int c_pipe[2];
	char *c_buf;
	size_t c_bufsiz;
	unsigned int c_nsyscalls;

	pthread_cond_t c_cv;
	pthread_mutex_t c_mtx;
};

static void ctx_setup(struct context *, size_t);
static void ctx_teardown(struct context *);
static void ctx_lock(struct context *);
static void ctx_unlock(struct context *);

static int test_run_down(size_t);

static void *write_thread(void *);

/*
 * Verify delivery of SIGPIPE while trying to write on a pipe where the read end
 * is gone.
 *
 * The writer thread first of writes BIG_PIPE_SIZE number of bytes and then
 * tries to perform the same operation again. The ambition is to cause the
 * writer thread to go to sleep since the pipe capacity is exhausted. The main
 * thread closes the read end at this point causing the writer thread to wake up
 * and punt.
 */
int
test_run_down_write_big()
{

	return test_run_down(BIG_PIPE_SIZE);
}

/*
 * Verify delivery of SIGPIPE while trying to write on a pipe where the read end
 * is gone.
 *
 * The writer thread writes a single byte continously while the main thread ends
 * up closing the read end after at least a single byte has been written. The
 * ambition is not to make the writer thread go to sleep but rather punt early
 * while writing since the read end is gone.
 */
int
test_run_down_write_small()
{

	return test_run_down(1);
}

static void
ctx_setup(struct context *ctx, size_t bufsiz)
{
	int error;

	if (pipe(ctx->c_pipe) == -1)
		err(1, "pipe");

	ctx->c_buf = malloc(bufsiz);
	if (ctx->c_buf == NULL)
		err(1, NULL);
	ctx->c_bufsiz = bufsiz;
	ctx->c_nsyscalls = 0;

	error = pthread_cond_init(&ctx->c_cv, NULL);
	if (error)
		errc(1, error, "pthread_cond_init");

	error = pthread_mutex_init(&ctx->c_mtx, NULL);
	if (error)
		errc(1, error, "pthread_mutex_init");
}

static void
ctx_teardown(struct context *ctx)
{
	int error;

	if (ctx->c_pipe[0] != -1)
		close(ctx->c_pipe[0]);
	ctx->c_pipe[0] = -1;
	if (ctx->c_pipe[1] != -1)
		close(ctx->c_pipe[1]);
	ctx->c_pipe[1] = -1;

	free(ctx->c_buf);

	error = pthread_cond_destroy(&ctx->c_cv);
	if (error)
		errc(1, error, "pthread_cond_destroy");

	error = pthread_mutex_destroy(&ctx->c_mtx);
	if (error)
		errc(1, error, "pthread_mutex_destroy");
}

static void
ctx_lock(struct context *ctx)
{
	int error;

	error = pthread_mutex_lock(&ctx->c_mtx);
	if (error)
		errc(1, error, "pthread_mutex_lock");
}

static void
ctx_unlock(struct context *ctx)
{
	int error;

	error = pthread_mutex_unlock(&ctx->c_mtx);
	if (error)
		errc(1, error, "pthread_mutex_unlock");
}

static int
test_run_down(size_t bufsiz)
{
	struct context ctx;
	pthread_t th;
	int error;

	ctx_setup(&ctx, bufsiz);

	error = pthread_create(&th, NULL, write_thread, &ctx);
	if (error)
		errc(1, error, "pthread_create");

	ctx_lock(&ctx);
	for (;;) {
		if (ctx.c_nsyscalls > 0)
			break;

		error = pthread_cond_wait(&ctx.c_cv, &ctx.c_mtx);
		if (error)
			errc(1, error, "pthread_cond_wait");
	}
	ctx_unlock(&ctx);

	/* Signal shutdown to the write thread. */
	close(ctx.c_pipe[0]);
	ctx.c_pipe[0] = -1;

	error = pthread_join(th, NULL);
	if (error)
		errc(1, error, "pthread_join");

	if (!gotsigpipe)
		errx(1, "no SIGPIPE");

	ctx_teardown(&ctx);

	return 0;
}

static void *
write_thread(void *arg)
{
	struct context *ctx = arg;
	int error;

	for (;;) {
		ssize_t n;

		n = write(ctx->c_pipe[1], ctx->c_buf, ctx->c_bufsiz);
		if (n == -1) {
			if (errno == EPIPE)
				break;
			err(1, "write");
		}

		ctx_lock(ctx);
		ctx->c_nsyscalls++;
		ctx_unlock(ctx);

		error = pthread_cond_signal(&ctx->c_cv);
		if (error)
			errc(1, error, "pthread_cond_signal");
	}

	return NULL;
}
