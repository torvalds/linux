/*	$OpenBSD: kill.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: kill.c,v 1.3 1995/04/22 10:59:06 cgd Exp $	*/

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

#include <stdio.h>

#include "trek.h"

/*
**  KILL KILL KILL !!!
**
**	This file handles the killing off of almost anything.
*/

/*
**  Handle a Klingon's death
**
**	The Klingon at the sector given by the parameters is killed
**	and removed from the Klingon list.  Notice that it is not
**	removed from the event list; this is done later, when the
**	the event is to be caught.  Also, the time left is recomputed,
**	and the game is won if that was the last klingon.
*/

void
killk(int ix, int iy)
{
	int		i;

	printf("   *** Klingon at %d,%d destroyed ***\n", ix, iy);

	/* remove the scoundrel */
	Now.klings -= 1;
	Sect[ix][iy] = EMPTY;
	Quad[Ship.quadx][Ship.quady].klings -= 1;
	/* %%% IS THIS SAFE???? %%% */
	Quad[Ship.quadx][Ship.quady].scanned -= 100;
	Game.killk += 1;

	/* find the Klingon in the Klingon list */
	for (i = 0; i < Etc.nkling; i++)
		if (ix == Etc.klingon[i].x && iy == Etc.klingon[i].y)
		{
			/* purge him from the list */
			Etc.nkling -= 1;
			for (; i < Etc.nkling; i++)
				Etc.klingon[i] = Etc.klingon[i + 1];
			break;
		}

	/* find out if that was the last one */
	if (Now.klings <= 0)
		win();

	/* recompute time left */
	Now.time = Now.resource / Now.klings;
	return;
}


/*
**  handle a starbase's death
*/

void
killb(int qx, int qy)
{
	struct quad	*q;
	struct xy	*b;

	q = &Quad[qx][qy];

	if (q->bases <= 0)
		return;
	if (!damaged(SSRADIO))
	{
		/* then update starchart */
		if (q->scanned < 1000)
			q->scanned -= 10;
		else
			if (q->scanned > 1000)
				q->scanned = -1;
	}
	q->bases = 0;
	Now.bases -= 1;
	for (b = Now.base; ; b++)
		if (qx == b->x && qy == b->y)
			break;
	*b = Now.base[Now.bases];
	if (qx == Ship.quadx && qy == Ship.quady)
	{
		Sect[Etc.starbase.x][Etc.starbase.y] = EMPTY;
		if (Ship.cond == DOCKED)
			undock(0);
		printf("Starbase at %d,%d destroyed\n", Etc.starbase.x, Etc.starbase.y);
	}
	else
	{
		if (!damaged(SSRADIO))
		{
			printf("Uhura: Starfleet command reports that the starbase in\n");
			printf("   quadrant %d,%d has been destroyed\n", qx, qy);
		}
		else
			schedule(E_KATSB | E_GHOST, 1e50, qx, qy, 0);
	}
}


/**
 **	kill an inhabited starsystem
 **/

/* 
 * x, y: quad coords if f == 0, else sector coords
 * f != 0 -- this quad;  f < 0 -- Enterprise's fault
 */
void
kills(int x, int y, int f)
{
	struct quad	*q;
	struct event	*e;
	const char	*name;

	if (f)
	{
		/* current quadrant */
		q = &Quad[Ship.quadx][Ship.quady];
		Sect[x][y] = EMPTY;
		name = systemname(q);
		if (name == 0)
			return;
		printf("Inhabited starsystem %s at %d,%d destroyed\n",
			name, x, y);
		if (f < 0)
			Game.killinhab += 1;
	}
	else
	{
		/* different quadrant */
		q = &Quad[x][y];
	}
	if (q->qsystemname & Q_DISTRESSED)
	{
		/* distressed starsystem */
		e = &Event[q->qsystemname & Q_SYSTEM];
		printf("Distress call for %s invalidated\n",
			Systemname[e->systemname]);
		unschedule(e);
	}
	q->qsystemname = 0;
	q->stars -= 1;
}


/**
 **	"kill" a distress call
 **/

/* 
 * x, y: quadrant coordinates
 * f: set if user is to be informed
 */
void
killd(int x, int y, int f)
{
	struct event	*e;
	int		i;
	struct quad	*q;

	q = &Quad[x][y];
	for (i = 0; i < MAXEVENTS; i++)
	{
		e = &Event[i];
		if (e->x != x || e->y != y)
			continue;
		switch (e->evcode)
		{
		  case E_KDESB:
			if (f)
			{
				printf("Distress call for starbase in %d,%d nullified\n",
					x, y);
				unschedule(e);
			}
			break;

		  case E_ENSLV:
		  case E_REPRO:
			if (f)
			{
				printf("Distress call for %s in quadrant %d,%d nullified\n",
					Systemname[e->systemname], x, y);
				q->qsystemname = e->systemname;
				unschedule(e);
			}
			else
			{
				e->evcode |= E_GHOST;
			}
		}
	}
}
