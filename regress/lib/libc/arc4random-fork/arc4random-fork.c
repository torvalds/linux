/*
 * Copyright (c) 2014 Google Inc.
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

#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define CHECK(x) assert(x)
#define CHECK_EQ(a, b) assert((a) == (b))
#define CHECK_NE(a, b) assert((a) != (b))
#define CHECK_GE(a, b) assert((a) >= (b))
#define CHECK_LE(a, b) assert((a) <= (b))

/* Test arc4random_buf(3) instead of arc4random(3). */
static int flagbuf;

/* Initialize arc4random(3) before forking. */
static int flagprefork;

enum {
	N = 4096
};

typedef struct {
	uint32_t x[N];
} Buf;

static int
isfullbuf(const Buf *buf)
{
	size_t i;
	for (i = 0; i < N; i++)
		if (buf->x[i])
			return (1);
	return (0);
}

static void
fillbuf(Buf *buf)
{
	if (flagbuf) {
		arc4random_buf(buf->x, sizeof(buf->x));
	} else {
		size_t i;
		for (i = 0; i < N; i++)
			buf->x[i] = arc4random();
	}
}

static void
usage()
{
	errx(1, "usage: arc4random-fork [-bp]");
}

static pid_t
safewaitpid(pid_t pid, int *status, int options)
{
	pid_t ret;
	do {
		ret = waitpid(pid, status, options);
	} while (ret == -1 && errno == EINTR);
	return (ret);
}

int
main(int argc, char *argv[])
{
	int opt, status;
	Buf *bufparent, *bufchildone, *bufchildtwo;
	pid_t pidone, pidtwo;
	size_t i, countone = 0, counttwo = 0, countkids = 0;

	/* Ensure SIGCHLD isn't set to SIG_IGN. */
	const struct sigaction sa = {
		.sa_handler = SIG_DFL,
	};
	CHECK_EQ(0, sigaction(SIGCHLD, &sa, NULL));

	while ((opt = getopt(argc, argv, "bp")) != -1) {
		switch (opt) {
		case 'b':
			flagbuf = 1;
			break;
		case 'p':
			flagprefork = 1;
			break;
		default:
			usage();
		}
	}

	if (flagprefork)
		(void)arc4random();

	bufparent = mmap(NULL, sizeof(Buf), PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_PRIVATE, -1, 0);
	CHECK_NE(MAP_FAILED, bufparent);

	bufchildone = mmap(NULL, sizeof(Buf), PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_SHARED, -1, 0);
	CHECK_NE(MAP_FAILED, bufchildone);

	bufchildtwo = mmap(NULL, sizeof(Buf), PROT_READ|PROT_WRITE,
	    MAP_ANON|MAP_SHARED, -1, 0);
	CHECK_NE(MAP_FAILED, bufchildtwo);

	pidone = fork();
	CHECK_GE(pidone, 0);
	if (pidone == 0) {
		fillbuf(bufchildone);
		_exit(0);
	}

	pidtwo = fork();
	CHECK_GE(pidtwo, 0);
	if (pidtwo == 0) {
		fillbuf(bufchildtwo);
		_exit(0);
	}

	fillbuf(bufparent);

	CHECK_EQ(pidone, safewaitpid(pidone, &status, 0));
	CHECK(WIFEXITED(status));
	CHECK_EQ(0, WEXITSTATUS(status));

	CHECK_EQ(pidtwo, safewaitpid(pidtwo, &status, 0));
	CHECK(WIFEXITED(status));
	CHECK_EQ(0, WEXITSTATUS(status));

	CHECK(isfullbuf(bufchildone));
	CHECK(isfullbuf(bufchildtwo));

	for (i = 0; i < N; i++) {
		countone += bufparent->x[i] == bufchildone->x[i];
		counttwo += bufparent->x[i] == bufchildtwo->x[i];
		countkids += bufchildone->x[i] == bufchildtwo->x[i];
	}

	/*
	 * These checks are inherently probabilistic and theoretically risk
	 * flaking, but there's less than a 1 in 2^40 chance of more than
	 * one pairwise match between two vectors of 4096 32-bit integers.
	 */
	CHECK_LE(countone, 1);
	CHECK_LE(counttwo, 1);
	CHECK_LE(countkids, 1);

	return (0);
}
