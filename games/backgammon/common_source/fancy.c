/*	$OpenBSD: fancy.c,v 1.13 2015/11/30 08:19:25 tb Exp $	*/

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

#include <err.h>
#include "back.h"

int     oldb[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int     oldr, oldw;

void
fboard(void)
{
	int     i, j, k, l;

	/* could use box() or wborder() instead of the following */
	move(0, 0);		/* do top line */
	for (i = 0; i < 53; i++)
		addch('_');

	move(15, 0);		/* do bottom line */
	for (i = 0; i < 53; i++)
		addch('_');

	l = 1;			/* do vertical lines */
	for (i = 52; i > -1; i -= 28) {
		k = (l == 1 ? 1 : 15);
		mvaddch(k, i, '|');
		for (j = 0; j < 14; j++)
			mvaddch(k += l, i, '|');
		if (i == 24)
			i += 32;
		l = -l;		/* alternate directions */
	}

	/* label positions */
	for (i = 13; i < 19; i++)
		mvprintw(2, 1 + (i - 13) * 4, "%d", i);
	for (i = 19; i < 25; i++)
		mvprintw(2, 29 + (i - 19) * 4, "%d", i);
	for (i = 12; i > 6; i--)
		mvprintw(14, 1 + (12 - i) * 4, "%2d", i);
	for (i = 6; i > 0; i--)
		mvprintw(14, 30 + (6 - i) * 4, "%d", i);

	/* print positions 12-7 */
	for (i = 12; i > 6; i--)
		if (board[i])
			bsect(board[i], 13, 1 + 4 * (12 - i), -1);
	/* print red men on bar */
	if (board[0])
		bsect(board[0], 13, 25, -1);
	/* print positions 6-1 */
	for (i = 6; i > 0; i--)
		if (board[i])
			bsect(board[i], 13, 29 + 4 * (6 - i), -1);
	/* print white's home */
	l = (off[1] < 0 ? off[1] + 15 : off[1]);
	bsect(l, 3, 54, 1);

	mvaddstr(8, 25, "BAR");

	/* print positions 13-18 */
	for (i = 13; i < 19; i++)
		if (board[i])
			bsect(board[i], 3, 1 + 4 * (i - 13), 1);
	/* print white's men on bar */
	if (board[25])
		bsect(board[25], 3, 25, 1);
	/* print positions 19-24 */
	for (i = 19; i < 25; i++)
		if (board[i])
			bsect(board[i], 3, 29 + 4 * (i - 19), 1);
	/* print red's home */
	l = (off[0] < 0 ? off[0] + 15 : off[0]);
	bsect(-l, 13, 54, -1);

	for (i = 0; i < 26; i++)/* save board position for refresh later */
		oldb[i] = board[i];
	oldr = (off[1] < 0 ? off[1] + 15 : off[1]);
	oldw = -(off[0] < 0 ? off[0] + 15 : off[0]);
}

/*
 * bsect (b,rpos,cpos,cnext)
 *	Print the contents of a board position.  "b" has the value of the
 * position, "rpos" is the row to start printing, "cpos" is the column to
 * start printing, and "cnext" is positive if the position starts at the top
 * and negative if it starts at the bottom.  The value of "cpos" is checked
 * to see if the position is a player's home, since those are printed
 * differently.
 */
void
bsect(int b, int rpos, int cpos, int cnext)
{
	int     j;		/* index */
	int     n;		/* number of men on position */
	int     bct;		/* counter */
	int     k;		/* index */
	char    pc;		/* color of men on position */

	n = abs(b);		/* initialize n and pc */
	pc = (b > 0 ? 'r' : 'w');

	if (n < 6 && cpos < 54)	/* position cursor at start */
		move(rpos, cpos + 1);
	else
		move(rpos, cpos);

	for (j = 0; j < 5; j++) {	/* print position row by row */

		for (k = 0; k < 15; k += 5)	/* print men */
			if (n > j + k)
				addch(pc);

		if (j < 4) {	/* figure how far to back up for next row */
			if (n < 6) {	/* stop if none left */
				if (j + 1 == n)
					break;
				bct = 1;	/* single column */
			} else {
				if (n < 11) {	/* two columns */
					if (cpos >= 54) {	/* home pos */
						if (j + 5 >= n)
							bct = 1;
						else
							bct = 2;
					} else { 	/* not home */
						if (j + 6 >= n)
							bct = 1;
						else
							bct = 2;
					}
				} else {	/* three columns */
					if (j + 10 >= n)
						bct = 2;
					else
						bct = 3;
				}
			}
			getyx(stdscr, rpos, cpos);
			move(rpos + cnext, cpos - bct);
		}
	}
}

void
moveplayers(void)
{
	int i, r, c;

	getyx(stdscr, r, c);
	for (i = 12; i > 6; i--)/* fix positions 12-7 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 13, 1 + (12 - i) * 4, -1);
			oldb[i] = board[i];
		}
	if (board[0] != oldb[0]) {	/* fix red men on bar */
		fixpos(oldb[0], board[0], 13, 25, -1);
		oldb[0] = board[0];
	}
	for (i = 6; i > 0; i--)	/* fix positions 6-1 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 13, 29 + (6 - i) * 4, -1);
			oldb[i] = board[i];
		}
	i = -(off[0] < 0 ? off[0] + 15 : off[0]);	/* fix white's home */
	if (oldw != i) {
		fixpos(oldw, i, 13, 54, -1);
		oldw = i;
	}
	for (i = 13; i < 19; i++)	/* fix positions 13-18 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 3, 1 + (i - 13) * 4, 1);
			oldb[i] = board[i];
		}
	if (board[25] != oldb[25]) {	/* fix white men on bar */
		fixpos(oldb[25], board[25], 3, 25, 1);
		oldb[25] = board[25];
	}
	for (i = 19; i < 25; i++)	/* fix positions 19-24 */
		if (board[i] != oldb[i]) {
			fixpos(oldb[i], board[i], 3, 29 + (i - 19) * 4, 1);
			oldb[i] = board[i];
		}
	i = (off[1] < 0 ? off[1] + 15 : off[1]);	/* fix red's home */
	if (oldr != i) {
		fixpos(oldr, i, 3, 54, 1);
		oldr = i;
	}
	move(r, c);		/* return to saved position */
	refresh();
}
	

