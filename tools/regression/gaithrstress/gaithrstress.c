/*-
 * Copyright (c) 2004 Brian Fundakowski Feldman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <err.h>
#include <netdb.h>
#include <pthread.h>
#include <resolv.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Per-thread struct containing all important data. */
struct worker {
	pthread_t w_thread;			     /* self */
	uintmax_t w_lookup_success, w_lookup_failure;   /* getaddrinfo stats */
	struct timespec w_max_lookup_time;
};

static volatile int workers_stop = 0;
static double max_random_sleep = 1.0;
static char **randwords;
static size_t nrandwords;
static const struct addrinfo *hints, hintipv4only = { .ai_family = AF_INET };

/*
 * We don't have good random(3)-type functions that are thread-safe,
 * unfortunately.
 */
static u_int32_t
my_arc4random_r(void)
{
	static pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;
	u_int32_t ret;

	(void)pthread_mutex_lock(&mymutex);
	ret = arc4random();
	(void)pthread_mutex_unlock(&mymutex);
	return (ret);
}

static void
randomsleep(double max_sleep_sec)
{
	struct timespec slptime = { 0, 0 };
	double rndsleep;

	rndsleep = (double)my_arc4random_r() / 4294967296.0 * max_sleep_sec;
	while (rndsleep >= 1.0) {
		slptime.tv_sec++;
		rndsleep -= 1.0;
	}
	slptime.tv_nsec = rndsleep * 1e9;
	(void)nanosleep(&slptime, NULL);
}

/*
 * Start looking up arbitrary hostnames and record the successes/failures.
 * Between lookups, sleep a random amount of time to make sure threads
 * stay well out of synchronization.
 *
 * Host name:	part		probability
 *		----		-----------
 *		www.		1/2
 *		random word	always, equal
 *		random word	1/3, equal
 *		.(net|com|org)	equal
 */
static void *
work(void *arg)
{
	struct worker *w = arg;

	/* Turn off domain name list searching as much as possible. */
	if (_res.options & RES_INIT || res_init() == 0)
		_res.options &= ~RES_DNSRCH;
	do {
		const char *suffixes[] = { "net", "com", "org" };
		const size_t nsuffixes = sizeof(suffixes) / sizeof(suffixes[0]);
		struct timespec ts_begintime, ts_total;
		struct addrinfo *res;
		char *hostname;
		int error;

		randomsleep(max_random_sleep);
		if (asprintf(&hostname, "%s%s%s.%s",
		    (my_arc4random_r() % 2) == 0 ? "www." : "",
		    randwords[my_arc4random_r() % nrandwords],
		    (my_arc4random_r() % 3) == 0 ?
		    randwords[my_arc4random_r() % nrandwords] : "",
		    suffixes[my_arc4random_r() % nsuffixes]) == -1)
			continue;
		(void)clock_gettime(CLOCK_REALTIME, &ts_begintime);
		error = getaddrinfo(hostname, NULL, hints, &res);
		(void)clock_gettime(CLOCK_REALTIME, &ts_total);
		ts_total.tv_sec -= ts_begintime.tv_sec;
		ts_total.tv_nsec -= ts_begintime.tv_nsec;
		if (ts_total.tv_nsec < 0) {
			ts_total.tv_sec--;
			ts_total.tv_nsec += 1000000000;
		}
		if (ts_total.tv_sec > w->w_max_lookup_time.tv_sec ||
		    (ts_total.tv_sec == w->w_max_lookup_time.tv_sec &&
		    ts_total.tv_nsec > w->w_max_lookup_time.tv_sec))
			w->w_max_lookup_time = ts_total;
		free(hostname);
		if (error == 0) {
			w->w_lookup_success++;
			freeaddrinfo(res);
		} else {
			w->w_lookup_failure++;
		}
	} while (!workers_stop);

	pthread_exit(NULL);
}

