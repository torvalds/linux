/*	$OpenBSD: update.c,v 1.19 2016/01/08 13:40:05 tb Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
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

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

#include <stdlib.h>
#include <string.h>

#include "extern.h"

void
setseed(const char *seed)
{
	seeded = 1;
	srandom_deterministic(atol(seed));
}

uint32_t
atcrandom(void)
{
	if (seeded)
		return random();
	else
		return arc4random();
}

uint32_t
atcrandom_uniform(uint32_t upper_bound)
{
	if (seeded)
		return random() % upper_bound;
	else
		return arc4random_uniform(upper_bound);
}

void
update(int dummy)
{
	int	i, dir_diff, unclean;
	PLANE	*pp, *p1, *p2;

	clck++;

	erase_all();

	/* put some planes in the air */
	do {
		unclean = 0;
		for (pp = ground.head; pp != NULL; pp = pp->next) {
			if (pp->new_altitude > 0) {
				delete(&ground, pp);
				append(&air, pp);
				unclean = 1;
				break;
			}
		}
	} while (unclean);

	/* do altitude change and basic movement */
	for (pp = air.head; pp != NULL; pp = pp->next) {
		/* type 0 only move every other turn */
		if (pp->plane_type == 0 && clck & 1)
			continue;

		pp->fuel--;
		if (pp->fuel < 0)
			loser(pp, "ran out of fuel.");

		pp->altitude += SGN(pp->new_altitude - pp->altitude);

		if (!pp->delayd) {
			dir_diff = pp->new_dir - pp->dir;
			/*
			 * Allow for circle commands
			 */
			if (pp->new_dir >= 0 && pp->new_dir < MAXDIR) {
				if (dir_diff > MAXDIR/2)
					dir_diff -= MAXDIR;
				else if (dir_diff < -(MAXDIR/2))
					dir_diff += MAXDIR;
			}
			if (dir_diff > 2)
				dir_diff = 2;
			else if (dir_diff < -2)
				dir_diff = -2;
			pp->dir += dir_diff;
			if (pp->dir >= MAXDIR)
				pp->dir -= MAXDIR;
			else if (pp->dir < 0)
				pp->dir += MAXDIR;
		}
		pp->xpos += displacement[pp->dir].dx;
		pp->ypos += displacement[pp->dir].dy;

		if (pp->delayd && pp->xpos == sp->beacon[pp->delayd_no].x &&
		    pp->ypos == sp->beacon[pp->delayd_no].y) {
			pp->delayd = 0;
			if (pp->status == S_UNMARKED)
				pp->status = S_MARKED;
		}

		switch (pp->dest_type) {
		case T_AIRPORT:
			if (pp->xpos == sp->airport[pp->dest_no].x &&
			    pp->ypos == sp->airport[pp->dest_no].y &&
			    pp->altitude == 0) {
				if (pp->dir != sp->airport[pp->dest_no].dir)
				    loser(pp, "landed in the wrong direction.");
				else {
				    pp->status = S_GONE;
				    continue;
				}
			}
			break;
		case T_EXIT:
			if (pp->xpos == sp->exit[pp->dest_no].x &&
			    pp->ypos == sp->exit[pp->dest_no].y) {
			    	if (pp->altitude != 9)
				    loser(pp, "exited at the wrong altitude.");
				else {
				    pp->status = S_GONE;
				    continue;
				}
			}
			break;
		default:
			loser(pp, "has a bizarre destination, get help!");
		}
		if (pp->altitude > 9)
			/* "this is impossible" */
			loser(pp, "exceeded flight ceiling.");
		if (pp->altitude <= 0) {
			for (i = 0; i < sp->num_airports; i++)
				if (pp->xpos == sp->airport[i].x &&
				    pp->ypos == sp->airport[i].y) {
					if (pp->dest_type == T_AIRPORT)
					    loser(pp, 
						"landed at the wrong airport.");
					else
					    loser(pp, 
						"landed instead of exited.");
				}
			loser(pp, "crashed on the ground.");
		}
		if (pp->xpos < 1 || pp->xpos >= sp->width - 1 ||
		    pp->ypos < 1 || pp->ypos >= sp->height - 1) {
			for (i = 0; i < sp->num_exits; i++)
				if (pp->xpos == sp->exit[i].x &&
				    pp->ypos == sp->exit[i].y) {
					if (pp->dest_type == T_EXIT)
					    loser(pp, 
						"exited via the wrong exit.");
					else
					    loser(pp, 
						"exited instead of landed.");
				}
			loser(pp, "illegally left the flight arena.");
		}
	}

	/*
	 * Traverse the list once, deleting the planes that are gone.
	 */
	for (pp = air.head; pp != NULL; pp = p2) {
		p2 = pp->next;
		if (pp->status == S_GONE) {
			safe_planes++;
			delete(&air, pp);
		}
	}

	draw_all();

	for (p1 = air.head; p1 != NULL; p1 = p1->next)
		for (p2 = p1->next; p2 != NULL; p2 = p2->next)
			if (too_close(p1, p2, 1)) {
				static char	buf[80];

				(void)snprintf(buf, sizeof buf,
				    "collided with plane '%c'.",
				    name(p2));
				loser(p1, buf);
			}
	/*
	 * Check every other update.  Actually, only add on even updates.
	 * Otherwise, prop jobs show up *on* entrance.  Remember that
	 * we don't update props on odd updates.
	 */
	if (atcrandom_uniform(sp->newplane_time) == 0)
		addplane();
}

