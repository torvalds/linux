/*	$OpenBSD: check.c,v 1.7 2015/11/30 08:19:25 tb Exp $	*/

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

void
getmove(void)
{
	int     i, c;

	c = 0;
	for (;;) {
		i = checkmove(c);

		switch (i) {
		case -1:
			if (movokay(mvlim)) {
				move(20, 0);
				for (i = 0; i < mvlim; i++)
					if (h[i])
						wrhit(g[i]);
				nexturn();
				if (*offopp == 15)
					cturn *= -2;
				return;
			}
		case -4:
		case 0:
			refresh();
			if (i != 0 && i != -4)
				break;
			mvaddstr(20, 0, *Colorptr);
			if (i == -4)
				addstr(" must make ");
			else
				addstr(" can only make ");
			printw("%d move%s.\n", mvlim, mvlim > 1 ? "s":"");
			break;

		case -3:
			if (quit())
				return;
		}

		move(cturn == -1 ? 18 : 19, 39);
		clrtoeol();
		c = -1;
	}
}

int
movokay(int mv)
{
	int     i, m;

	if (d0)
		swap;

	for (i = 0; i < mv; i++) {
		if (p[i] == g[i]) {
			moverr(i);
			mvaddstr(20, 0, "Attempt to move to same location.\n");
			return(0);
		}
		if (cturn * (g[i] - p[i]) < 0) {
			moverr(i);
			mvaddstr(20, 0, "Backwards move.\n");
			return(0);
		}
		if (abs(board[bar]) && p[i] != bar) {
			moverr(i);
			mvaddstr(20, 0, "Men still on bar.\n");
			return(0);
		}
		if ((m = makmove(i))) {
			moverr(i);
			switch (m) {
			case 1:
				addstr("Move not rolled.\n");
				break;

			case 2:
				addstr("Bad starting position.\n");
				break;

			case 3:
				addstr("Destination occupied.\n");
				break;

			case 4:
				addstr("Can't remove men yet.\n");
			}
			return(0);
		}
	}
	return(1);
}
