/*	$OpenBSD: input.c,v 1.19 2017/08/13 02:12:16 tedu Exp $	*/
/*    $NetBSD: input.c,v 1.3 1996/02/06 22:47:33 jtc Exp $    */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)input.c	8.1 (Berkeley) 5/31/93
 */

/*
 * Tetris input.
 */

#include <sys/time.h>

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "input.h"
#include "tetris.h"

/* return true iff the given timespec is positive */
#define	TS_POS(ts) \
	((ts)->tv_sec > 0 || ((ts)->tv_sec == 0 && (ts)->tv_nsec > 0))

/*
 * Do a `read wait': poll for reading from stdin, with timeout *limit.
 * On return, subtract the time spent waiting from *limit.
 * It will be positive only if input appeared before the time ran out;
 * otherwise it will be zero or perhaps negative.
 *
 * If limit is NULL, wait forever, but return if poll is interrupted.
 *
 * Return 0 => no input, 1 => can read() from stdin, -1 => interrupted
 */
int
rwait(struct timespec *limit)
{
	struct timespec start, end, elapsed;
	struct pollfd pfd[1];

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;

	if (limit != NULL)
		clock_gettime(CLOCK_MONOTONIC, &start);
again:
	switch (ppoll(pfd, 1, limit, NULL)) {
	case -1:
		if (limit == NULL)
			return (-1);
		if (errno == EINTR)
			goto again;
		stop("poll failed, help");
	case 0:	/* timed out */
		timespecclear(limit);
		return (0);
	}
	if (limit != NULL) {
		/* we have input, so subtract the elapsed time from *limit */
		clock_gettime(CLOCK_MONOTONIC, &end);
		timespecsub(&end, &start, &elapsed);
		timespecsub(limit, &elapsed, limit);
	}
	return (1);
}

/*
 * `sleep' for the current turn time and eat any
 * input that becomes available.
 */
void
tsleep(void)
{
	struct timespec ts;
	char c;

	ts.tv_sec = 0;
	ts.tv_nsec = fallrate;
	while (TS_POS(&ts))
		if (rwait(&ts) && read(STDIN_FILENO, &c, 1) != 1)
			break;
}

/*
 * getchar with timeout.
 */
int
tgetchar(void)
{
	static struct timespec timeleft;
	char c;

	/*
	 * Reset timeleft to fallrate whenever it is not positive.
	 * In any case, wait to see if there is any input.  If so,
	 * take it, and update timeleft so that the next call to
	 * tgetchar() will not wait as long.  If there is no input,
	 * make timeleft zero or negative, and return -1.
	 *
	 * Most of the hard work is done by rwait().
	 */
	if (!TS_POS(&timeleft)) {
		faster();	/* go faster */
		timeleft.tv_sec = 0;
		timeleft.tv_nsec = fallrate;
	}
	if (!rwait(&timeleft))
		return (-1);
	if (read(STDIN_FILENO, &c, 1) != 1)
		stop("end of file, help");
	return ((int)(unsigned char)c);
}
