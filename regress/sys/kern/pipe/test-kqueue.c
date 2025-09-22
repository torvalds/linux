/*	$OpenBSD: test-kqueue.c,v 1.6 2023/10/14 13:05:43 anton Exp $	*/

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

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "pipe.h"

enum kqueue_mode {
	KQUEUE_READ,
	KQUEUE_READ_EOF,
	KQUEUE_WRITE,
	KQUEUE_WRITE_EOF,
};

struct context {
	enum kqueue_mode c_mode;
	int c_alive;

	int c_pipe[2];
	int c_kq;

	char *c_buf;
	size_t c_bufsiz;

	pthread_t c_th;
	pthread_mutex_t c_mtx;
};

static void ctx_setup(struct context *, enum kqueue_mode, int);
static void ctx_teardown(struct context *);
static int ctx_thread_alive(struct context *);
static void ctx_thread_start(struct context *);
static void ctx_lock(struct context *);
static void ctx_unlock(struct context *);

static void *kqueue_thread(void *);

/*
 * Verify kqueue read event.
 */
int
test_kqueue_read(void)
{
	struct context ctx;

	ctx_setup(&ctx, KQUEUE_READ, O_NONBLOCK);
	ctx_thread_start(&ctx);

	while (ctx_thread_alive(&ctx)) {
		ssize_t n;

		n = write(ctx.c_pipe[1], &ctx.c_buf[0], 1);
		if (n == -1) {
			if (errno == EPIPE)
				break;
			if (errno == EAGAIN)
				continue;
			err(1, "write");
		}
		if (n != 1)
			errx(1, "write: %ld != 1", n);
	}

	ctx_teardown(&ctx);

	return 0;
}

/*
 * Verify kqueue read EOF event.
 */
int
test_kqueue_read_eof(void)
{
	struct context ctx;

	ctx_setup(&ctx, KQUEUE_READ_EOF, 0);
	ctx_thread_start(&ctx);

	while (ctx_thread_alive(&ctx)) {
		if (ctx.c_pipe[1] == -1)
			continue;

		close(ctx.c_pipe[1]);
		ctx.c_pipe[1] = -1;
	}

	ctx_teardown(&ctx);

	return 0;
}

/*
 * Verify kqueue write event.
 */
int
test_kqueue_write(void)
{
	struct context ctx;
	ssize_t n;

	ctx_setup(&ctx, KQUEUE_WRITE, 0);

	n = write(ctx.c_pipe[1], ctx.c_buf, ctx.c_bufsiz);
	if (n == -1)
		err(1, "write");
	if ((size_t)n != ctx.c_bufsiz)
		errx(1, "write: %ld != %zu", n, ctx.c_bufsiz);

	ctx_thread_start(&ctx);

	while (ctx_thread_alive(&ctx)) {
		unsigned char c;

		n = read(ctx.c_pipe[0], &c, 1);
		if (n == -1)
			err(1, "read");
		if (n == 0)
			break;
		if (n != 1)
			errx(1, "read: %ld != 1", n);
	}

	ctx_teardown(&ctx);

	return 0;
}

/*
 * XXX Verify kqueue write event.
 */
int
test_kqueue_write_eof(void)
{

	return 0;
}

static void
ctx_setup(struct context *ctx, enum kqueue_mode mode, int flags)
{
	int error;

	ctx->c_mode = mode;
	ctx->c_alive = 1;

	if (flags) {
		if (pipe2(ctx->c_pipe, flags) == -1)
			err(1, "pipe");
	} else {
		if (pipe(ctx->c_pipe) == -1)
			err(1, "pipe");
	}

	ctx->c_kq = kqueue();
	if (ctx->c_kq == -1)
		err(1, "kqueue");

	ctx->c_bufsiz = PIPE_SIZE;
	ctx->c_buf = malloc(ctx->c_bufsiz);
	if (ctx->c_buf == NULL)
		err(1, NULL);

	error = pthread_mutex_init(&ctx->c_mtx, NULL);
	if (error)
		errc(1, error, "pthread_mutex_init");
}

static void
ctx_teardown(struct context *ctx)
{
	int error;

	error = pthread_join(ctx->c_th, NULL);
	if (error)
		errc(1, error, "pthread_join");

	error = pthread_mutex_destroy(&ctx->c_mtx);
	if (error)
		errc(1, error, "pthread_mutex_destroy");

	free(ctx->c_buf);

	close(ctx->c_pipe[0]);
	close(ctx->c_pipe[1]);
	close(ctx->c_kq);
}

static int
ctx_thread_alive(struct context *ctx)
{
	int alive;

	ctx_lock(ctx);
	alive = ctx->c_alive;
	ctx_unlock(ctx);
	return alive;
}

static void
ctx_thread_start(struct context *ctx)
{
	int error;

	error = pthread_create(&ctx->c_th, NULL, kqueue_thread, ctx);
	if (error)
		errc(1, error, "pthread_create");
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

static void *
kqueue_thread(void *arg)
{
	struct context *ctx = arg;
	struct kevent kev;
	int fd, filter, nevents;

	switch (ctx->c_mode) {
	case KQUEUE_READ:
	case KQUEUE_READ_EOF:
		fd = ctx->c_pipe[0];
		filter = EVFILT_READ;
		break;
	case KQUEUE_WRITE:
	case KQUEUE_WRITE_EOF:
		fd = ctx->c_pipe[1];
		filter = EVFILT_WRITE;
		break;
	}

	EV_SET(&kev, fd, filter, EV_ADD, 0, 0, NULL);
	nevents = kevent(ctx->c_kq, &kev, 1, NULL, 0, NULL);
	if (nevents == -1)
		err(1, "kevent");
	nevents = kevent(ctx->c_kq, NULL, 0, &kev, 1, NULL);
	if (nevents == -1)
		err(1, "kevent");
	if (nevents != 1)
		errx(1, "kevent: %d != 1", nevents);

	if ((int)kev.ident != fd)
		errx(1, "kevent: ident");
	if (kev.filter != filter)
		errx(1, "kevent: filter");

	switch (ctx->c_mode) {
	case KQUEUE_READ_EOF:
	case KQUEUE_WRITE_EOF:
		if ((kev.flags & EV_EOF) == 0)
			errx(1, "kevent: eof");
		break;
	default:
		break;
	}

	ctx_lock(ctx);
	ctx->c_alive = 0;
	ctx_unlock(ctx);

	close(fd);

	return NULL;
}
