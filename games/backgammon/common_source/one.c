/*	$OpenBSD: one.c,v 1.7 2015/11/30 08:19:25 tb Exp $	*/

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

int
makmove(int i)
{
	int     n, d;
	int     max;

	d = d0;
	n = abs(g[i] - p[i]);
	max = (*offptr < 0 ? 7 : last());
	if (board[p[i]] * cturn <= 0)
		return(checkd(d) + 2);
	if (g[i] != home && board[g[i]] * cturn < -1)
		return(checkd(d) + 3);
	if (i || D0 == D1) {
		if (n == max ? D1 < n : D1 != n)
			return(checkd(d) + 1);
	} else {
		if (n == max ? D0 < n && D1 < n : D0 != n && D1 != n)
			return(checkd(d) + 1);
		if (n == max ? D0 < n : D0 != n)
			swap;
	}
	if (g[i] == home && *offptr < 0)
		return(checkd(d) + 4);
	h[i] = 0;
	board[p[i]] -= cturn;
	if (g[i] != home) {
		if (board[g[i]] == -cturn) {
			board[home] -= cturn;
			board[g[i]] = 0;
			h[i] = 1;
			if (abs(bar - g[i]) < 7) {
				(*inopp)--;
				if (*offopp >= 0)
					*offopp -= 15;
			}
		}
		board[g[i]] += cturn;
		if (abs(home - g[i]) < 7 && abs(home - p[i]) > 6) {
			(*inptr)++;
			if (*inptr + *offptr == 0)
				*offptr += 15;
		}
	} else {
		(*offptr)++;
		(*inptr)--;
	}
	return(0);
}

void
moverr(int i)
{
	int     j;

	mvprintw(20, 0, "Error:  ");
	for (j = 0; j <= i; j++) {
		printw("%d-%d", p[j], g[j]);
		if (j < i)
			addch(',');
	}
	addstr("... ");
	movback(i);
}

int
checkd(int d)
{
	if (d0 != d)
		swap;
	return(0);
}

int
last(void)
{
	int     i;

	for (i = home - 6 * cturn; i != home; i += cturn)
		if (board[i] * cturn > 0)
			return(abs(home - i));
	return(-1);
}

void
movback(int i)
{
	int     j;

	for (j = i - 1; j >= 0; j--)
		backone(j);
}

void
backone(int i)
{
	board[p[i]] += cturn;
	if (g[i] != home) {
		board[g[i]] -= cturn;
		if (abs(g[i] - home) < 7 && abs(p[i] - home) > 6) {
			(*inptr)--;
			if (*inptr + *offptr < 15 && *offptr >= 0)
				*offptr -= 15;
		}
	} else {
		(*offptr)--;
		(*inptr)++;
	}
	if (h[i]) {
		board[home] += cturn;
		board[g[i]] = -cturn;
		if (abs(bar - g[i]) < 7) {
			(*inopp)++;
			if (*inopp + *offopp == 0)
				*offopp += 15;
		}
	}
}
