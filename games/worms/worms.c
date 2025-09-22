/*	$OpenBSD: worms.c,v 1.30 2021/10/23 11:22:49 mestre Exp $	*/

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
 *
 *	 @@@	    @@@	   @@@@@@@@@@	  @@@@@@@@@@@	 @@@@@@@@@@@@
 *	 @@@	    @@@	  @@@@@@@@@@@@	  @@@@@@@@@@@@	 @@@@@@@@@@@@@
 *	 @@@	    @@@	 @@@@	   @@@@	  @@@@		 @@@@ @@@  @@@@
 *	 @@@   @@   @@@	 @@@	    @@@	  @@@		 @@@  @@@   @@@
 *	 @@@  @@@@  @@@	 @@@	    @@@	  @@@		 @@@  @@@   @@@
 *	 @@@@ @@@@ @@@@	 @@@	    @@@	  @@@		 @@@  @@@   @@@
 *	  @@@@@@@@@@@@	 @@@@	   @@@@	  @@@		 @@@  @@@   @@@
 *	   @@@@	 @@@@	  @@@@@@@@@@@@	  @@@		 @@@  @@@   @@@
 *	    @@	  @@	   @@@@@@@@@@	  @@@		 @@@  @@@   @@@
 *
 *				 Eric P. Scott
 *			  Caltech High Energy Physics
 *				 October, 1980
 *
 */
