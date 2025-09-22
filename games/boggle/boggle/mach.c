/*	$OpenBSD: mach.c,v 1.23 2023/10/10 08:22:19 tb Exp $	*/
/*	$NetBSD: mach.c,v 1.5 1995/04/28 22:28:48 mycroft Exp $	*/

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

/*
 * Terminal interface
 *
 * Input is raw and unechoed
 */
#include <sys/ioctl.h>

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include "bog.h"
#include "extern.h"

static int ccol, crow, maxw;
static int colstarts[MAXCOLS], ncolstarts;
static char *separator;
int ncols, nlines, lastline;

/* 
 * The following determine the screen layout
 */
int PROMPT_COL	= 20;
int PROMPT_LINE	= 3;

int BOARD_COL	= 0;
int BOARD_LINE	= 0;

int SCORE_COL	= 20;
int SCORE_LINE	= 0;

int LIST_COL	= 0;
int LIST_LINE	= 10;

int TIMER_COL	= 20;
int TIMER_LINE	= 2;

extern char **pword, **mword;
extern int ngames, nmwords, npwords, tnmwords, tnpwords, ncubes, grid;

static void	cont_catcher(int);
static int	prwidth(char **, int);
static void	prword(char **, int);
static void	stop_catcher(int);
static void	tty_cleanup(void);
static int	tty_setup(void);
static void	tty_showboard(char *);
static void	winch_catcher(int);

/*
 * Do system dependent initialization
 * This is called once, when the program starts
 */
int
setup(void)
{
	char *cp, *ep;

	if (tty_setup() < 0)
		return(-1);

	separator = malloc(4 * grid + 2);
	if (separator == NULL)
		err(1, NULL);

	ep = separator + 4 * grid;
	for (cp = separator; cp < ep;) {
		*cp++ = '+';
		*cp++ = '-';
		*cp++ = '-';
		*cp++ = '-';
	}
	*cp++ = '+';
	*cp = '\0';

	SCORE_COL += (grid - 4) * 4;
	TIMER_COL += (grid - 4) * 4;
	PROMPT_COL += (grid - 4) * 4;
	LIST_LINE += (grid - 4) * 2;

	return(0);
}

/*
 * Do system dependent clean up
 * This is called once, just before the program terminates
 */
void
cleanup(void)
{
	tty_cleanup();
}

/*
 * Display the player's word list, the list of words not found, and the running
 * stats
 */
void
results(void)
{
	int col, row;
	int denom1, denom2;

	move(LIST_LINE, LIST_COL);
	clrtobot();
	printw("Words you found (%d):", npwords);
	refresh();
	move(LIST_LINE + 1, LIST_COL);
	prtable(pword, npwords, 0, ncols, prword, prwidth);

	getyx(stdscr, row, col);
	move(row + 1, col);
	printw("Words you missed (%d):", nmwords);
	refresh();
	move(row + 2, col);
	prtable(mword, nmwords, 0, ncols, prword, prwidth);

	denom1 = npwords + nmwords;
	denom2 = tnpwords + tnmwords;
 
	move(SCORE_LINE, SCORE_COL);
	printw("Score: %d out of %d\n", npwords, denom1);
	move(SCORE_LINE + 1, SCORE_COL);
	printw("Percentage: %0.2f%% (%0.2f%% over %d game%s)\n",
	denom1 ? (100.0 * npwords)  / (double) denom1 : 0.0,
	denom2 ? (100.0 * tnpwords) / (double) denom2 : 0.0,
	ngames, ngames > 1 ? "s" : "");
	move(TIMER_LINE, TIMER_COL);
	wclrtoeol(stdscr);
}

static void
prword(char **base, int indx)
{
	printw("%s", base[indx]);
}

static int
prwidth(char **base, int indx)
{
	return (strlen(base[indx]));
}

/*
 * Main input routine
 *
 * - doesn't accept words longer than MAXWORDLEN or containing caps
 */
char *
get_line(char *q)
{
	int ch, done;
	char *p;
	int row, col;

	p = q;
	done = 0;
	while (!done) {
		ch = timerch();
		switch (ch) {
		case '\n':
		case '\r':
		case ' ':
			done = 1;
			break;
		case '\033':
			findword();
			break;
		case '\177':			/* <del> */
		case '\010':			/* <bs> */
			if (p == q)
				break;
			p--;
			getyx(stdscr, row, col);
			move(row, col - 1);
			clrtoeol();
			refresh();
			break;
		case '\025':			/* <^u> */
		case '\027':			/* <^w> */
			if (p == q)
				break;
			getyx(stdscr, row, col);
			move(row, col - (int) (p - q));
			p = q;
			clrtoeol();
			refresh();
			break;
#ifdef SIGTSTP
		case '\032':			/* <^z> */
			stop_catcher(0);
			break;
#endif
		case '\023':			/* <^s> */
			stoptime();
			printw("<PAUSE>");
			refresh();
			while ((ch = inputch()) != '\021' && ch != '\023')
				;
			move(crow, ccol);
			clrtoeol();
			refresh();
			starttime();
			break;
		case '\003':			/* <^c> */
			cleanup();
			exit(0);
		case '\004':			/* <^d> */
			done = 1;
			ch = EOF;
			break;
		case '\014':			/* <^l> */
		case '\022':			/* <^r> */
			redraw();
			break;
		case '?':
			stoptime();
			if (help() < 0)
				showstr("Can't open help file", 1);
			starttime();
			break;
		default:
			if (!islower(ch))
				break;
			if ((int) (p - q) == MAXWORDLEN) {
				p = q;
				badword();
				break;
			}
			*p++ = ch;
			addch(ch);
			refresh();
			break;
		}
	}
	*p = '\0';
	if (ch == EOF)
		return(NULL);
	return(q);
}

