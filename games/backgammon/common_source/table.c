/*	$OpenBSD: table.c,v 1.9 2015/11/30 08:19:25 tb Exp $	*/

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

#include <ctype.h>
#include "back.h"

const char   *const help2[] = {
	"   Enter moves as <s>-<f> or <s>/<r> where <s> is the starting",
	"position, <f> is the finishing position, and <r> is the roll.",
	"Remember, each die roll must be moved separately.",
	0
};

struct state {
	char    ch;
	int     fcode;
	int     newst;
};

static const struct state atmata[] = {

	{'R', 1, 0},	{'?', 7, 0},	{'Q', 0, -3},	{'B', 8, 25},
	{'9', 2, 25},	{'8', 2, 25},	{'7', 2, 25},	{'6', 2, 25},
	{'5', 2, 25},	{'4', 2, 25},	{'3', 2, 25},	{'2', 2, 19},
	{'1', 2, 15},	{'0', 2, 25},	{'.', 0, 0},	{'9', 2, 25},
	{'8', 2, 25},	{'7', 2, 25},	{'6', 2, 25},	{'5', 2, 25},

	{'4', 2, 25},	{'3', 2, 25},	{'2', 2, 25},	{'1', 2, 25},
	{'0', 2, 25},	{'/', 0, 32},	{'-', 0, 39},	{'.', 0, 0},
	{'/', 5, 32},	{' ', 6, 3},	{',', 6, 3},	{'\n', 0, -1},
	{'6', 3, 28},	{'5', 3, 28},	{'4', 3, 28},	{'3', 3, 28},
	{'2', 3, 28},	{'1', 3, 28},	{'.', 0, 0},	{'H', 9, 61},

	{'9', 4, 61},	{'8', 4, 61},	{'7', 4, 61},	{'6', 4, 61},
	{'5', 4, 61},	{'4', 4, 61},	{'3', 4, 61},	{'2', 4, 53},
	{'1', 4, 51},	{'0', 4, 61},	{'.', 0, 0},	{'9', 4, 61},
	{'8', 4, 61},	{'7', 4, 61},	{'6', 4, 61},	{'5', 4, 61},
	{'4', 4, 61},	{'3', 4, 61},	{'2', 4, 61},	{'1', 4, 61},

	{'0', 4, 61},	{' ', 6, 3},	{',', 6, 3},	{'-', 5, 39},
	{'\n', 0, -1},	{'.', 0, 0}
};

int
checkmove(int ist)
{
	int     curr, curc;
	int     j, n;
	int    c;

domove:
	getyx(stdscr, curr, curc);
	if (ist == 0)
		mvprintw(curr, 32, "Move:  ");
	ist = mvl = ncin = 0;
	for (j = 0; j < 5; j++)
		p[j] = g[j] = -1;

dochar:
	c = readc();

	if (c == 'S') {
		raflag = 0;
		save(1);
		move(cturn == -1 ? 18 : 19, 39);
		ist = -1;
		goto domove;
	}
	if ((c == KEY_BACKSPACE || c == 0177) && ncin > 0) {
		getyx(stdscr, curr, curc);
		move(curr, curc - 1);
		ncin--;
		n = rsetbrd();
		if (n == 0) {
			n = -1;
			refresh();
		}
		if ((ist = n) > 0)
			goto dochar;
		getyx(stdscr, curr, curc);
		move(curr, 39);
		clrtoeol();
		goto domove;
	} else if (c == KEY_DL && ncin > 0) {
		getyx(stdscr, curr, curc);
		move(curr, 39);
		clrtoeol();
		ist = -1;
		refresh();
		goto domove;
	}
	if (!isascii(c) || (ncin >= CIN_SIZE - 1)) {
		beep();
		goto domove;
	}
	n = dotable(c, ist);
	if (n >= 0) {
		cin[ncin++] = c;
		if (n > 2)
			if (c != '\n')
				addch(c);
		ist = n;
		if (n)
			goto dochar;
		else
			goto domove;
	}
	if (n == -1 && mvl >= mvlim)
		return(0);
	if (n == -1 && mvl < mvlim-1)
		return(-4);
	if (n == -6) {
		if (movokay(mvl + 1)) {
			moveplayers();
			movback(mvl + 1);
		} else
			move(cturn == -1 ? 18 : 19, ncin + 39);
		ist = n = rsetbrd();
		goto dochar;
	}
	if (n != -5)
		return(n);
	beep();
	goto dochar;
}

int
dotable(char c, int i)
{
	int     a;
	int     test;

	test = (c == 'R');

	while ((a = atmata[i].ch) != '.') {
		if (a == c || (test && a == '\n')) {
			switch (atmata[i].fcode) {
			case 1:
				wrboard();
				move(cturn == -1 ? 18 : 19, 0);
				proll();
				addstr("\t\t");
				break;

			case 2:
				if (p[mvl] == -1)
					p[mvl] = c - '0';
				else
					p[mvl] = p[mvl] * 10 + c - '0';
				break;

			case 3:
				if (g[mvl] != -1) {
					if (mvl < mvlim)
						mvl++;
					p[mvl] = p[mvl - 1];
				}
				g[mvl] = p[mvl] + cturn * (c - '0');
				if (g[mvl] < 0)
					g[mvl] = 0;
				if (g[mvl] > 25)
					g[mvl] = 25;
				break;

			case 4:
				if (g[mvl] == -1)
					g[mvl] = c - '0';
				else
					g[mvl] = g[mvl] * 10 + c - '0';
				break;

			case 5:
				if (mvl < mvlim)
					mvl++;
				p[mvl] = g[mvl - 1];
				break;

			case 6:
				if (mvl < mvlim)
					mvl++;
				break;

			case 7:
				move(20, 0);
				text(help2);
				move(cturn == -1 ? 18 : 19, 39);
				break;

			case 8:
				p[mvl] = bar;
				break;

			case 9:
				g[mvl] = home;
			}

			if (!test || a != '\n')
				return(atmata[i].newst);
			else
				return(-6);
		}
		i++;
	}
	return (-5);
}

int
rsetbrd(void)
{
	int     i, j, n;

	n = 0;
	mvl = 0;
	for (i = 0; i < 4; i++)
		p[i] = g[i] = -1;
	for (j = 0; j < ncin; j++)
		if ((n = dotable(cin[j], n)) < 0)
			return (n);
	return(n);
}
