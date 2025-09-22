/*	$OpenBSD: rain.c,v 1.22 2021/10/23 11:22:49 mestre Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

/*
 * rain 11/3/1980 EPS/CITHEP
 * cc rain.c -o rain -O -ltermlib
 */

#include <curses.h>
#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t sig_caught = 0;

static void	onsig(int);


int
main(int argc, char *argv[])
{
	int x, y, j;
	long tcols, tlines;
	const char *errstr;
	struct termios term;
	struct timespec sleeptime;
	speed_t speed;
	time_t delay = 0;
	int ch;
	int xpos[5], ypos[5];

	/* set default delay based on terminal baud rate */
	if (tcgetattr(STDOUT_FILENO, &term) == 0 &&
	    (speed = cfgetospeed(&term)) > B9600)
		delay = (speed / B9600) - 1;

	while ((ch = getopt(argc, argv, "d:h")) != -1)
		switch(ch) {
		case 'd':
			delay = (time_t)strtonum(optarg, 0, 1000, &errstr);
			if (errstr)
			    errx(1, "delay (0-1000) is %s: %s", errstr, optarg);
			break;
		case 'h':
		default:
			(void)fprintf(stderr, "usage: rain [-d delay]\n");
			return 1;
		}

	/* Convert delay from ms -> ns */
	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = delay * 500000;
	timespecadd(&sleeptime, &sleeptime, &sleeptime);

	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	tcols = COLS - 4;
	tlines = LINES - 4;

	(void)signal(SIGHUP, onsig);
	(void)signal(SIGINT, onsig);
	(void)signal(SIGQUIT, onsig);
	(void)signal(SIGSTOP, onsig);
	(void)signal(SIGTSTP, onsig);
	(void)signal(SIGTERM, onsig);
	
	curs_set(0);
	for (j = 4; j >= 0; --j) {
		xpos[j] = arc4random_uniform(tcols) + 2;
		ypos[j] = arc4random_uniform(tlines) + 2;
	}
	for (j = 0;;) {
		if (sig_caught) {
			endwin();
			return 0;
		}
		x = arc4random_uniform(tcols) + 2;
		y = arc4random_uniform(tlines) + 2;
		mvaddch(y, x, '.');
		mvaddch(ypos[j], xpos[j], 'o');
		if (!j--)
			j = 4;
		mvaddch(ypos[j], xpos[j], 'O');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 1, xpos[j], '-');
		mvaddstr(ypos[j], xpos[j] - 1, "|.|");
		mvaddch(ypos[j] + 1, xpos[j], '-');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 2, xpos[j], '-');
		mvaddstr(ypos[j] - 1, xpos[j] - 1, "/ \\");
		mvaddstr(ypos[j], xpos[j] - 2, "| O |");
		mvaddstr(ypos[j] + 1, xpos[j] - 1, "\\ /");
		mvaddch(ypos[j] + 2, xpos[j], '-');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 2, xpos[j], ' ');
		mvaddstr(ypos[j] - 1, xpos[j] - 1, "   ");
		mvaddstr(ypos[j], xpos[j] - 2, "     ");
		mvaddstr(ypos[j] + 1, xpos[j] - 1, "   ");
		mvaddch(ypos[j] + 2, xpos[j], ' ');
		xpos[j] = x;
		ypos[j] = y;
		refresh();
		nanosleep(&sleeptime, NULL);
	}
}

static void
onsig(int dummy)
{
	sig_caught = 1;
}
