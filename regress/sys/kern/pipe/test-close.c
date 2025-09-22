/*	$OpenBSD: test-close.c,v 1.1 2020/06/29 18:25:37 anton Exp $	*/

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
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "pipe.h"

struct context {
	volatile sig_atomic_t *c_alive;
	int c_fd;
};

static void *close_thread(void *);
static void sighandler(int);

static volatile sig_atomic_t alive = 1;

/*
 * Regression during close(2) causing a use-after-free.
 * The main thread repeatedly creates a new pipe which two other threads tries
 * to close. By default, 100 iterations is performed.
 */
int
test_close_race(void)
{
	pthread_t th1, th2;
	struct context ctx1, ctx2;
	int nrounds = 100;
	int pip[2];
	int error;

	if (signal(SIGINT, sighandler) == SIG_ERR)
		err(1, "signal");

	ctx1.c_alive = &alive;
	ctx1.c_fd = 3;
	error = pthread_create(&th1, NULL, close_thread, &ctx1);
	if (error)
		errc(1, error, "pthread_create");
	ctx2.c_alive = &alive;
	ctx2.c_fd = 4;
	error = pthread_create(&th2, NULL, close_thread, &ctx2);
	if (error)
		errc(1, error, "pthread_create");

	while (alive) {
		if (!infinity && nrounds-- == 0)
			alive = 0;

		if (pipe(pip) == -1)
			err(1, "pipe");
		if (pip[0] != 3)
			close(pip[0]);
		if (pip[1] != 4)
			close(pip[1]);
	}

	error = pthread_join(th1, NULL);
	if (error)
		errc(1, error, "pthread_join");
	error = pthread_join(th2, NULL);
	if (error)
		errc(1, error, "pthread_join");

	return 0;
}

static void *
close_thread(void *arg)
{
	const struct context *ctx = arg;

	while (*ctx->c_alive)
		close(ctx->c_fd);

	return NULL;
}

static void
sighandler(int signo)
{

	alive = 0;
}
