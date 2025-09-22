/*	$OpenBSD: computer.c,v 1.12 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: computer.c,v 1.4 1995/04/24 12:25:51 cgd Exp $	*/

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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "getpar.h"
#include "trek.h"

/*
**  On-Board Computer
**
**	A computer request is fetched from the captain.  The requests
**	are:
**
**	chart -- print a star chart of the known galaxy.  This includes
**		every quadrant that has ever had a long range or
**		a short range scan done of it, plus the location of
**		all starbases.  This is of course updated by any sub-
**		space radio broadcasts (unless the radio is out).
**		The format is the same as that of a long range scan
**		except that ".1." indicates that a starbase exists
**		but we know nothing else.
**
**	trajectory -- gives the course and distance to every know
**		Klingon in the quadrant.  Obviously this fails if the
**		short range scanners are out.
**
**	course -- gives a course computation from whereever you are
**		to any specified location.  If the course begins
**		with a slash, the current quadrant is taken.
**		Otherwise the input is quadrant and sector coordi-
**		nates of the target sector.
**
**	move -- identical to course, except that the move is performed.
**
**	score -- prints out the current score.
**
**	pheff -- "PHaser EFFectiveness" at a given distance.  Tells
**		you how much stuff you need to make it work.
**
**	warpcost -- Gives you the cost in time and units to move for
**		a given distance under a given warp speed.
**
**	impcost -- Same for the impulse engines.
**
**	distresslist -- Gives a list of the currently known starsystems
**		or starbases which are distressed, together with their
**		quadrant coordinates.
**
**	If a command is terminated with a semicolon, you remain in
**	the computer; otherwise, you escape immediately to the main
**	command processor.
*/

struct cvntab	Cputab[] =
{
	{ "ch",		"art",			(cmdfun)1,		0 },
	{ "t",		"rajectory",		(cmdfun)2,		0 },
	{ "c",		"ourse",		(cmdfun)3,		0 },
	{ "m",		"ove",			(cmdfun)3,		1 },
	{ "s",		"core",			(cmdfun)4,		0 },
	{ "p",		"heff",			(cmdfun)5,		0 },
	{ "w",		"arpcost",		(cmdfun)6,		0 },
	{ "i",		"mpcost",		(cmdfun)7,		0 },
	{ "d",		"istresslist",		(cmdfun)8,		0 },
	{ NULL,		NULL,			NULL,			0 }
};

static int kalc(int, int, int, int, double *);
static void prkalc(int, double);

