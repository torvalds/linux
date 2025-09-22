/*	$OpenBSD: move_robs.c,v 1.9 2016/01/04 17:33:24 mestre Exp $	*/
/*	$NetBSD: move_robs.c,v 1.3 1995/04/22 10:08:59 cgd Exp $	*/

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

#include "robots.h"

/*
 * move_robots:
 *	Move the robots around
 */
void
move_robots(void)
{
	COORD	*rp;

#ifdef DEBUG
	move(Min.y, Min.x);
	addch(inch());
	move(Max.y, Max.x);
	addch(inch());
#endif /* DEBUG */
	for (rp = Robots; rp < &Robots[MAXROBOTS]; rp++) {
		if (rp->y < 0)
			continue;
		mvaddch(rp->y, rp->x, ' ');
		Field[rp->y][rp->x]--;
		rp->y += sign(My_pos.y - rp->y);
		rp->x += sign(My_pos.x - rp->x);
		if (rp->y <= 0)
			rp->y = 0;
		else if (rp->y >= Y_FIELDSIZE)
			rp->y = Y_FIELDSIZE - 1;
		if (rp->x <= 0)
			rp->x = 0;
		else if (rp->x >= X_FIELDSIZE)
			rp->x = X_FIELDSIZE - 1;
		Field[rp->y][rp->x]++;
	}

	Min.y = Y_FIELDSIZE;
	Min.x = X_FIELDSIZE;
	Max.y = 0;
	Max.x = 0;
	for (rp = Robots; rp < &Robots[MAXROBOTS]; rp++)
		if (rp->y < 0)
			continue;
		else if (rp->y == My_pos.y && rp->x == My_pos.x)
			Dead = TRUE;
		else if (Field[rp->y][rp->x] > 1) {
			mvaddch(rp->y, rp->x, HEAP);
			rp->y = -1;
			Num_robots--;
			if (Waiting)
				Wait_bonus++;
			add_score(ROB_SCORE);
		}
		else {
			mvaddch(rp->y, rp->x, ROBOT);
			if (rp->y < Min.y)
				Min.y = rp->y;
			if (rp->x < Min.x)
				Min.x = rp->x;
			if (rp->y > Max.y)
				Max.y = rp->y;
			if (rp->x > Max.x)
				Max.x = rp->x;
		}

#ifdef DEBUG
	standout();
	move(Min.y, Min.x);
	addch(inch());
	move(Max.y, Max.x);
	addch(inch());
	standend();
#endif /* DEBUG */
}

/*
 * add_score:
 *	Add a score to the overall point total
 */
void
add_score(int add)
{
	Score += add;
	move(Y_SCORE, X_SCORE);
	printw("%d", Score);
}

/*
 * sign:
 *	Return the sign of the number
 */
int
sign(int n)
{
	if (n < 0)
		return -1;
	else if (n > 0)
		return 1;
	else
		return 0;
}
