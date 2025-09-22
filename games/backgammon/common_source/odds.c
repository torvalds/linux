/*	$OpenBSD: odds.c,v 1.7 2016/01/08 13:40:05 tb Exp $	*/

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
odds(int r1, int r2, int val)
{
	int     i, j;

	if (r1 == 0) {
		for (i = 0; i < 6; i++)
			for (j = 0; j < 6; j++)
				table[i][j] = 0;
		return;
	} else {
		r1--;
		if (r2-- == 0) {
			for (i = 0; i < 6; i++) {
				table[i][r1] += val;
				table[r1][i] += val;
			}
		} else {
			table[r2][r1] += val;
			table[r1][r2] += val;
		}
	}
}

int
count(void)
{
	int     i, j, total;

	total = 0;
	for (i = 0; i < 6; i++)
		for (j = 0; j < 6; j++)
			total += table[i][j];
	return(total);
}

int
canhit(int i, int c)
{
	int     j, k, b;
	int     a, diff, place, addon, menstuck;

	if (c == 0)
		odds(0, 0, 0);
	if (board[i] > 0) {
		a = -1;
		b = 25;
	} else {
		a = 1;
		b = 0;
	}
	place = abs(25 - b - i);
	menstuck = abs(board[b]);
	for (j = b; j != i; j += a) {
		if (board[j] * a > 0) {
			diff = abs(j - i);
			addon = place + ((board[j] * a > 2 || j == b) ? 5 : 0);
			if ((j == b && menstuck == 1) ||
			    (j != b && menstuck == 0))
				for (k = 1; k < diff; k++)
					if (k < 7 && diff - k < 7 &&
					    (board[i + a * k] * a >= 0 ||
					     board[i + a * (diff - k)] >= 0))
						odds(k, diff - k, addon);
			if ((j == b || menstuck < 2) && diff < 7)
				odds(diff, 0, addon);
		}
		if (j == b && menstuck > 1)
			break;
	}
	return(count());
}