int
inputch(void)
{
	int ch;

	if ((ch = getch()) == ERR)
		err(1, "cannot read input");
	return (ch & 0177);
}

void
redraw(void)
{
	clearok(stdscr, 1);
	refresh();
}

void
flushin(FILE *fp)
{

	(void) tcflush(fileno(fp), TCIFLUSH);
}

static int gone;

/*
 * Stop the game timer
 */
void
stoptime(void)
{
	extern time_t start_t;
	time_t t;

	(void)time(&t);
	gone = (int) (t - start_t);
}

/*
 * Restart the game timer
 */
void
starttime(void)
{
	extern time_t start_t;
	time_t t;

	(void)time(&t);
	start_t = t - (long) gone;
}

/*
 * Initialize for the display of the player's words as they are typed
 * This display starts at (LIST_LINE, LIST_COL) and goes "down" until the last
 * line.  After the last line a new column is started at LIST_LINE
 * Keep track of each column position for showword()
 * There is no check for exceeding COLS
 */
void
startwords(void)
{
	crow = LIST_LINE;
	ccol = LIST_COL;
	maxw = 0;
	ncolstarts = 1;
	colstarts[0] = LIST_COL;
	move(LIST_LINE, LIST_COL);
	refresh();
}

/*
 * Add a word to the list and start a new column if necessary
 * The maximum width of the current column is maintained so we know where
 * to start the next column
 */
void
addword(char *w)
{
	int n;

	if (crow == lastline) {
		crow = LIST_LINE;
		ccol += (maxw + 5);
		colstarts[ncolstarts++] = ccol;
		maxw = 0;
		move(crow, ccol);
	}
	else {
		move(++crow, ccol);
		if ((n = strlen(w)) > maxw)
			maxw = n;
	}
	refresh();
}

/*
 * The current word is unacceptable so erase it
 */
void
badword(void)
{

	move(crow, ccol);
	clrtoeol();
	refresh();
}

/*
 * Highlight the nth word in the list (starting with word 0)
 * No check for wild arg
 */
void
showword(int n)
{
	int col, row;

	row = LIST_LINE + n % (lastline - LIST_LINE + 1);
	col = colstarts[n / (lastline - LIST_LINE + 1)];
	move(row, col);
	standout();
	printw("%s", pword[n]);
	standend();
	move(crow, ccol);
	refresh();
	delay(15);
	move(row, col);
	printw("%s", pword[n]);
	move(crow, ccol);
	refresh();
}

/*
 * Walk the path of a word, refreshing the letters,
 * optionally pausing after each
 */
static void
doword(int pause, int r, int c)
{
	extern char *board;
	extern int wordpath[];
	int i, row, col;
	unsigned char ch;

	for (i = 0; wordpath[i] != -1; i++) {
		row = BOARD_LINE + (wordpath[i] / 4) * 2 + 1;
		col = BOARD_COL + (wordpath[i] % 4) * 4 + 2;
		move(row, col);
		ch = board[wordpath[i]];
		if (HISET(ch))
			attron(A_BOLD);
		if (SEVENBIT(ch) == 'q')
			printw("Qu");
		else
			printw("%c", toupper(SEVENBIT(ch)));
		if (HISET(ch))
			attroff(A_BOLD);
		if (pause) {
			move(r, c);
			refresh();
			delay(5);
		}
	}
}

/*
 * Get a word from the user and check if it is in either of the two
 * word lists
 * If it's found, show the word on the board for a short time and then
 * erase the word
 *
 * Note: this function knows about the format of the board
 */