void
computer(int v)
{
	int			ix, iy;
	int			i, j;
	int			tqx, tqy;
	const struct cvntab	*r;
	int			cost;
	int			course;
	double			dist, time;
	double			warpfact;
	struct quad		*q;
	struct event		*e;

	if (check_out(COMPUTER))
		return;
	while (1)
	{
		r = getcodpar("\nRequest", Cputab);
		switch ((long)r->value)
		{

		  case 1:			/* star chart */
			printf("Computer record of galaxy for all long range sensor scans\n\n");
			printf("  ");
			/* print top header */
			for (i = 0; i < NQUADS; i++)
				printf("-%d- ", i);
			printf("\n");
			for (i = 0; i < NQUADS; i++)
			{
				printf("%d ", i);
				for (j = 0; j < NQUADS; j++)
				{
					if (i == Ship.quadx && j == Ship.quady)
					{
						printf("$$$ ");
						continue;
					}
					q = &Quad[i][j];
					/* 1000 or 1001 is special case */
					if (q->scanned >= 1000)
					{
						if (q->scanned > 1000)
							printf(".1. ");
						else
							printf("/// ");
					}
					else
						if (q->scanned < 0)
							printf("... ");
						else
							printf("%3d ", q->scanned);
				}
				printf("%d\n", i);
			}
			printf("  ");
			/* print bottom footer */
			for (i = 0; i < NQUADS; i++)
				printf("-%d- ", i);
			printf("\n");
			break;

		  case 2:			/* trajectory */
			if (check_out(SRSCAN))
			{
				break;
			}
			if (Etc.nkling <= 0)
			{
				printf("No Klingons in this quadrant\n");
				break;
			}
			/* for each Klingon, give the course & distance */
			for (i = 0; i < Etc.nkling; i++)
			{
				printf("Klingon at %d,%d", Etc.klingon[i].x, Etc.klingon[i].y);
				course = kalc(Ship.quadx, Ship.quady, Etc.klingon[i].x, Etc.klingon[i].y, &dist);
				prkalc(course, dist);
			}
			break;

		  case 3:			/* course calculation */
			if (readdelim('/'))
			{
				tqx = Ship.quadx;
				tqy = Ship.quady;
			}
			else
			{
				ix = getintpar("Quadrant");
				if (ix < 0 || ix >= NSECTS)
					break;
				iy = getintpar("q-y");
				if (iy < 0 || iy >= NSECTS)
					break;
				tqx = ix;
				tqy = iy;
			}
			ix = getintpar("Sector");
			if (ix < 0 || ix >= NSECTS)
				break;
			iy = getintpar("s-y");
			if (iy < 0 || iy >= NSECTS)
				break;
			course = kalc(tqx, tqy, ix, iy, &dist);
			if (r->value2)
			{
				warp(-1, course, dist);
				break;
			}
			printf("%d,%d/%d,%d to %d,%d/%d,%d",
				Ship.quadx, Ship.quady, Ship.sectx, Ship.secty, tqx, tqy, ix, iy);
			prkalc(course, dist);
			break;

		  case 4:			/* score */
			score();
			break;

		  case 5:			/* phaser effectiveness */
			dist = getfltpar("range");
			if (dist < 0.0)
				break;
			dist *= 10.0;
			cost = pow(0.90, dist) * 98.0 + 0.5;
			printf("Phasers are %d%% effective at that range\n", cost);
			break;

		  case 6:			/* warp cost (time/energy) */
			dist = getfltpar("distance");
			if (dist < 0.0)
				break;
			warpfact = getfltpar("warp factor");
			if (warpfact <= 0.0)
				warpfact = Ship.warp;
			cost = (dist + 0.05) * warpfact * warpfact * warpfact;
			time = Param.warptime * dist / (warpfact * warpfact);
			printf("Warp %.2f distance %.2f stardates %.2f cost %d (%d w/ shlds up) units\n",
				warpfact, dist, time, cost, cost + cost);
			break;

		  case 7:			/* impulse cost */
			dist = getfltpar("distance");
			if (dist < 0.0)
				break;
			cost = 20 + 100 * dist;
			time = dist / 0.095;
			printf("Distance %.2f cost %.2f stardates %d units\n",
				dist, time, cost);
			break;

		  case 8:			/* distresslist */
			j = 1;
			printf("\n");
			/* scan the event list */
			for (i = 0; i < MAXEVENTS; i++)
			{
				e = &Event[i];
				/* ignore hidden entries */
				if (e->evcode & E_HIDDEN)
					continue;
				switch (e->evcode & E_EVENT)
				{

				  case E_KDESB:
					printf("Klingon is attacking starbase in quadrant %d,%d\n",
						e->x, e->y);
					j = 0;
					break;

				  case E_ENSLV:
				  case E_REPRO:
					printf("Starsystem %s in quadrant %d,%d is distressed\n",
						Systemname[e->systemname], e->x, e->y);
					j = 0;
					break;
				}
			}
			if (j)
				printf("No known distress calls are active\n");
			break;

		}

		/* skip to next semicolon or newline.  Semicolon
		 * means get new computer request; newline means
		 * exit computer mode. */
		while ((i = getchar()) != ';')
		{
			if (i == '\0')
				exit(1);
			if (i == '\n')
			{
				ungetc(i, stdin);
				return;
			}
		}
	}
}


/*
**  Course Calculation
**
**	Computes and outputs the course and distance from position
**	sqx,sqy/ssx,ssy to tqx,tqy/tsx,tsy.
*/

static int
kalc(int tqx, int tqy, int tsx, int tsy, double *dist)
{
	double		dx, dy;
	double		quadsize;
	double		angle;
	int		course;

	/* normalize to quadrant distances */
	quadsize = NSECTS;
	dx = (Ship.quadx + Ship.sectx / quadsize) - (tqx + tsx / quadsize);
	dy = (tqy + tsy / quadsize) - (Ship.quady + Ship.secty / quadsize);

	/* get the angle */
	angle = atan2(dy, dx);
	/* make it 0 -> 2 pi */
	if (angle < 0.0)
		angle += 6.283185307;
	/* convert from radians to degrees */
	course = angle * 57.29577951 + 0.5;
	dx = dx * dx + dy * dy;
	*dist = sqrt(dx);
	return (course);
}

static void
prkalc(int course, double dist)
{
	printf(": course %d  dist %.3f\n", course, dist);
}