const char *
command(PLANE *pp)
{
	static char	buf[50], *bp, *comm_start;

	buf[0] = '\0';
	bp = buf;
	(void)snprintf(bp, buf + sizeof buf - bp,
		"%c%d%c%c%d: ", name(pp), pp->altitude, 
		(pp->fuel < LOWFUEL) ? '*' : ' ',
		(pp->dest_type == T_AIRPORT) ? 'A' : 'E', pp->dest_no);

	comm_start = bp = strchr(buf, '\0');
	if (pp->altitude == 0)
		(void)snprintf(bp, buf + sizeof buf - bp,
			"Holding @ A%d", pp->orig_no);
	else if (pp->new_dir >= MAXDIR || pp->new_dir < 0)
		strlcpy(bp, "Circle", buf + sizeof buf - bp);
	else if (pp->new_dir != pp->dir)
		(void)snprintf(bp, buf + sizeof buf - bp,
			"%d", dir_deg(pp->new_dir));

	bp = strchr(buf, '\0');
	if (pp->delayd)
		(void)snprintf(bp, buf + sizeof buf - bp,
			" @ B%d", pp->delayd_no);

	bp = strchr(buf, '\0');
	if (*comm_start == '\0' && 
	    (pp->status == S_UNMARKED || pp->status == S_IGNORED))
		strlcpy(bp, "---------", buf + sizeof buf - bp);
	return (buf);
}

char
name(const PLANE *p)
{
	if (p->plane_type == 0)
		return ('A' + p->plane_no);
	else
		return ('a' + p->plane_no);
}

int
number(char l)
{
	if (l >= 'a' && l <= 'z')
		return (l - 'a');
	else if (l >= 'A' && l <= 'Z')
		return (l - 'A');
	else
		return (-1);
}

int
next_plane(void)
{
	static int	last_plane = -1;
	PLANE		*pp;
	int		found, start_plane = last_plane;

	do {
		found = 0;
		last_plane++;
		if (last_plane >= 26)
			last_plane = 0;
		for (pp = air.head; pp != NULL; pp = pp->next)
			if (pp->plane_no == last_plane) {
				found++;
				break;
			}
		if (!found)
			for (pp = ground.head; pp != NULL; pp = pp->next)
				if (pp->plane_no == last_plane) {
					found++;
					break;
				}
	} while (found && last_plane != start_plane);
	if (last_plane == start_plane)
		return (-1);
	return (last_plane);
}

int
addplane(void)
{
	PLANE	p, *pp, *p1;
	int	i, num_starts, close, rnd, rnd2, pnum;

	memset(&p, 0, sizeof (p));

	p.status = S_MARKED;
	p.plane_type = atcrandom_uniform(2);

	num_starts = sp->num_exits + sp->num_airports;
	rnd = atcrandom_uniform(num_starts);

	if (rnd < sp->num_exits) {
		p.dest_type = T_EXIT;
		p.dest_no = rnd;
	} else {
		p.dest_type = T_AIRPORT;
		p.dest_no = rnd - sp->num_exits;
	}

	/* loop until we get a plane not near another */
	for (i = 0; i < num_starts; i++) {
		/* loop till we get a different start point */
		while ((rnd2 = atcrandom_uniform(num_starts)) == rnd)
			;
		if (rnd2 < sp->num_exits) {
			p.orig_type = T_EXIT;
			p.orig_no = rnd2;
			p.xpos = sp->exit[rnd2].x;
			p.ypos = sp->exit[rnd2].y;
			p.new_dir = p.dir = sp->exit[rnd2].dir;
			p.altitude = p.new_altitude = 7;
			close = 0;
			for (p1 = air.head; p1 != NULL; p1 = p1->next)
				if (too_close(p1, &p, 4)) {
					close++;
					break;
				}
			if (close)
				continue;
		} else {
			p.orig_type = T_AIRPORT;
			p.orig_no = rnd2 - sp->num_exits;
			p.xpos = sp->airport[p.orig_no].x;
			p.ypos = sp->airport[p.orig_no].y;
			p.new_dir = p.dir = sp->airport[p.orig_no].dir;
			p.altitude = p.new_altitude = 0;
		}
		p.fuel = sp->width + sp->height;
		break;
	}
	if (i >= num_starts)
		return (-1);
	pnum = next_plane();
	if (pnum < 0)
		return (-1);
	p.plane_no = pnum;

	pp = newplane();
	memcpy(pp, &p, sizeof (p));

	if (pp->orig_type == T_AIRPORT)
		append(&ground, pp);
	else
		append(&air, pp);

	return (pp->dest_type);
}

PLANE	*
findplane(int n)
{
	PLANE	*pp;

	for (pp = air.head; pp != NULL; pp = pp->next)
		if (pp->plane_no == n)
			return (pp);
	for (pp = ground.head; pp != NULL; pp = pp->next)
		if (pp->plane_no == n)
			return (pp);
	return (NULL);
}

int
too_close(const PLANE *p1, const PLANE *p2, int dist)
{
	if (ABS(p1->altitude - p2->altitude) <= dist &&
	    ABS(p1->xpos - p2->xpos) <= dist && ABS(p1->ypos - p2->ypos) <= dist)
		return (1);
	else
		return (0);
}

int
dir_deg(int d)
{
	switch (d) {
	case 0: return (0);
	case 1: return (45);
	case 2: return (90);
	case 3: return (135);
	case 4: return (180);
	case 5: return (225);
	case 6: return (270);
	case 7: return (315);
	default:
		return (-1);
	}
}
