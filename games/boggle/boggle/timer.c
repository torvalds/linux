/*	$OpenBSD: timer.c,v 1.15 2016/08/27 02:11:27 guenther Exp $	*/
/*	$NetBSD: timer.c,v 1.3 1995/04/24 12:22:45 cgd Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Barry Brachman.
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
 */

#include <sys/select.h>
#include <curses.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

extern int TIMER_LINE, TIMER_COL;

static int waitch(long);

/*
 * Update the display of the remaining time while waiting for a character
 * If time runs out do a longjmp() to the game controlling routine, returning
 * non-zero; oth. return the character
 * Leave the cursor where it was initially
 */
int
timerch(void)
{
	extern int tlimit;
	extern time_t start_t;
	extern jmp_buf env;
	time_t prevt, t;
	int col, remaining, row;

	getyx(stdscr, row, col);
	prevt = 0L;
	for (;;) {
		if (waitch(1000L) == 1)
			break;
		time(&t);
		if (t == prevt)
			continue;
		prevt = t;
		remaining = tlimit - (int) (t - start_t);
		if (remaining < 0) {
			longjmp(env, 1);
		}
		move(TIMER_LINE, TIMER_COL);
		printw("%d:%02d", remaining / 60, remaining % 60);
		move(row, col);
		refresh();
	}
	return (inputch());
}

/*
 * Wait up to 'delay' microseconds for input to appear
 * Returns 1 if input is ready, 0 oth.
 */
static int
waitch(long delay)
{
	fd_set fdbits;
	struct timeval duration;

	duration.tv_sec = 0;
	duration.tv_usec = delay;
	FD_ZERO(&fdbits);
	FD_SET(STDIN_FILENO, &fdbits);
	return (select(STDIN_FILENO+1, &fdbits, NULL, NULL, &duration));
}

void
delay(int tenths)
{
	struct timeval duration;

	duration.tv_usec = (tenths % 10 ) * 100000L;
	duration.tv_sec = tenths / 10;
	select(0, 0, 0, 0, &duration);
}