void
fixpos(int old, int new, int r, int c, int inc)
{
	int     o, n, nv;
	int     ov, nc;
	char    col;

	nc = 0;
	if (old * new >= 0) {
		ov = abs(old);
		nv = abs(new);
		col = (old + new > 0 ? 'r' : 'w');
		o = (ov - 1) / 5;
		n = (nv - 1) / 5;
		if (o == n) {
			if (o == 2)
				nc = c + 2;
			if (o == 1)
				nc = c < 54 ? c : c + 1;
			if (o == 0)
				nc = c < 54 ? c + 1 : c;
			if (ov > nv)
				fixcol(r + inc * (nv - n * 5), nc, abs(ov - nv), ' ', inc);
			else
				fixcol(r + inc * (ov - o * 5), nc, abs(ov - nv), col, inc);
			return;
		} else {
			if (c < 54) {
				if (o + n == 1) {
					if (n) {
						fixcol(r, c, abs(nv - 5), col, inc);
						if (ov != 5)
							fixcol(r+inc*ov, c+1, abs(ov-5), col, inc);
					} else  {
						fixcol(r, c, abs(ov - 5), ' ', inc);
						if (nv != 5)
							fixcol(r+inc*nv, c+1, abs(nv-5), ' ', inc);
					}
					return;
				}
				if (n == 2) {
					if (ov != 10)
						fixcol(r+inc*(ov-5), c, abs(ov-10), col, inc);
					fixcol(r, c + 2, abs(nv - 10), col, inc);
				} else {
					if (nv != 10)
						fixcol(r+inc*(nv-5), c, abs(nv-10), ' ', inc);
					fixcol(r, c + 2, abs(ov - 10), ' ', inc);
				}
				return;
			}
			if (n > o) {
				fixcol(r+inc*(ov%5), c+o, abs(5*n-ov), col, inc);
				if (nv != 5 * n)
					fixcol(r, c+n, abs(5*n-nv), col, inc);
			} else {
				fixcol(r+inc*(nv%5), c+n, abs(5*n-nv), ' ', inc);
				if (ov != 5 * o)
					fixcol(r, c+o, abs(5*o-ov), ' ', inc);
			}
			return;
		}
	}
	nv = abs(new);
	fixcol(r, c + 1, nv, new > 0 ? 'r' : 'w', inc);
	if (abs(old) <= abs(new))
		return;
	fixcol(r + inc * new, c + 1, abs(old + new), ' ', inc);
}

void
fixcol(int r, int c, int l, int ch, int inc)
{
	int     i;

	mvaddch(r, c, ch);
	for (i = 1; i < l; i++) {
		r += inc;
		mvaddch(r, c, ch);
	}
}


void
initcurses(void)
{
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	nl();
	clear();

	if ((LINES < 24) || (COLS < 80)) {
		endwin();
		errx(1, "screen must be at least 24x80.");
	}
}
