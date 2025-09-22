/*
 * Copyright (c) 2018 Otto Moerbeek <otto@drijf.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

pthread_cond_t cond;
pthread_mutex_t mutex;

void *p;

void *m(void *arg)
{
	p = malloc(100000);
	if (p == NULL)
		err(1, NULL);
	return NULL;
}

void *f(void *arg)
{
	free(p);
	free(p);
	return NULL;
}

void
catch(int x)
{
	_exit(0);
}

int
main(void)
{
	const struct rlimit lim = {0, 0};
	pthread_t t1, t2;

	/* prevent coredumps */
	setrlimit(RLIMIT_CORE, &lim);
	printf("This test is supposed to print a malloc error\n");

	signal(SIGABRT, catch);

	if (pthread_create(&t1, NULL, m, NULL))
		err(1, "pthread_create");
	pthread_join(t1, NULL);

	if (pthread_create(&t2, NULL, f, NULL))
		err(1, "pthread_create");
	pthread_join(t2, NULL);

	return 1;
}
