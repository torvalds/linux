/*	$OpenBSD: worm.c,v 1.39 2018/08/24 11:14:49 mestre Exp $	*/

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
 * Worm.  Written by Michael Toy
 * UCSC
 */

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#define HEAD '@'
#define BODY 'o'
#define LENGTH 7
#define RUNLEN 8
#define CNTRL(p) (p-'A'+1)

WINDOW *tv;
WINDOW *stw;
struct body {
	int x;
	int y;
	struct body *prev;
	struct body *next;
} *head, *tail, goody;
int growing = 0;
int growthscale = 1;
int running = 0;
int slow = 0;
int score = 0;
int start_len = LENGTH;
int visible_len;
int lastch;
char outbuf[BUFSIZ];

volatile sig_atomic_t wantleave = 0;
volatile sig_atomic_t wantsuspend = 0;

__dead void	crash(void);
void	display(struct body *, char);
void	leave(int);
void	life(void);
void	newpos(struct body *);
struct body 	*newlink(void);
int	process(int);
void	prize(void);
int	rnd(int);
void	setup(void);
void	suspend(int);

int
main(int argc, char **argv)
{
	int retval;
	struct pollfd pfd[1];
	const char *errstr;
	struct timespec t, tn, tdiff;

	timespecclear(&t);

	setvbuf(stdout, outbuf, _IOFBF, sizeof outbuf);
	signal(SIGINT, leave);
	signal(SIGQUIT, leave);
	signal(SIGTSTP, suspend);	/* process control signal */
	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	slow = (baudrate() <= 1200);
	clear();
	if (COLS < 18 || LINES < 5) {
		endwin();
		errx(1, "screen too small");
	}
	growthscale = COLS * LINES / 2000;
	if (growthscale == 0)
		growthscale = 1;
	if (argc >= 2) {
		start_len = strtonum(argv[1], 1, ((LINES-3) * (COLS-2)) / 3,
		    &errstr);
		if (errstr) {
			endwin();
			errx(1, "length argument is %s.", errstr);
		}
	}
	stw = newwin(1, COLS-1, 0, 0);
	tv = newwin(LINES-1, COLS-1, 1, 0);
	box(tv, '*', '*');
	scrollok(tv, FALSE);
	scrollok(stw, FALSE);
	wmove(stw, 0, 0);
	wprintw(stw, " Worm");
	refresh();
	wrefresh(stw);
	wrefresh(tv);
	life();			/* Create the worm */
	prize();		/* Put up a goal */
	wmove(tv, head->y, head->x);    /* Leave cursor on worm */
	wrefresh(tv);
	while (1) {
		if (wantleave) {
			endwin();
			return 0;
		}
		if (wantsuspend) {
			move(LINES-1, 0);
			refresh();
			endwin();
			fflush(stdout);
			kill(getpid(), SIGSTOP);
			signal(SIGTSTP, suspend);
			cbreak();
			noecho();
			setup();
			wantsuspend = 0;
		}

		if (running) {
			running--;
			process(lastch);
		} else {
			/* Check for timeout. */
			clock_gettime(CLOCK_MONOTONIC, &tn);
			if (timespeccmp(&t, &tn, <=)) {
				t = tn;
				t.tv_sec += 1;

				process(lastch);
				continue;
			}

			/* Prepare next read */
			pfd[0].fd = STDIN_FILENO;
			pfd[0].events = POLLIN;
			timespecsub(&t, &tn, &tdiff);
			retval = ppoll(pfd, 1, &tdiff, NULL);

			/* Nothing to do if timed out or signal. */
			if (retval <= 0)
				continue;

			/* Only update timer if valid key was pressed. */
			if (process(getch()) == 0)
				continue;

			/* Update using clock_gettime(), tn is too old now. */
			clock_gettime(CLOCK_MONOTONIC, &t);
			t.tv_sec += 1;
		}
	}
}