void
findword(void)
{
	int c, found, i, r;
	char buf[MAXWORDLEN + 1];
	extern int usedbits, wordpath[];
	extern char **mword, **pword;
	extern int nmwords, npwords;

	getyx(stdscr, r, c);
	getword(buf);
	found = 0;
	for (i = 0; i < npwords; i++) {
		if (strcmp(buf, pword[i]) == 0) {
			found = 1;
			break;
		}
	}
	if (!found) {
		for (i = 0; i < nmwords; i++) {
			if (strcmp(buf, mword[i]) == 0) {
				found = 1;
				break;
			}
		}
	}
	for (i = 0; i < MAXWORDLEN; i++)
		wordpath[i] = -1;
	usedbits = 0;
	if (!found || checkword(buf, -1, wordpath) == -1) {
		move(r, c);
		clrtoeol();
		addstr("[???]");
		refresh();
		delay(10);
		move(r, c);
		clrtoeol();
		refresh();
		return;
	}

	standout();
	doword(1, r, c);
	standend();
	doword(0, r, c);

	move(r, c);
	clrtoeol();
	refresh();
}

/*
 * Display a string at the current cursor position for the given number of secs
 */
void
showstr(char *str, int delaysecs)
{
	addstr(str);
	refresh();
	delay(delaysecs * 10);
	move(crow, ccol);
	clrtoeol();
	refresh();
}

void
putstr(char *s)
{
	addstr(s);
}

/*
 * Get a valid word and put it in the buffer
 */
void
getword(char *q)
{
	int ch, col, done, i, row;
	char *p;

	done = 0;
	i = 0;
	p = q;
	addch('[');
	refresh();
	while (!done && i < MAXWORDLEN - 1) {
		ch = inputch();
		switch (ch) {
		case '\177':			/* <del> */
		case '\010':			/* <bs> */
			if (p == q)
				break;
			p--;
			getyx(stdscr, row, col);
			move(row, col - 1);
			clrtoeol();
			break;
		case '\025':			/* <^u> */
		case '\027':			/* <^w> */
			if (p == q)
				break;
			getyx(stdscr, row, col);
			move(row, col - (int) (p - q));
			p = q;
			clrtoeol();
			break;
		case ' ':
		case '\n':
		case '\r':
			done = 1;
			break;
		case '\014':			/* <^l> */
		case '\022':			/* <^r> */
			clearok(stdscr, 1);
			refresh();
			break;
		default:
			if (islower(ch)) {
				*p++ = ch;
				addch(ch);
				i++;
			}
			break;
		}
		refresh();
	}
	*p = '\0';
	addch(']');
	refresh();
}

void
showboard(char *b)
{
	tty_showboard(b);
}

void
prompt(char *mesg)
{
	move(PROMPT_LINE, PROMPT_COL);
	printw("%s", mesg);
	move(PROMPT_LINE + 1, PROMPT_COL);
	refresh();
}

static int
tty_setup(void)
{
	initscr();
	raw();
	noecho();

	/*
	 * Does curses look at the winsize structure?
	 * Should handle SIGWINCH ...
	 */
	nlines = LINES;
	lastline = nlines - 1;
	ncols = COLS;

	signal(SIGTSTP, stop_catcher);
	signal(SIGCONT, cont_catcher);
	signal(SIGWINCH, winch_catcher);
	return(0);
}

static void
stop_catcher(int signo)
{
	sigset_t sigset, osigset;

	stoptime();
	noraw();
	echo();
	move(nlines - 1, 0);
	refresh();

	signal(SIGTSTP, SIG_DFL);
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTSTP);
	sigprocmask(SIG_UNBLOCK, &sigset, &osigset);
	kill(0, SIGTSTP);
	sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
	signal(SIGTSTP, stop_catcher);
}
 
static void
cont_catcher(int signo)
{
	noecho();
	raw();
	clearok(stdscr, 1);
	move(crow, ccol);
	refresh();
	starttime();
}
 
/*
 * The signal is caught but nothing is done about it...
 * It would mean reformatting the entire display
 */
static void
winch_catcher(int signo)
{
	struct winsize win;

	(void) signal(SIGWINCH, winch_catcher);
	(void) ioctl(fileno(stdout), TIOCGWINSZ, &win);
	/*
	LINES = win.ws_row;
	COLS = win.ws_col;
	*/
}

static void
tty_cleanup(void)
{
	move(nlines - 1, 0);
	refresh();
	noraw();
	echo();
	endwin();
}

static void
tty_showboard(char *b)
{
	int i, line;
	char ch;

	clear();
	move(BOARD_LINE, BOARD_COL);
	line = BOARD_LINE;
	printw("%s", separator);
	move(++line, BOARD_COL);
	for (i = 0; i < ncubes; i++) {
		printw("| ");
		ch = SEVENBIT(b[i]);
		if (HISET(b[i]))
			attron(A_BOLD);
		if (ch == 'q')
			printw("Qu");
		else
			printw("%c ", toupper((unsigned char)ch));
		if (HISET(b[i]))
			attroff(A_BOLD);
		if ((i + 1) % grid == 0) {
			printw("|");
			move(++line, BOARD_COL);
			printw("%s", separator);
			move(++line, BOARD_COL);
		}
	}
	move(SCORE_LINE, SCORE_COL);
	printw("Type '?' for help");
	refresh();
}
