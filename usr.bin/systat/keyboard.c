/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifdef lint
static const char sccsid[] = "@(#)keyboard.c	8.1 (Berkeley) 6/6/93";
#endif

#include <sys/select.h>
#include <sys/time.h>

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "systat.h"
#include "extern.h"

static char line[80];
static int keyboard_dispatch(int ch);

int
keyboard(void)
{
	int ch, n;
	struct timeval last, intvl, now, tm;
	fd_set rfds;

	/* Set initial timings */
	gettimeofday(&last, NULL);
	intvl.tv_sec = delay / 1000000;
	intvl.tv_usec = delay % 1000000;
	for (;;) {
		col = 0;
		move(CMDLINE, 0);
		for (;;) {
			/* Determine interval to sleep */
			(void)gettimeofday(&now, NULL);
			tm.tv_sec = last.tv_sec + intvl.tv_sec - now.tv_sec;
			tm.tv_usec = last.tv_usec + intvl.tv_usec - now.tv_usec;
			while (tm.tv_usec < 0) {
				tm.tv_usec += 1000000;
				tm.tv_sec--;
			}
			while (tm.tv_usec >= 1000000) {
				tm.tv_usec -= 1000000;
				tm.tv_sec++;
			}
			if (tm.tv_sec < 0) {
				/* We have to update screen immediately */
				display();
				gettimeofday(&last, NULL);
				continue;
			}

			/* Prepare select  */
			FD_ZERO(&rfds);
			FD_SET(STDIN_FILENO, &rfds);
			n = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tm);

			if (n > 0) {
				/* Read event on stdin */
				ch = getch();

				if (keyboard_dispatch(ch) == 0) {
					refresh();
					continue;
				}
	
				line[col] = '\0';
				command(line + 1);
				/* Refresh delay */
				intvl.tv_sec = delay / 1000000;
				intvl.tv_usec = delay % 1000000;
				refresh();
				break;
			}

			if (n < 0 && errno != EINTR)
				exit(1);

			/* Timeout or signal. Call display another time */
			display();
			gettimeofday(&last, NULL);
		}
	}
}

static int
keyboard_dispatch(int ch)
{

	if (ch == ERR) {
		if (errno == EINTR)
			return 0;
		exit(1);
	}
	if (ch >= 'A' && ch <= 'Z')
		ch += 'a' - 'A';
	if (col == 0) {
		if (ch == CTRL('l')) {
			wrefresh(curscr);
			return 0;
		}
		if (ch == CTRL('g')) {
			status();
			return 0;
		}
		if (ch != ':')
			return 0;
		move(CMDLINE, 0);
		clrtoeol();
	}
	if (ch == erasechar() && col > 0) {
		if (col == 1 && line[0] == ':')
			return 0;
		col--;
		goto doerase;
	}
	if (ch == CTRL('w') && col > 0) {
		while (--col >= 0 && isspace(line[col]))
			;
		col++;
		while (--col >= 0 && !isspace(line[col]))
			if (col == 0 && line[0] == ':')
				return 1;
		col++;
		goto doerase;
	}
	if (ch == killchar() && col > 0) {
		col = 0;
		if (line[0] == ':')
			col++;
doerase:
		move(CMDLINE, col);
		clrtoeol();
		return 0;
	}
	if (isprint(ch) || ch == ' ') {
		line[col] = ch;
		mvaddch(CMDLINE, col, ch);
		col++;
	}

	if (col == 0 || (ch != '\r' && ch != '\n'))
		return 0;

	return 1;
}