int
dowordfile(const char *fname)
{
	FILE *fp;
	char newword[64];
	size_t n;

	fp = fopen(fname, "r");
	if (fp == NULL)
		return (-1);
	nrandwords = 0;
	while (fgets(newword, sizeof(newword), fp) != NULL)
		nrandwords++;
	if (ferror(fp) || fseek(fp, 0, SEEK_SET) != 0)
		goto fail;
	randwords = calloc(nrandwords, sizeof(char *));
	if (randwords == NULL)
		goto fail;
	n = nrandwords;
	nrandwords = 0;
	while (fgets(newword, sizeof(newword), fp) != NULL) {
		newword[strcspn(newword, "\r\n")] = '\0';
		randwords[nrandwords] = strdup(newword);
		if (randwords[nrandwords] == NULL)
			err(1, "reading words file");
		if (++nrandwords == n)
			break;
	}
	nrandwords = n;
	fclose(fp);
	return (0);
fail:
	fclose(fp);
	return (-1);
}

int
main(int argc, char **argv) {
	unsigned long nworkers = 1;
	struct worker *workers;
	size_t i;
	char waiting[3], *send, *wordfile = "/usr/share/dict/words";
	int ch;

	if (getprogname() == NULL)
		setprogname(argv[0]);
	printf("%s: threaded stress-tester for getaddrinfo(3)\n",
	    getprogname());
	printf("(c) 2004 Brian Feldman <green@FreeBSD.org>\n");
	while ((ch = getopt(argc, argv, "4s:t:w:")) != -1) {
		switch (ch) {
		case '4':
			hints = &hintipv4only;
			break;
		case 's':
			max_random_sleep = strtod(optarg, &send);
			if (*send != '\0')
				goto usage;
			break;
		case 't':
			nworkers = strtoul(optarg, &send, 0);
			if (*send != '\0')
				goto usage;
			break;
		case 'w':
			wordfile = optarg;
			break;
		default:
usage:
			fprintf(stderr, "usage: %s [-4] [-s sleep] "
			    "[-t threads] [-w wordfile]\n", getprogname());
			exit(2);
		}
	}
	argc -= optind;
	argv += optind;

	if (nworkers < 1 || nworkers != (size_t)nworkers)
		goto usage;
	if (dowordfile(wordfile) == -1)
		err(1, "reading word file %s", wordfile);
	if (nrandwords < 1)
		errx(1, "word file %s did not have >0 words", wordfile);
	printf("Read %zu random words from %s.\n", nrandwords, wordfile);
	workers = calloc(nworkers, sizeof(*workers));
	if (workers == NULL)
		err(1, "allocating workers");
	printf("Intra-query delay time is from 0 to %g seconds (random).\n",
	    max_random_sleep);

	printf("Starting %lu worker%.*s: ", nworkers, nworkers > 1, "s");
	fflush(stdout);
	for (i = 0; i < nworkers; i++) {
		if (pthread_create(&workers[i].w_thread, NULL, work,
		    &workers[i]) != 0)
			err(1, "creating worker %zu", i);
		printf("%zu%s", i, i == nworkers - 1 ? ".\n" : ", ");
		fflush(stdout);
	}

	printf("<Press enter key to end test.>\n");
	(void)fgets(waiting, sizeof(waiting), stdin);
	workers_stop = 1;

	printf("Stopping %lu worker%.*s: ", nworkers, nworkers > 1, "s");
	fflush(stdout);
	for (i = 0; i < nworkers; i++) {
		pthread_join(workers[i].w_thread, NULL);
		printf("%zu%s", i, i == nworkers - 1 ? ".\n" : ", ");
		fflush(stdout);
	}

	printf("%-10s%-20s%-20s%-29s\n", "Worker", "Successful GAI",
	    "Failed GAI", "Max resolution time (M:SS*)");
	printf("%-10s%-20s%-20s%-29s\n", "------", "--------------",
	    "----------", "---------------------------");
	for (i = 0; i < nworkers; i++) {
		printf("%-10zu%-20ju%-20ju%ld:%s%.2f\n", i,
		    workers[i].w_lookup_success, workers[i].w_lookup_failure,
		    workers[i].w_max_lookup_time.tv_sec / 60,
		    workers[i].w_max_lookup_time.tv_sec % 60 < 10 ? "0" : "",
		    (double)(workers[i].w_max_lookup_time.tv_sec % 60) +
		    (double)workers[i].w_max_lookup_time.tv_nsec / 1e9);
	}

	exit(0);
}
