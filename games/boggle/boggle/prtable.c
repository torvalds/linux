/*	$OpenBSD: prtable.c,v 1.11 2008/08/04 18:42:09 millert Exp $	*/
/*	$NetBSD: prtable.c,v 1.2 1995/03/21 12:14:42 cgd Exp $	*/

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
 *
 *	@(#)prtable.c	8.1 (Berkeley) 6/11/93
 */

#include <curses.h>

#include "extern.h"

#define NCOLS	5

static int	get_maxlen(char **, int, int (*)(char **, int));

extern int	lastline, LIST_LINE, LIST_COL;

/*
 * Routine to print a table
 * Modified from 'ls.c' mods (BJB/83)
 * Arguments:
 *	base	- address of first entry
 *	num     - number of entries
 *	d_cols  - number of columns to use if > 0, "best" size if == 0
 *	width	- max line width if not zero
 *	prentry - address of the routine to call to print the string
 *	length  - address of the routine to call to determine the length
 *		  of string to be printed 
 *
 * prtable and length are called with the address of the base and
 * an index
 */
void
prtable(char **base, int num, int d_cols, int width, 
        void (*prentry)(char **, int), int (*length)(char **, int))
{
	int c, j;
	int a, b, cols, loc, maxlen, nrows, z;
	int col, row;

	if (num == 0)
		return;
	maxlen = get_maxlen(base, num, length) + 1;
	if (d_cols > 0)
		cols = d_cols;
	else
		cols = width / maxlen;
	if (cols == 0)
		cols = NCOLS;
	nrows = (num - 1) / cols + 1;
	for (a = 1; a <= nrows; a++) {
		b = c = z = loc = 0;
		for (j = 0; j < num; j++) {
			c++;
			if (c >= a + b)
				break;
		}
		while (j < num) {
			(*prentry)(base, j);
			loc += (*length)(base, j);
			z++;
			b += nrows;
			for (j++; j < num; j++) {
				c++;
				if (c >= a + b)
					break;
			}
			if (j < num) {
				while (loc < z * maxlen) {
					addch(' ');
					loc++;
				}
			}
		}
		getyx(stdscr, row, col);
		move(row + 1, 0);
		if (row + 1 == lastline && a != nrows) {
			attron(A_REVERSE);
			printw("--More--");
			attroff(A_REVERSE);
			do {
			    j = inputch();
			} while (j != ' ' && j != 'q' && j != 'Q');
			if (j == 'q' || j == 'Q') {
				move(row + 1, 0);
				wclrtoeol(stdscr);
				break;
			}
			move(LIST_LINE, LIST_COL);
			wclrtobot(stdscr);
		}
	}
	refresh();
}

static int
get_maxlen(char **base, int num, int (*length)(char **, int))
{
	int i, len, max;

	max = (*length)(base, 0);
	for (i = 0; i < num; i++) {
		if ((len = (*length)(base, i)) > max)
			max = len;
	}
	return(max);
}