#include <curses.h>
#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static const struct options {
	int nopts;
	int opts[3];
}
	normal[8] = {
	{ 3, { 7, 0, 1 } },
	{ 3, { 0, 1, 2 } },
	{ 3, { 1, 2, 3 } },
	{ 3, { 2, 3, 4 } },
	{ 3, { 3, 4, 5 } },
	{ 3, { 4, 5, 6 } },
	{ 3, { 5, 6, 7 } },
	{ 3, { 6, 7, 0 } }
},	upper[8] = {
	{ 1, { 1, 0, 0 } },
	{ 2, { 1, 2, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 2, { 4, 5, 0 } },
	{ 1, { 5, 0, 0 } },
	{ 2, { 1, 5, 0 } }
},
	left[8] = {
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 2, { 2, 3, 0 } },
	{ 1, { 3, 0, 0 } },
	{ 2, { 3, 7, 0 } },
	{ 1, { 7, 0, 0 } },
	{ 2, { 7, 0, 0 } }
},
	right[8] = {
	{ 1, { 7, 0, 0 } },
	{ 2, { 3, 7, 0 } },
	{ 1, { 3, 0, 0 } },
	{ 2, { 3, 4, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 2, { 6, 7, 0 } }
},
	lower[8] = {
	{ 0, { 0, 0, 0 } },
	{ 2, { 0, 1, 0 } },
	{ 1, { 1, 0, 0 } },
	{ 2, { 1, 5, 0 } },
	{ 1, { 5, 0, 0 } },
	{ 2, { 5, 6, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } }
},
	upleft[8] = {
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 1, { 3, 0, 0 } },
	{ 2, { 1, 3, 0 } },
	{ 1, { 1, 0, 0 } }
},
	upright[8] = {
	{ 2, { 3, 5, 0 } },
	{ 1, { 3, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 1, { 5, 0, 0 } }
},
	lowleft[8] = {
	{ 3, { 7, 0, 1 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 1, { 1, 0, 0 } },
	{ 2, { 1, 7, 0 } },
	{ 1, { 7, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } }
},
	lowright[8] = {
	{ 0, { 0, 0, 0 } },
	{ 1, { 7, 0, 0 } },
	{ 2, { 5, 7, 0 } },
	{ 1, { 5, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } },
	{ 0, { 0, 0, 0 } }
};

static const char	flavor[] = {
	'O', '*', '#', '$', '%', '0', '@', '~'
};
static const short	xinc[] = {
	1,  1,  1,  0, -1, -1, -1,  0
}, yinc[] = {
	-1,  0,  1,  1,  1,  0, -1, -1
};
static struct	worm {
	int orientation, head;
	short *xpos, *ypos;
} *worm;

volatile sig_atomic_t sig_caught = 0;

void	 nomem(void);
void	 onsig(int);

int
main(int argc, char *argv[])
{
	int x, y, h, n;
	struct worm *w;
	const struct options *op;
	short *ip;
	int CO, LI, last, bottom, ch, length, number, trail;
	short **ref;
	const char *field, *errstr;
	struct timespec sleeptime;
	struct termios term;
	speed_t speed;
	time_t delay = 0;

	/* set default delay based on terminal baud rate */
	if (tcgetattr(STDOUT_FILENO, &term) == 0 &&
	    (speed = cfgetospeed(&term)) > B9600)
		delay = (speed / B9600) - 1;

	length = 16;
	number = 3;
	trail = ' ';
	field = NULL;
	while ((ch = getopt(argc, argv, "d:fhl:n:t")) != -1)
		switch(ch) {
		case 'd':
			delay = (time_t)strtonum(optarg, 0, 1000, &errstr);
			if (errstr)
				errx(1, "delay (0-1000) is %s: %s", errstr,
				    optarg);
			break;
		case 'f':
			field = "WORM";
			break;
		case 'l':
			length = strtonum(optarg, 2, 1024, &errstr);
			if (errstr)
				errx(1, "length (2-1024) is %s: %s", errstr,
				    optarg);
			break;
		case 'n':
			number = strtonum(optarg, 1, 100, &errstr);
			if (errstr)
				errx(1, "number of worms (1-100) is %s: %s",
				    errstr, optarg);
			break;
		case 't':
			trail = '.';
			break;
		case 'h':
		default:
			(void)fprintf(stderr, "usage: %s [-ft] [-d delay] "
			    "[-l length] [-n number]\n", getprogname());
			return 1;
		}

	/* Convert delay from ms -> ns */
	sleeptime.tv_sec = 0;
	sleeptime.tv_nsec = delay * 500000;
	timespecadd(&sleeptime, &sleeptime, &sleeptime);

	if (!(worm = calloc(number, sizeof(struct worm))))
		nomem();
	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	curs_set(0);
	CO = COLS;
	LI = LINES;
	last = CO - 1;
	bottom = LI - 1;
	if (!(ip = reallocarray(NULL, LI, CO * sizeof(short))) ||
	    !(ref = calloc(LI, sizeof(short *)))) {
		endwin();
		nomem();
	}
	for (n = 0; n < LI; ++n) {
		ref[n] = ip;
		ip += CO;
	}
	for (ip = ref[0], n = LI * CO; --n >= 0;)
		*ip++ = 0;
	for (n = number, w = &worm[0]; --n >= 0; w++) {
		w->orientation = w->head = 0;
		if (!(ip = calloc(length, sizeof(short)))) {
			endwin();
			nomem();
		}
		w->xpos = ip;
		for (x = length; --x >= 0;)
			*ip++ = -1;
		if (!(ip = calloc(length, sizeof(short)))) {
			endwin();
			nomem();
		}
		w->ypos = ip;
		for (y = length; --y >= 0;)
			*ip++ = -1;
	}

	(void)signal(SIGHUP, onsig);
	(void)signal(SIGINT, onsig);
	(void)signal(SIGQUIT, onsig);
	(void)signal(SIGSTOP, onsig);
	(void)signal(SIGTSTP, onsig);
	(void)signal(SIGTERM, onsig);

	if (field) {
		const char *p = field;

		for (y = LI; --y >= 0;) {
			for (x = CO; --x >= 0;) {
				addch(*p++);
				if (!*p)
					p = field;
			}
			refresh();
		}
	}
	for (;;) {
		refresh();
		if (sig_caught) {
			endwin();
			return 0;
		}
		nanosleep(&sleeptime, NULL);
		for (n = 0, w = &worm[0]; n < number; n++, w++) {
			if ((x = w->xpos[h = w->head]) < 0) {
				mvaddch(y = w->ypos[h] = bottom,
				    x = w->xpos[h] = 0,
				    flavor[n % sizeof(flavor)]);
				ref[y][x]++;
			}
			else
				y = w->ypos[h];
			if (++h == length)
				h = 0;
			if (w->xpos[w->head = h] >= 0) {
				int x1, y1;

				x1 = w->xpos[h];
				y1 = w->ypos[h];
				if (--ref[y1][x1] == 0)
					mvaddch(y1, x1, trail);
			}

			if (x == 0) {
				if (y == 0)
					op = &upleft[w->orientation];
				else if (y == bottom)
					op = &lowleft[w->orientation];
				else
					op = &left[w->orientation];
			} else if (x == last) {
				if (y == 0)
					op = &upright[w->orientation];
				else if (y == bottom)
					op = &lowright[w->orientation];
				else
					op = &right[w->orientation];
			} else {
				if (y == 0)
					op = &upper[w->orientation];
				else if (y == bottom)
					op = &lower[w->orientation];
				else
					op = &normal[w->orientation];
			}

			switch (op->nopts) {
			case 0:
				endwin();
				return(1);
			case 1:
				w->orientation = op->opts[0];
				break;
			default:
				w->orientation =
				    op->opts[arc4random_uniform(op->nopts)];
			}
			mvaddch(y += yinc[w->orientation],
			    x += xinc[w->orientation],
			    flavor[n % sizeof(flavor)]);
			ref[w->ypos[h] = y][w->xpos[h] = x]++;
		}
	}
}

void
onsig(int signo)
{
	sig_caught = 1;
}

void
nomem(void)
{
	errx(1, "not enough memory.");
}
