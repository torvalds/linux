/*	$OpenBSD: subs.c,v 1.22 2015/12/02 20:05:01 tb Exp $	*/

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

#include "back.h"

__dead void	usage(void);

int     buffnum;
char    outbuff[BUFSIZ];

static const char plred[] = "Player is red, computer is white.";
static const char plwhite[] = "Player is white, computer is red.";
static const char nocomp[] = "(No computer play.)";

void
errexit(const char *s)
{
	write(STDERR_FILENO, "\n", 1);
	perror(s);
	getout(0);
}

int
readc(void)
{
	int    c;

	clrtoeol();
	refresh();
	c = getch();
	if (c == '\004' || c == ERR)	/* ^D or failure	*/
		getout(0);
	if (c == '\033' || c == '\015')
		return('\n');
	if (cflag)
		return(c);
	if (c == '\014')
		return('R');
	if (c >= 'a' && c <= 'z')
		return(c & 0137);	/* upper case */
	return(c);
}

void
proll(void)
{
	if (d0)
		swap;
	if (cturn == 1)
		printw("Red's roll:  ");
	else
		printw("White's roll:  ");
	printw("%d,%d", D0, D1);
	clrtoeol();
}

void
gwrite(void)
{
	int     r, c;

	getyx(stdscr, r, c);
	move(16, 0);
	if (gvalue > 1) {
		printw("Game value:  %d.  ", gvalue);
		if (dlast == -1)
			addstr(color[0]);
		else
			addstr(color[1]);
		addstr(" doubled last.");
	} else {
		if (!dflag)
			printw("[No doubling.]  ");
		switch (pnum) {
		case -1:	/* player is red */
			addstr(plred);
			break;
		case 0:	/* player is both colors */
			addstr(nocomp);
			break;
		case 1:	/* player is white */
			addstr(plwhite);
		}
	}
	if (rscore || wscore) {
		addstr("  ");
		wrscore();
	}
	clrtoeol();
	move(r, c);
}

int
quit(void)
{
	move(20, 0);
	clrtobot();
	addstr("Are you sure you want to quit?");
	if (yorn(0)) {
		if (rfl) {
			addstr("Would you like to save this game?");
			if (yorn(0))
				save(0);
		}
		cturn = 0;
		return(1);
	}
	return(0);
}

int
yorn(char special)
{
	char    c;
	int     i;

	i = 1;
	while ((c = readc()) != 'Y' && c != 'N') {
		if (special && c == special)
			return(2);
		if (i) {
			if (special)
				printw("  (Y, N, or %c)", special);
			else
				printw("  (Y or N)");
			i = 0;
		} else
			beep();
	}
	if (c == 'Y')
		addstr("  Yes.\n");
	else
		addstr("  No.\n");
	refresh();
	return(c == 'Y');
}

void
wrhit(int i)
{
	printw("Blot hit on %d.\n", i);
}

void
nexturn(void)
{
	int     c;

	cturn = -cturn;
	c = cturn / abs(cturn);
	home = bar;
	bar = 25 - bar;
	offptr += c;
	offopp -= c;
	inptr += c;
	inopp -= c;
	Colorptr += c;
	colorptr += c;
}

void
getarg(int argc, char **argv)
{
	int     ch;

	while ((ch = getopt(argc, argv, "bdnrs:w")) != -1)
		switch(ch) {
		case 'n':	/* don't ask if rules or instructions needed */
			if (rflag)
				break;
			aflag = 0;
			break;

		case 'b':	/* player is both red and white */
			if (rflag)
				break;
			pnum = 0;
			aflag = 0;
			break;

		case 'r':	/* player is red */
			if (rflag)
				break;
			pnum = -1;
			aflag = 0;
			break;

		case 'w':	/* player is white */
			if (rflag)
				break;
			pnum = 1;
			aflag = 0;
			break;

		case 's':	/* restore saved game */
			recover(optarg);
			break;

		case 'd':	/* disable doubling */
			dflag = 0;
			aflag = 0;
			break;

		default:	/* print cmdline options */
			usage();
	} /* end switch */
}

void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-bdnrw] [-s file]\n", __progname);
	exit(1);
}

void
init(void)
{
	int     i;

	for (i = 0; i < 26;)
		board[i++] = 0;
	board[1] = 2;
	board[6] = board[13] = -5;
	board[8] = -3;
	board[12] = board[19] = 5;
	board[17] = 3;
	board[24] = -2;
	off[0] = off[1] = -15;
	in[0] = in[1] = 5;
	gvalue = 1;
	dlast = 0;
}

void
wrscore(void)
{
	printw("Score:  %s %d, %s %d", color[1], rscore, color[0], wscore);
}


void
getout(int dummy)
{
	/* go to bottom of screen */
	move(23, 0);
	clrtoeol();

	endwin();
	exit(0);
}

void
roll(void)
{
	char    c;
	int     row;
	int     col;

	if (iroll) {
		getyx(stdscr, row, col);
		mvprintw(17, 0, "ROLL: ");
		c = readc();
		if (c != '\n') {
			while (c < '1' || c > '6')
				c = readc();
			D0 = c - '0';
			printw(" %c", c);
			c = readc();
			while (c < '1' || c > '6')
				c = readc();
			D1 = c - '0';
			printw(" %c", c);
			move(17, 0);
			clrtoeol();
			move(row, col);
			return;
		}
		move(17, 0);
		clrtoeol();
		move(row, col);
	}
	D0 = rnum(6) + 1;
	D1 = rnum(6) + 1;
	d0 = 0;
}
