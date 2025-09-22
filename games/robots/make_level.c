/*	$OpenBSD: make_level.c,v 1.8 2016/01/04 17:33:24 mestre Exp $	*/
/*	$NetBSD: make_level.c,v 1.3 1995/04/22 10:08:56 cgd Exp $	*/

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

#include <string.h>

#include "robots.h"

/*
 * make_level:
 *	Make the current level
 */
void
make_level(void)
{
	int	i;
	COORD	*cp;
	int	x;

	reset_count();
	for (i = 1; i < Y_FIELDSIZE; i++)
		for (x = 1; x < X_FIELDSIZE; x++)
			if (Field[i][x] != 0)
				mvaddch(i, x, ' ');
	if (My_pos.y > 0)
		mvaddch(My_pos.y, My_pos.x, ' ');

	Waiting = FALSE;
	Wait_bonus = 0;
	leaveok(stdscr, FALSE);
	for (cp = Robots; cp < &Robots[MAXROBOTS]; cp++)
		cp->y = -1;
	My_pos.y = -1;

	memset(Field, 0, sizeof Field);
	Min.y = Y_FIELDSIZE;
	Min.x = X_FIELDSIZE;
	Max.y = 0;
	Max.x = 0;
	if ((i = Level * 10) > MAXROBOTS)
		i = MAXROBOTS;
	Num_robots = i;
	while (i-- > 0) {
		cp = rnd_pos();
		Robots[i] = *cp;
		Field[cp->y][cp->x]++;
		if (cp->y < Min.y)
			Min.y = cp->y;
		if (cp->x < Min.x)
			Min.x = cp->x;
		if (cp->y > Max.y)
			Max.y = cp->y;
		if (cp->x > Max.x)
			Max.x = cp->x;
	}
	My_pos = *rnd_pos();
	refresh();
}
