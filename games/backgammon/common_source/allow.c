/*	$OpenBSD: allow.c,v 1.6 2015/11/30 08:19:25 tb Exp $	*/

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
movallow(void)
{
	int     i, m, iold, r;

	if (d0)
		swap;
	m = (D0 == D1 ? 4 : 2);
	for (i = 0; i < 4; i++)
		p[i] = bar;
	i = iold = 0;
	while (i < m) {
		if (*offptr == 15)
			break;
		h[i] = 0;
		if (board[bar]) {
			if (i == 1 || m == 4)
				g[i] = bar + cturn * D1;
			else
				g[i] = bar + cturn * D0;
			if ((r = makmove(i))) {
				if (d0 || m == 4)
					break;
				swap;
				movback(i);
				if (i > iold)
					iold = i;
				for (i = 0; i < 4; i++)
					p[i] = bar;
				i = 0;
			} else
				i++;
			continue;
		}
		if ((p[i] += cturn) == home) {
			if (i > iold)
				iold = i;
			if (m == 2 && i) {
				movback(i);
				p[i--] = bar;
				if (p[i] != bar)
					continue;
				else
					break;
			}
			if (d0 || m == 4)
				break;
			swap;
			movback(i);
			for (i = 0; i < 4; i++)
				p[i] = bar;
			i = 0;
			continue;
		}
		if (i == 1 || m == 4)
			g[i] = p[i] + cturn * D1;
		else
			g[i] = p[i] + cturn * D0;
		if (g[i] * cturn > home) {
			if (*offptr >= 0)
				g[i] = home;
			else
				continue;
		}
		if (board[p[i]] * cturn > 0 && (r = makmove(i)) == 0)
			i++;
	}
	movback(i);
	return (iold > i ? iold : i);
}
