/*	$OpenBSD: tutor.c,v 1.10 2023/05/05 10:26:50 tb Exp $	*/

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
#include "tutor.h"

static const char better[] = "That is a legal move, but there is a better one.\n";

__dead void
tutor(void)
{
	int     i, j, k;
	int     wrongans;

	wrongans = 0;
	i = 0;
	begscr = 18;
	cturn = -1;
	home = 0;
	bar = 25;
	inptr = &in[0];
	inopp = &in[1];
	offptr = &off[0];
	offopp = &off[1];
	Colorptr = &color[0];
	colorptr = &color[2];
	colen = 5;
	wrboard();

	while (1) {
		if (!brdeq(test[i].brd, board)) {
			wrongans++;
			move(18, 0);
			if (wrongans >= 3) {
				wrongans = 0;
				text(*test[i].ans);
				memcpy(board,test[i].brd,26*sizeof(int));
				/* and have to fix *inptr, *offptr; player is red (+ve) */
				k = 0;
				for (j = 19; j < 26; j++)
					k += (board[j] > 0 ? board[j] : 0);
				*inopp = k;
				for (j = 0; j < 19; j++)
					k += (board[j] > 0 ? board[j] : 0);
				*offopp = k - 30;  /* -15 at start */
				moveplayers();
				clrest();
			} else {
				addstr(better);
				nexturn();
				movback(mvlim);
				moveplayers();
				clrest();
				getyx(stdscr, j, k);
				if (j == 19) {
					proll();
					addch('\t');
				} else
					move(j > 19 ? j - 2 : j + 4, 25);
				getmove();
				if (cturn == 0)
					leave();
				continue;
			}
		} else
			wrongans = 0;
		move(18, 0);
		text(*test[i].com);
		move(19, 0);
		if (i == maxmoves)
			break;
		D0 = test[i].roll1;
		D1 = test[i].roll2;
		d0 = 0;
		mvlim = 0;
		for (j = 0; j < 4; j++) {
			if (test[i].mp[j] == test[i].mg[j])
				break;
			p[j] = test[i].mp[j];
			g[j] = test[i].mg[j];
			mvlim++;
		}
		if (mvlim)
			for (j = 0; j < mvlim; j++)
				if (makmove(j))
					addstr("AARGH!!!\n");
		moveplayers();
		nexturn();
		D0 = test[i].new1;
		D1 = test[i].new2;
		d0 = 0;
		i++;
		mvlim = movallow();
		if (mvlim) {
			clrest();
			proll();
			addch('\t');
			getmove();
			moveplayers();
			if (cturn == 0)
				leave();
		}
	}
	leave();
}

void
clrest(void)
{
	int     r, c, j;

	getyx(stdscr, r, c);
	for (j = r + 1; j < 24; j++) {
		move(j, 0);
		clrtoeol();
	}
	move(r, c);
}

int
brdeq(const int *b1, const int *b2)
{
	const int    *e;

	e = b1 + 26;
	while (b1 < e)
		if (*b1++ != *b2++)
			return(0);
	return(1);
}
