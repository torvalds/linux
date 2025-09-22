/*	$OpenBSD: threads.c,v 1.1.1.1 2012/07/13 17:49:53 eric Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <err.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <netdb.h>

#define MAX_THREADS	50

int	  ac;
char	**av;
int	  loop;
int	  nthreads;

int long_err;
int gai_errno;
int rrset_errno;

void async_resolver_done(void *);

void stats(void)
{
	struct rusage	ru;

	getrusage(RUSAGE_SELF, &ru);
	printf("%li\n", ru.ru_maxrss);
}

void*
task(void *arg)
{
	int		 id, i, j, c;
	struct addrinfo *ai, *n;

	id = *((int*) arg);

	n = NULL; c =0;

	for(j = 0; j < loop; j++)
		for(i = 0; i < ac; i++) {
			if (getaddrinfo(av[i], NULL, NULL, &ai) == 0) {
/*
				for (c = 0, n = ai; n; c++, n = n->ai_next);
				printf("%i:%s: ok: %i\n", id, av[i], c);
*/
				freeaddrinfo(ai);
			} else {
/*
				printf("%i:%s: fail\n", id, av[i]);
*/
			}
		}
	return (NULL);
}

void
usage(void)
{
	extern const char *__progname;
	fprintf(stderr, "usage: %s [-L loop] [-l loop] [-t threads] <host> ...\n",
		__progname);
}

int
main(int argc, char **argv)
{
	pthread_t	th[MAX_THREADS];
	int		th_args[MAX_THREADS], r, i, ch;
	int		n, LOOP;

	nthreads = 1;
	loop = 1;
	LOOP = 1;

	while ((ch = getopt(argc, argv, "L:l:t:")) != -1) {
		switch (ch) {
		case 'L':
			LOOP = atoi(optarg);
			break;
		case 'l':
			loop = atoi(optarg);
			break;
		case 't':
			nthreads = atoi(optarg);
			if (nthreads > MAX_THREADS)
				nthreads = MAX_THREADS;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	ac = argc;
	av = argv;

	printf("%i %i %i\n", LOOP, nthreads, loop);
	for (n = 0; n < LOOP; n ++) {
		for (i = 0; i < nthreads; i++) {
			th_args[i] = i;
			r = pthread_create(&th[i], NULL, task, (void *) &th_args[i]);
			if (r == -1)
				errx(1, "pthread_create");
		}
		for (i = 0; i < nthreads; i++)
			pthread_join(th[i], NULL);

		if (nthreads == 0)
			task(&n);

		stats();
	}

	return (0);
}
