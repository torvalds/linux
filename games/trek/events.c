/*	$OpenBSD: events.c,v 1.8 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: events.c,v 1.3 1995/04/22 10:58:50 cgd Exp $	*/

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
#include <string.h>

#include "getpar.h"
#include "trek.h"

/*
**  CAUSE TIME TO ELAPSE
**
**	This routine does a hell of a lot.  It elapses time, eats up
**	energy, regenerates energy, processes any events that occur,
**	and so on.
*/

/* argument is set if called in a time warp */
int
events(int warp)
{
	int		i, j;
	char		*p;
	struct kling	*k;
	double		rtime;
	double		xdate;
	double		idate;
	struct event	*ev = NULL;
	int		ix, iy;
	struct quad	*q;
	struct event	*e;
	int		evnum;
	int		restcancel;

	/* if nothing happened, just allow for any Klingons killed */
	if (Move.time <= 0.0)
	{
		Now.time = Now.resource / Now.klings;
		return (0);
	}

	/* indicate that the cloaking device is now working */
	Ship.cloakgood = 1;

	/* idate is the initial date */
	idate = Now.date;

	/* schedule attacks if resting too long */
	if (Move.time > 0.5 && Move.resting)
		schedule(E_ATTACK, 0.5, 0, 0, 0);

	/* scan the event list */
	while (1)
	{
		restcancel = 0;
		evnum = -1;
		/* xdate is the date of the current event */
		xdate = idate + Move.time;

		/* find the first event that has happened */
		for (i = 0; i < MAXEVENTS; i++)
		{
			e = &Event[i];
			if (e->evcode == 0 || (e->evcode & E_GHOST))
				continue;
			if (e->date < xdate)
			{
				xdate = e->date;
				ev = e;
				evnum = i;
			}
		}
		e = ev;

		/* find the time between events */
		rtime = xdate - Now.date;

		/* decrement the magic "Federation Resources" pseudo-variable */
		Now.resource -= Now.klings * rtime;
		/* and recompute the time left */
		Now.time = Now.resource / Now.klings;

		/* move us up to the next date */
		Now.date = xdate;

		/* check for out of time */
		if (Now.time <= 0.0)
			lose(L_NOTIME);
#		ifdef xTRACE
		if (evnum >= 0 && Trace)
			printf("xdate = %.2f, evcode %d params %d %d %d\n",
				xdate, e->evcode, e->x, e->y, e->systemname);
#		endif

		/* if evnum < 0, no events occurred  */
		if (evnum < 0)
			break;

		/* otherwise one did.  Find out what it is */
		switch (e->evcode & E_EVENT)
		{

		  case E_SNOVA:			/* supernova */
			/* cause the supernova to happen */
			snova(-1, 0);
			/* and schedule the next one */
			xresched(e, E_SNOVA, 1);
			break;

		  case E_LRTB:			/* long range tractor beam */
			/* schedule the next one */
			xresched(e, E_LRTB, Now.klings);
			/* LRTB cannot occur if we are docked */
			if (Ship.cond != DOCKED)
			{
				/* pick a new quadrant */
				i = ranf(Now.klings) + 1;
				for (ix = 0; ix < NQUADS; ix++)
				{
					for (iy = 0; iy < NQUADS; iy++)
					{
						q = &Quad[ix][iy];
						if (q->stars >= 0)
							if ((i -= q->klings) <= 0)
								break;
					}
					if (i <= 0)
						break;
				}

				/* test for LRTB to same quadrant */
				if (Ship.quadx == ix && Ship.quady == iy)
					break;

				/* nope, dump him in the new quadrant */
				Ship.quadx = ix;
				Ship.quady = iy;
				printf("\n%s caught in long range tractor beam\n", Ship.shipname);
				printf("*** Pulled to quadrant %d,%d\n", Ship.quadx, Ship.quady);
				Ship.sectx = ranf(NSECTS);
				Ship.secty = ranf(NSECTS);
				initquad(0);
				/* truncate the move time */
				Move.time = xdate - idate;
			}
			break;

		  case E_KATSB:			/* Klingon attacks starbase */
			/* if out of bases, forget it */
			if (Now.bases <= 0)
			{
				unschedule(e);
				break;
			}

			/* check for starbase and Klingons in same quadrant */
			for (i = 0; i < Now.bases; i++)
			{
				ix = Now.base[i].x;
				iy = Now.base[i].y;
				/* see if a Klingon exists in this quadrant */
				q = &Quad[ix][iy];
				if (q->klings <= 0)
					continue;

				/* see if already distressed */
				for (j = 0; j < MAXEVENTS; j++)
				{
					e = &Event[j];
					if ((e->evcode & E_EVENT) != E_KDESB)
						continue;
					if (e->x == ix && e->y == iy)
						break;
				}
				if (j < MAXEVENTS)
					continue;

				/* got a potential attack */
				break;
			}
			e = ev;
			if (i >= Now.bases)
			{
				/* not now; wait a while and see if some Klingons move in */
				reschedule(e, 0.5 + 3.0 * franf());
				break;
			}
			/* schedule a new attack, and a destruction of the base */
			xresched(e, E_KATSB, 1);
			e = xsched(E_KDESB, 1, ix, iy, 0);

			/* report it if we can */
			if (!damaged(SSRADIO))
			{
				printf("\nUhura:  Captain, we have received a distress signal\n");
				printf("  from the starbase in quadrant %d,%d.\n",
					ix, iy);
				restcancel++;
			}
			else
				/* SSRADIO out, make it so we can't see the distress call */
				/* but it's still there!!! */
				e->evcode |= E_HIDDEN;
			break;

		  case E_KDESB:			/* Klingon destroys starbase */
			unschedule(e);
			q = &Quad[e->x][e->y];
			/* if the base has mysteriously gone away, or if the Klingon
			   got tired and went home, ignore this event */
			if (q->bases <=0 || q->klings <= 0)
				break;
			/* are we in the same quadrant? */
			if (e->x == Ship.quadx && e->y == Ship.quady)
			{
				/* yep, kill one in this quadrant */
				printf("\nSpock: ");
				killb(Ship.quadx, Ship.quady);
			}
			else
				/* kill one in some other quadrant */
				killb(e->x, e->y);
			break;

		  case E_ISSUE:		/* issue a distress call */
			xresched(e, E_ISSUE, 1);
			/* if we already have too many, throw this one away */
			if (Ship.distressed >= MAXDISTR)
				break;
			/* try a whole bunch of times to find something suitable */
			for (i = 0; i < 100; i++)
			{
				ix = ranf(NQUADS);
				iy = ranf(NQUADS);
				q = &Quad[ix][iy];
				/* need a quadrant which is not the current one,
				   which has some stars which are inhabited and
				   not already under attack, which is not
				   supernova'ed, and which has some Klingons in it */
				if (!((ix == Ship.quadx && iy == Ship.quady) || q->stars < 0 ||
				    (q->qsystemname & Q_DISTRESSED) ||
				    (q->qsystemname & Q_SYSTEM) == 0 || q->klings <= 0))
					break;
			}
			if (i >= 100)
				/* can't seem to find one; ignore this call */
				break;

			/* got one!!  Schedule its enslavement */
			Ship.distressed++;
			e = xsched(E_ENSLV, 1, ix, iy, q->qsystemname);
			q->qsystemname = (e - Event) | Q_DISTRESSED;

			/* tell the captain about it if we can */
			if (!damaged(SSRADIO))
			{
				printf("\nUhura: Captain, starsystem %s in quadrant %d,%d is under attack\n",
					Systemname[e->systemname], ix, iy);
				restcancel++;
			}
			else
				/* if we can't tell him, make it invisible */
				e->evcode |= E_HIDDEN;
			break;

		  case E_ENSLV:		/* starsystem is enslaved */
			unschedule(e);
			/* see if current distress call still active */
			q = &Quad[e->x][e->y];
			if (q->klings <= 0)
			{
				/* no Klingons, clean up */
				/* restore the system name */
				q->qsystemname = e->systemname;
				break;
			}

			/* play stork and schedule the first baby */
			e = schedule(E_REPRO, Param.eventdly[E_REPRO] * franf(), e->x, e->y, e->systemname);

			/* report the disaster if we can */
			if (!damaged(SSRADIO))
			{
				printf("\nUhura:  We've lost contact with starsystem %s\n",
					Systemname[e->systemname]);
				printf("  in quadrant %d,%d.\n",
					e->x, e->y);
			}
			else
				e->evcode |= E_HIDDEN;
			break;

		  case E_REPRO:		/* Klingon reproduces */
			/* see if distress call is still active */
			q = &Quad[e->x][e->y];
			if (q->klings <= 0)
			{
				unschedule(e);
				q->qsystemname = e->systemname;
				break;
			}
			xresched(e, E_REPRO, 1);
			/* reproduce one Klingon */
			ix = e->x;
			iy = e->y;
			if (Now.klings == 127)
				break;		/* full right now */
			if (q->klings >= MAXKLQUAD)
			{
				/* this quadrant not ok, pick an adjacent one */
				for (i = ix - 1; i <= ix + 1; i++)
				{
					if (i < 0 || i >= NQUADS)
						continue;
					for (j = iy - 1; j <= iy + 1; j++)
					{
						if (j < 0 || j >= NQUADS)
							continue;
						q = &Quad[i][j];
						/* check for this quad ok (not full & no snova) */
						if (q->klings >= MAXKLQUAD || q->stars < 0)
							continue;
						break;
					}
					if (j <= iy + 1)
						break;
				}
				if (j > iy + 1)
					/* cannot create another yet */
					break;
				ix = i;
				iy = j;
			}
			/* deliver the child */
			q->klings++;
			Now.klings++;
			if (ix == Ship.quadx && iy == Ship.quady)
			{
				/* we must position Klingon */
				sector(&ix, &iy);
				Sect[ix][iy] = KLINGON;
				k = &Etc.klingon[Etc.nkling++];
				k->x = ix;
				k->y = iy;
				k->power = Param.klingpwr;
				k->srndreq = 0;
				compkldist(Etc.klingon[0].dist == Etc.klingon[0].avgdist ? 0 : 1);
			}

			/* recompute time left */
			Now.time = Now.resource / Now.klings;
			break;

		  case E_SNAP:		/* take a snapshot of the galaxy */
			xresched(e, E_SNAP, 1);
			p = (char *) Etc.snapshot;
			memcpy(p, Quad, sizeof (Quad));
			p += sizeof (Quad);
			memcpy(p, Event, sizeof (Event));
			p += sizeof (Event);
			memcpy(p, &Now, sizeof (Now));
			Game.snap = 1;
			break;

		  case E_ATTACK:	/* Klingons attack during rest period */
			if (!Move.resting)
			{
				unschedule(e);
				break;
			}
			attack(1);
			reschedule(e, 0.5);
			break;

		  case E_FIXDV:
			i = e->systemname;
			unschedule(e);

			/* de-damage the device */
			printf("%s reports repair work on the %s finished.\n",
				Device[i].person, Device[i].name);

			/* handle special processing upon fix */
			switch (i)
			{

			  case LIFESUP:
				Ship.reserves = Param.reserves;
				break;

			  case SINS:
				if (Ship.cond == DOCKED)
					break;
				printf("Spock has tried to recalibrate your Space Internal Navigation System,\n");
				printf("  but he has no standard base to calibrate to.  Suggest you get\n");
				printf("  to a starbase immediately so that you can properly recalibrate.\n");
				Ship.sinsbad = 1;
				break;

			  case SSRADIO:
				restcancel = dumpssradio();
				break;
			}
			break;

		  default:
			break;
		}

		if (restcancel && Move.resting && getynpar("Spock: Shall we cancel our rest period"))
			Move.time = xdate - idate;

	}

	/* unschedule an attack during a rest period */
	if ((e = Now.eventptr[E_ATTACK]))
		unschedule(e);

	if (!warp)
	{
		/* eat up energy if cloaked */
		if (Ship.cloaked)
			Ship.energy -= Param.cloakenergy * Move.time;

		/* regenerate resources */
		rtime = 1.0 - exp(-Param.regenfac * Move.time);
		Ship.shield += (Param.shield - Ship.shield) * rtime;
		Ship.energy += (Param.energy - Ship.energy) * rtime;

		/* decrement life support reserves */
		if (damaged(LIFESUP) && Ship.cond != DOCKED)
			Ship.reserves -= Move.time;
	}
	return (0);
}