void
life(void)
{
	struct body *bp, *np;
	int i,j = 1;

	head = newlink();
	head->x = start_len % (COLS-5) + 2;
	head->y = LINES / 2;
	head->next = NULL;
	display(head, HEAD);
	for (i = 0, bp = head; i < start_len; i++, bp = np) {
		np = newlink();
		np->next = bp;
		bp->prev = np;
		if (((bp->x <= 2) && (j == 1)) || ((bp->x >= COLS-4) && (j == -1))) {
			j *= -1;
			np->x = bp->x;
			np->y = bp->y + 1;
		} else {
			np->x = bp->x - j;
			np->y = bp->y;
		}
		display(np, BODY);
	}
	tail = np;
	tail->prev = NULL;
	visible_len = start_len + 1;
}

void
display(struct body *pos, char chr)
{
	wmove(tv, pos->y, pos->x);
	waddch(tv, chr);
}

void
leave(int dummy)
{
	wantleave = 1;
}

int
rnd(int range)
{
	return arc4random_uniform(range);
}

void
newpos(struct body *bp)
{
	if (visible_len == (LINES-3) * (COLS-3) - 1) {
		endwin();
		printf("\nYou won!\nYour final score was %d\n\n", score);
		exit(0);
	}
	do {
		bp->y = rnd(LINES-3)+ 1;
		bp->x = rnd(COLS-3) + 1;
		wmove(tv, bp->y, bp->x);
	} while(winch(tv) != ' ');
}

void
prize(void)
{
	int value;

	value = rnd(9) + 1;
	newpos(&goody);
	waddch(tv, value+'0');
	wrefresh(tv);
}

int
process(int ch)
{
	int x,y;
	struct body *nh;

	x = head->x;
	y = head->y;
	switch(ch) {
#ifdef KEY_LEFT
	case KEY_LEFT:
#endif
	case 'h':
		x--;
		break;
#ifdef KEY_DOWN
	case KEY_DOWN:
#endif
	case 'j':
		y++;
		break;
#ifdef KEY_UP
	case KEY_UP:
#endif
	case 'k':
		y--;
		break;
#ifdef KEY_RIGHT
	case KEY_RIGHT:
#endif
	case 'l':
		x++;
		break;
	case 'H':
		x--;
		running = RUNLEN;
		ch = tolower(ch);
		break;
	case 'J':
		y++;
		running = RUNLEN/2;
		ch = tolower(ch);
		break;
	case 'K':
		y--;
		running = RUNLEN/2;
		ch = tolower(ch);
		break;
	case 'L':
		x++;
		running = RUNLEN;
		ch = tolower(ch);
		break;
	case '\f':
		setup();
		return (0);
	case CNTRL('Z'):
		suspend(0);
		return (0);
	case CNTRL('C'):
		crash();
		return (0);
	case CNTRL('D'):
		crash();
		return (0);
	case ERR:
		leave(0);
		return (0);
	default:
		return (0);
	}
	lastch = ch;
	if (growing == 0) {
		display(tail, ' ');
		tail->next->prev = NULL;
		nh = tail->next;
		free(tail);
		tail = nh;
		visible_len--;
	} else
		growing--;
	display(head, BODY);
	wmove(tv, y, x);
	if (isdigit(ch = winch(tv))) {
		int amt = ch - '0';
		growing += amt * growthscale;
		prize();
		score += amt;
		running = 0;
		wmove(stw, 0, COLS - 12);
		wprintw(stw, "Score: %3d", score);
		wrefresh(stw);
	} else if(ch != ' ')
		crash();
	nh = newlink();
	nh->next = NULL;
	nh->prev = head;
	head->next = nh;
	nh->y = y;
	nh->x = x;
	display(nh, HEAD);
	head = nh;
	visible_len++;
	if (!(slow && running)) {
		wmove(tv, head->y, head->x);
		wrefresh(tv);
	}
	return (1);
}

struct body *
newlink(void)
{
	struct body *tmp;

	if ((tmp = malloc(sizeof (struct body))) == NULL) {
		endwin();
		errx(1, "out of memory");
	}
	return (tmp);
}

void
crash(void)
{
	sleep(2);
	clear();
	endwin();
	printf("Well, you ran into something and the game is over.\n");
	printf("Your final score was %d\n", score);
	exit(0);  /* leave() calls endwin(), which would hose the printf()'s */
}

void
suspend(int dummy)
{
	wantsuspend = 1;
}

void
setup(void)
{
	clear();
	refresh();
	touchwin(stw);
	wrefresh(stw);
	touchwin(tv);
	wrefresh(tv);
}
