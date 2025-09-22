/*	$OpenBSD: shots.c,v 1.14 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: shots.c,v 1.3 1997/10/11 08:13:50 lukem Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * + Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor
 *   the names of its contributors may be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>
#include <stdlib.h>
#include <syslog.h>

#include "conf.h"
#include "hunt.h"
#include "server.h"

#define	PLUS_DELTA(x, max)	if (x < max) x++; else x--
#define	MINUS_DELTA(x, min)	if (x > min) x--; else x++

static	void	chkshot(BULLET *, BULLET *);
static	void	chkslime(BULLET *, BULLET *);
static	void	explshot(BULLET *, int, int);
static	void	find_under(BULLET *, BULLET *);
static	int	iswall(int, int);
static	void	mark_boot(BULLET *);
static	void	mark_player(BULLET *);
static	int	move_drone(BULLET *);
static	void	move_flyer(PLAYER *);
static	int	move_normal_shot(BULLET *);
static	void	move_slime(BULLET *, int, BULLET *);
static	void	save_bullet(BULLET *);
static	void	zapshot(BULLET *, BULLET *);

/* Return true if there is pending activity */
int
can_moveshots(void)
{
	PLAYER *pp;

	/* Bullets are moving? */
	if (Bullets)
		return 1;

	/* Explosions are happening? */
	if (can_rollexpl())
		return 1;

	/* Things are flying? */
	for (pp = Boot; pp < &Boot[NBOOTS]; pp++)
		if (pp->p_flying >= 0)
			return 1;
	for (pp = Player; pp < End_player; pp++)
		if (pp->p_flying >= 0)
			return 1;

	/* Everything is quiet: */
	return 0;
}

/*
 * moveshots:
 *	Move the shots already in the air, taking explosions into account
 */
void
moveshots(void)
{
	BULLET	*bp, *next;
	PLAYER	*pp;
	int	x, y;
	BULLET	*blist;

	rollexpl();
	if (Bullets == NULL)
		goto no_bullets;

	/*
	 * First we move through the bullet list conf_bulspd times, looking
	 * for things we may have run into.  If we do run into
	 * something, we set up the explosion and disappear, checking
	 * for damage to any player who got in the way.
	 */

	/* Move the list to a working list */
	blist = Bullets;
	Bullets = NULL;

	/* Work with bullets on the working list (blist) */
	for (bp = blist; bp != NULL; bp = next) {
		next = bp->b_next;

		x = bp->b_x;
		y = bp->b_y;

		/* Un-draw the bullet on all screens: */
		Maze[y][x] = bp->b_over;
		check(ALL_PLAYERS, y, x);

		/* Decide how to move the bullet: */
		switch (bp->b_type) {

		  /* Normal, atomic bullets: */
		  case SHOT:
		  case GRENADE:
		  case SATCHEL:
		  case BOMB:
			if (move_normal_shot(bp)) {
				/* Still there: put back on the active list */
				bp->b_next = Bullets;
				Bullets = bp;
			}
			break;

		  /* Slime bullets that explode into slime on impact: */
		  case SLIME:
			if (bp->b_expl || move_normal_shot(bp)) {
				/* Still there: put back on the active list */
				bp->b_next = Bullets;
				Bullets = bp;
			}
			break;

		  /* Drones that wander about: */
		  case DSHOT:
			if (move_drone(bp)) {
				/* Still there: put back on the active list */
				bp->b_next = Bullets;
				Bullets = bp;
			}
			break;

		  /* Other/unknown: */
		  default:
			/* Place it back on the active list: */
			bp->b_next = Bullets;
			Bullets = bp;
			break;
		}
	}

	/* Again, hang the Bullets list off `blist' and work with that: */
	blist = Bullets;
	Bullets = NULL;
	for (bp = blist; bp != NULL; bp = next) {
		next = bp->b_next;
		/* Is the bullet exploding? */
		if (!bp->b_expl) {
			/*
			 * Its still flying through the air.
			 * Put it back on the bullet list.
			 */
			save_bullet(bp);

			/* All the monitors can see the bullet: */
			for (pp = Monitor; pp < End_monitor; pp++)
				check(pp, bp->b_y, bp->b_x);

			/* All the scanning players can see the drone: */
			if (bp->b_type == DSHOT)
				for (pp = Player; pp < End_player; pp++)
					if (pp->p_scan >= 0)
						check(pp, bp->b_y, bp->b_x);
		} else {
			/* It is exploding. Check what we hit: */
			chkshot(bp, next);
			/* Release storage for the destroyed bullet: */
			free(bp);
		}
	}

	/* Re-draw all the players: (in case a bullet wiped them out) */
	for (pp = Player; pp < End_player; pp++)
		Maze[pp->p_y][pp->p_x] = pp->p_face;

no_bullets:

	/* Move flying boots through the air: */
	for (pp = Boot; pp < &Boot[NBOOTS]; pp++)
		if (pp->p_flying >= 0)
			move_flyer(pp);

	/* Move flying players through the air: */
	for (pp = Player; pp < End_player; pp++) {
		if (pp->p_flying >= 0)
			move_flyer(pp);
		/* Flush out the explosions: */
		sendcom(pp, REFRESH);
		look(pp);
	}

	/* Flush out and synchronise all the displays: */
	sendcom(ALL_PLAYERS, REFRESH);
}

/*
 * move_normal_shot:
 *	Move a normal shot along its trajectory.
 *	Returns false if the bullet no longer needs tracking.
 */
static int
move_normal_shot(BULLET  *bp)
{
	int	i, x, y;
	PLAYER	*pp;

	/*
	 * Walk an unexploded bullet along conf_bulspd times, moving it
	 * one unit along each step. We flag it as exploding if it
	 * meets something.
	 */

	for (i = 0; i < conf_bulspd; i++) {

		/* Stop if the bullet has already exploded: */
		if (bp->b_expl)
			break;

		/* Adjust the bullet's co-ordinates: */
		x = bp->b_x;
		y = bp->b_y;
		switch (bp->b_face) {
		  case LEFTS:
			x--;
			break;
		  case RIGHT:
			x++;
			break;
		  case ABOVE:
			y--;
			break;
		  case BELOW:
			y++;
			break;
		}


		/* Look at what the bullet is colliding with : */
		switch (Maze[y][x]) {
		  /* Gun shots have a chance of collision: */
		  case SHOT:
			if (rand_num(100) < conf_pshot_coll) {
				zapshot(Bullets, bp);
				zapshot(bp->b_next, bp);
			}
			break;
		  /* Grenades only have a chance of collision: */
		  case GRENADE:
			if (rand_num(100) < conf_pgren_coll) {
				zapshot(Bullets, bp);
				zapshot(bp->b_next, bp);
			}
			break;
		  /* Reflecting walls richochet the bullet: */
		  case WALL4:
			switch (bp->b_face) {
			  case LEFTS:
				bp->b_face = BELOW;
				break;
			  case RIGHT:
				bp->b_face = ABOVE;
				break;
			  case ABOVE:
				bp->b_face = RIGHT;
				break;
			  case BELOW:
				bp->b_face = LEFTS;
				break;
			}
			Maze[y][x] = WALL5;
			for (pp = Monitor; pp < End_monitor; pp++)
				check(pp, y, x);
			break;
		  case WALL5:
			switch (bp->b_face) {
			  case LEFTS:
				bp->b_face = ABOVE;
				break;
			  case RIGHT:
				bp->b_face = BELOW;
				break;
			  case ABOVE:
				bp->b_face = LEFTS;
				break;
			  case BELOW:
				bp->b_face = RIGHT;
				break;
			}
			Maze[y][x] = WALL4;
			for (pp = Monitor; pp < End_monitor; pp++)
				check(pp, y, x);
			break;
		  /* Dispersion doors randomly disperse bullets: */
		  case DOOR:
			switch (rand_num(4)) {
			  case 0:
				bp->b_face = ABOVE;
				break;
			  case 1:
				bp->b_face = BELOW;
				break;
			  case 2:
				bp->b_face = LEFTS;
				break;
			  case 3:
				bp->b_face = RIGHT;
				break;
			}
			break;
		  /* Bullets zing past fliers: */
		  case FLYER:
			pp = play_at(y, x);
			message(pp, "Zing!");
			break;
		  /* Bullets encountering a player: */
		  case LEFTS:
		  case RIGHT:
		  case BELOW:
		  case ABOVE:
			/*
			 * Give the person a chance to catch a
			 * grenade if s/he is facing it:
			 */
			pp = play_at(y, x);
			pp->p_ident->i_shot += bp->b_charge;
			if (opposite(bp->b_face, Maze[y][x])) {
			    /* Give them a 10% chance: */
			    if (rand_num(100) < conf_pgren_catch) {
				/* They caught it! */
				if (bp->b_owner != NULL)
					message(bp->b_owner,
					    "Your charge was absorbed!");

				/*
				 * The target player stole from the bullet's
				 * owner. Charge stolen statistics:
				 */
				if (bp->b_score != NULL)
					bp->b_score->i_robbed += bp->b_charge;

				/* They acquire more ammo: */
				pp->p_ammo += bp->b_charge;

				/* Check if it would have destroyed them: */
				if (pp->p_damage + bp->b_size * conf_mindam
				    > pp->p_damcap)
					/* Lucky escape statistics: */
					pp->p_ident->i_saved++;

				/* Tell them: */
				message(pp, "Absorbed charge (good shield!)");

				/* Absorbtion statistics: */
				pp->p_ident->i_absorbed += bp->b_charge;

				/* Deallocate storage: */
				free(bp);

				/* Update ammo display: */
				ammo_update(pp);

				/* No need for caller to keep tracking it: */
				return FALSE;
			    }

			    /* Bullets faced head-on (statistics): */
			    pp->p_ident->i_faced += bp->b_charge;
			}

			/*
			 * Small chance that the bullet just misses the
			 * person.  If so, the bullet just goes on its
			 * merry way without exploding. (5% chance)
			 */
			if (rand_num(100) < conf_pmiss) {
				/* Ducked statistics: */
				pp->p_ident->i_ducked += bp->b_charge;

				/* Check if it would have killed them: */
				if (pp->p_damage + bp->b_size * conf_mindam
				    > pp->p_damcap)
					/* Lucky escape statistics: */
					pp->p_ident->i_saved++;

				/* Shooter missed statistics: */
				if (bp->b_score != NULL)
					bp->b_score->i_missed += bp->b_charge;

				/* Tell target that they were missed: */
				message(pp, "Zing!");

				/* Tell the bullet owner they missed: */
				if (bp->b_owner != NULL)
				    message(bp->b_owner,
					((bp->b_score->i_missed & 0x7) == 0x7) ?
					"My!  What a bad shot you are!" :
					"Missed him");

				/* Don't fall through */
				break;
			} else {
				/* The player is to be blown up: */
				bp->b_expl = TRUE;
			}
			break;
		  /* Bullet hits a wall, and always explodes: */
		  case WALL1:
		  case WALL2:
		  case WALL3:
			bp->b_expl = TRUE;
			break;
		}

		/* Update the bullet's new position: */
		bp->b_x = x;
		bp->b_y = y;
	}

	/* Caller should keep tracking the bullet: */
	return TRUE;
}

/*
 * move_drone:
 *	Move the drone to the next square
 *	Returns FALSE if the drone need no longer be tracked.
 */
static int
move_drone(BULLET *bp)
{
	int	mask, count;
	int	n, dir = -1;
	PLAYER	*pp;

	/* See if we can give someone a blast: */
	if (is_player(Maze[bp->b_y][bp->b_x - 1])) {
		dir = WEST;
		goto drone_move;
	}
	if (is_player(Maze[bp->b_y - 1][bp->b_x])) {
		dir = NORTH;
		goto drone_move;
	}
	if (is_player(Maze[bp->b_y + 1][bp->b_x])) {
		dir = SOUTH;
		goto drone_move;
	}
	if (is_player(Maze[bp->b_y][bp->b_x + 1])) {
		dir = EAST;
		goto drone_move;
	}

	/* Find out what directions are clear and move that way: */
	mask = count = 0;
	if (!iswall(bp->b_y, bp->b_x - 1))
		mask |= WEST, count++;
	if (!iswall(bp->b_y - 1, bp->b_x))
		mask |= NORTH, count++;
	if (!iswall(bp->b_y + 1, bp->b_x))
		mask |= SOUTH, count++;
	if (!iswall(bp->b_y, bp->b_x + 1))
		mask |= EAST, count++;

	/* All blocked up, just wait: */
	if (count == 0)
		return TRUE;

	/* Only one way to go: */
	if (count == 1) {
		dir = mask;
		goto drone_move;
	}

	/* Avoid backtracking, and remove the direction we came from: */
	switch (bp->b_face) {
	  case LEFTS:
		if (mask & EAST)
			mask &= ~EAST, count--;
		break;
	  case RIGHT:
		if (mask & WEST)
			mask &= ~WEST, count--;
		break;
	  case ABOVE:
		if (mask & SOUTH)
			mask &= ~SOUTH, count--;
		break;
	  case BELOW:
		if (mask & NORTH)
			mask &= ~NORTH, count--;
		break;
	}

	/* Pick one of the remaining directions: */
	n = rand_num(count);
	if (n >= 0 && mask & NORTH)
		dir = NORTH, n--;
	if (n >= 0 && mask & SOUTH)
		dir = SOUTH, n--;
	if (n >= 0 && mask & EAST)
		dir = EAST, n--;
	if (n >= 0 && mask & WEST)
		dir = WEST, n--;

drone_move:
	/* Move the drone: */
	switch (dir) {
	  case -1:
		/* no move */
	  case WEST:
		bp->b_x--;
		bp->b_face = LEFTS;
		break;
	  case EAST:
		bp->b_x++;
		bp->b_face = RIGHT;
		break;
	  case NORTH:
		bp->b_y--;
		bp->b_face = ABOVE;
		break;
	  case SOUTH:
		bp->b_y++;
		bp->b_face = BELOW;
		break;
	}

	/* Look at what the drone moved onto: */
	switch (Maze[bp->b_y][bp->b_x]) {
	  case LEFTS:
	  case RIGHT:
	  case BELOW:
	  case ABOVE:
		/*
		 * Players have a 1% chance of absorbing a drone,
		 * if they are facing it.
		 */
		if (rand_num(100) < conf_pdroneabsorb && opposite(bp->b_face,
		    Maze[bp->b_y][bp->b_x])) {

			/* Feel the power: */
			pp = play_at(bp->b_y, bp->b_x);
			pp->p_ammo += bp->b_charge;
			message(pp, "**** Absorbed drone ****");

			/* Release drone storage: */
			free(bp);

			/* Update ammo: */
			ammo_update(pp);

			/* No need for caller to keep tracking drone: */
			return FALSE;
		}
		/* Detonate the drone: */
		bp->b_expl = TRUE;
		break;
	}

	/* Keep tracking the drone. */
	return TRUE;
}

/*
 * save_bullet:
 *	Put a bullet back onto the bullet list
 */
static void
save_bullet(BULLET *bp)
{

	/* Save what the bullet will be flying over: */
	bp->b_over = Maze[bp->b_y][bp->b_x];

	switch (bp->b_over) {
	  /* Bullets that can pass through each other: */
	  case SHOT:
	  case GRENADE:
	  case SATCHEL:
	  case BOMB:
	  case SLIME:
	  case LAVA:
	  case DSHOT:
		find_under(Bullets, bp);
		break;
	}

	switch (bp->b_over) {
	  /* A bullet hits a player: */
	  case LEFTS:
	  case RIGHT:
	  case ABOVE:
	  case BELOW:
	  case FLYER:
		mark_player(bp);
		break;

	  /* A bullet passes a boot: */
	  case BOOT:
	  case BOOT_PAIR:
		mark_boot(bp);
		/* FALLTHROUGH */

	  /* The bullet flies over everything else: */
	  default:
		Maze[bp->b_y][bp->b_x] = bp->b_type;
		break;
	}

	/* Insert the bullet into the Bullets list: */
	bp->b_next = Bullets;
	Bullets = bp;
}

/*
 * move_flyer:
 *	Update the position of a player in flight
 */
static void
move_flyer(PLAYER *pp)
{
	int	x, y;

	if (pp->p_undershot) {
		fixshots(pp->p_y, pp->p_x, pp->p_over);
		pp->p_undershot = FALSE;
	}

	/* Restore what the flier was flying over */
	Maze[pp->p_y][pp->p_x] = pp->p_over;

	/* Fly: */
	x = pp->p_x + pp->p_flyx;
	y = pp->p_y + pp->p_flyy;

	/* Bouncing off the edges of the maze: */
	if (x < 1) {
		x = 1 - x;
		pp->p_flyx = -pp->p_flyx;
	}
	else if (x > WIDTH - 2) {
		x = (WIDTH - 2) - (x - (WIDTH - 2));
		pp->p_flyx = -pp->p_flyx;
	}
	if (y < 1) {
		y = 1 - y;
		pp->p_flyy = -pp->p_flyy;
	}
	else if (y > HEIGHT - 2) {
		y = (HEIGHT - 2) - (y - (HEIGHT - 2));
		pp->p_flyy = -pp->p_flyy;
	}

	/* Make sure we don't land on something we can't: */
again:
	switch (Maze[y][x]) {
	  default:
		/*
		 * Flier is over something other than space, a wall
		 * or a door. Randomly move (drift) the flier a little bit
		 * and then try again:
		 */
		switch (rand_num(4)) {
		  case 0:
			PLUS_DELTA(x, WIDTH - 2);
			break;
		  case 1:
			MINUS_DELTA(x, 1);
			break;
		  case 2:
			PLUS_DELTA(y, HEIGHT - 2);
			break;
		  case 3:
			MINUS_DELTA(y, 1);
			break;
		}
		goto again;
	  /* Give a little boost when about to land on a wall or door: */
	  case WALL1:
	  case WALL2:
	  case WALL3:
	  case WALL4:
	  case WALL5:
	  case DOOR:
		if (pp->p_flying == 0)
			pp->p_flying++;
		break;
	  /* Spaces are okay: */
	  case SPACE:
		break;
	}

	/* Update flier's coordinates: */
	pp->p_y = y;
	pp->p_x = x;

	/* Consume 'flying' time: */
	if (pp->p_flying-- == 0) {
		/* Land: */
		if (pp->p_face != BOOT && pp->p_face != BOOT_PAIR) {
			/* Land a player - they stustain a fall: */
			checkdam(pp, (PLAYER *) NULL, (IDENT *) NULL,
				rand_num(pp->p_damage / conf_fall_frac), FALL);
			pp->p_face = rand_dir();
			showstat(pp);
		} else {
			/* Land boots: */
			if (Maze[y][x] == BOOT)
				pp->p_face = BOOT_PAIR;
			Maze[y][x] = SPACE;
		}
	}

	/* Save under the flier: */
	pp->p_over = Maze[y][x];
	/* Draw in the flier: */
	Maze[y][x] = pp->p_face;
	showexpl(y, x, pp->p_face);
}

/*
 * chkshot
 *	Handle explosions
 */
static void
chkshot(BULLET *bp, BULLET *next)
{
	int	y, x;
	int	dy, dx, absdy;
	int	delta, damage;
	char	expl;
	PLAYER	*pp;

	delta = 0;
	switch (bp->b_type) {
	  case SHOT:
	  case MINE:
	  case GRENADE:
	  case GMINE:
	  case SATCHEL:
	  case BOMB:
		delta = bp->b_size - 1;
		break;
	  case SLIME:
	  case LAVA:
		chkslime(bp, next);
		return;
	  case DSHOT:
		bp->b_type = SLIME;
		chkslime(bp, next);
		return;
	}

	/* Draw the explosion square: */
	for (y = bp->b_y - delta; y <= bp->b_y + delta; y++) {
		if (y < 0 || y >= HEIGHT)
			continue;
		dy = y - bp->b_y;
		absdy = (dy < 0) ? -dy : dy;
		for (x = bp->b_x - delta; x <= bp->b_x + delta; x++) {
			/* Draw a part of the explosion cloud: */
			if (x < 0 || x >= WIDTH)
				continue;
			dx = x - bp->b_x;
			if (dx == 0)
				expl = (dy == 0) ? '*' : '|';
			else if (dy == 0)
				expl = '-';
			else if (dx == dy)
				expl = '\\';
			else if (dx == -dy)
				expl = '/';
			else
				expl = '*';
			showexpl(y, x, expl);

			/* Check what poor bastard was in the explosion: */
			switch (Maze[y][x]) {
			  case LEFTS:
			  case RIGHT:
			  case ABOVE:
			  case BELOW:
			  case FLYER:
				if (dx < 0)
					dx = -dx;
				if (absdy > dx)
					damage = bp->b_size - absdy;
				else
					damage = bp->b_size - dx;

				/* Everybody hurts, sometimes. */
				pp = play_at(y, x);
				checkdam(pp, bp->b_owner, bp->b_score,
					damage * conf_mindam, bp->b_type);
				break;
			  case GMINE:
			  case MINE:
				/* Mines detonate in a chain reaction: */
				add_shot((Maze[y][x] == GMINE) ?
					GRENADE : SHOT,
					y, x, LEFTS,
					(Maze[y][x] == GMINE) ?
					GRENREQ : BULREQ,
					(PLAYER *) NULL, TRUE, SPACE);
				Maze[y][x] = SPACE;
				break;
			}
		}
	}
}

/*
 * chkslime:
 *	handle slime shot exploding
 */
static void
chkslime(BULLET *bp, BULLET *next)
{
	BULLET	*nbp;

	switch (Maze[bp->b_y][bp->b_x]) {
	  /* Slime explodes on walls and doors: */
	  case WALL1:
	  case WALL2:
	  case WALL3:
	  case WALL4:
	  case WALL5:
	  case DOOR:
		switch (bp->b_face) {
		  case LEFTS:
			bp->b_x++;
			break;
		  case RIGHT:
			bp->b_x--;
			break;
		  case ABOVE:
			bp->b_y++;
			break;
		  case BELOW:
			bp->b_y--;
			break;
		}
		break;
	}

	/* Duplicate the unit of slime: */
	nbp = malloc(sizeof (BULLET));
	if (nbp == NULL) {
		logit(LOG_ERR, "malloc");
		return;
	}
	*nbp = *bp;

	/* Move it around: */
	move_slime(nbp, nbp->b_type == SLIME ? conf_slimespeed :
	    conf_lavaspeed, next);
}

/*
 * move_slime:
 *	move the given slime shot speed times and add it back if
 *	it hasn't fizzled yet
 */
static void
move_slime(BULLET *bp, int speed, BULLET *next)
{
	int	i, j, dirmask, count;
	PLAYER	*pp;
	BULLET	*nbp;

	if (speed == 0) {
		if (bp->b_charge <= 0)
			free(bp);
		else
			save_bullet(bp);
		return;
	}

	/* Draw it: */
	showexpl(bp->b_y, bp->b_x, bp->b_type == LAVA ? LAVA : '*');

	switch (Maze[bp->b_y][bp->b_x]) {
	  /* Someone got hit by slime or lava: */
	  case LEFTS:
	  case RIGHT:
	  case ABOVE:
	  case BELOW:
	  case FLYER:
		pp = play_at(bp->b_y, bp->b_x);
		message(pp, "You've been slimed.");
		checkdam(pp, bp->b_owner, bp->b_score, conf_mindam, bp->b_type);
		break;
	  /* Bullets detonate in slime and lava: */
	  case SHOT:
	  case GRENADE:
	  case SATCHEL:
	  case BOMB:
	  case DSHOT:
		explshot(next, bp->b_y, bp->b_x);
		explshot(Bullets, bp->b_y, bp->b_x);
		break;
	}


	/* Drain the slime/lava of some energy: */
	if (--bp->b_charge <= 0) {
		/* It fizzled: */
		free(bp);
		return;
	}

	/* Figure out which way the slime should flow: */
	dirmask = 0;
	count = 0;
	switch (bp->b_face) {
	  case LEFTS:
		if (!iswall(bp->b_y, bp->b_x - 1))
			dirmask |= WEST, count++;
		if (!iswall(bp->b_y - 1, bp->b_x))
			dirmask |= NORTH, count++;
		if (!iswall(bp->b_y + 1, bp->b_x))
			dirmask |= SOUTH, count++;
		if (dirmask == 0)
			if (!iswall(bp->b_y, bp->b_x + 1))
				dirmask |= EAST, count++;
		break;
	  case RIGHT:
		if (!iswall(bp->b_y, bp->b_x + 1))
			dirmask |= EAST, count++;
		if (!iswall(bp->b_y - 1, bp->b_x))
			dirmask |= NORTH, count++;
		if (!iswall(bp->b_y + 1, bp->b_x))
			dirmask |= SOUTH, count++;
		if (dirmask == 0)
			if (!iswall(bp->b_y, bp->b_x - 1))
				dirmask |= WEST, count++;
		break;
	  case ABOVE:
		if (!iswall(bp->b_y - 1, bp->b_x))
			dirmask |= NORTH, count++;
		if (!iswall(bp->b_y, bp->b_x - 1))
			dirmask |= WEST, count++;
		if (!iswall(bp->b_y, bp->b_x + 1))
			dirmask |= EAST, count++;
		if (dirmask == 0)
			if (!iswall(bp->b_y + 1, bp->b_x))
				dirmask |= SOUTH, count++;
		break;
	  case BELOW:
		if (!iswall(bp->b_y + 1, bp->b_x))
			dirmask |= SOUTH, count++;
		if (!iswall(bp->b_y, bp->b_x - 1))
			dirmask |= WEST, count++;
		if (!iswall(bp->b_y, bp->b_x + 1))
			dirmask |= EAST, count++;
		if (dirmask == 0)
			if (!iswall(bp->b_y - 1, bp->b_x))
				dirmask |= NORTH, count++;
		break;
	}
	if (count == 0) {
		/*
		 * No place to go.  Just sit here for a while and wait
		 * for adjacent squares to clear out.
		 */
		save_bullet(bp);
		return;
	}
	if (bp->b_charge < count) {
		/* Only bp->b_charge paths may be taken */
		while (count > bp->b_charge) {
			if (dirmask & WEST)
				dirmask &= ~WEST;
			else if (dirmask & EAST)
				dirmask &= ~EAST;
			else if (dirmask & NORTH)
				dirmask &= ~NORTH;
			else if (dirmask & SOUTH)
				dirmask &= ~SOUTH;
			count--;
		}
	}

	/* Spawn little slimes off in every possible direction: */
	i = bp->b_charge / count;
	j = bp->b_charge % count;
	if (dirmask & WEST) {
		count--;
		nbp = create_shot(bp->b_type, bp->b_y, bp->b_x - 1, LEFTS,
			i, bp->b_size, bp->b_owner, bp->b_score, TRUE, SPACE);
		move_slime(nbp, speed - 1, next);
	}
	if (dirmask & EAST) {
		count--;
		nbp = create_shot(bp->b_type, bp->b_y, bp->b_x + 1, RIGHT,
			(count < j) ? i + 1 : i, bp->b_size, bp->b_owner,
			bp->b_score, TRUE, SPACE);
		move_slime(nbp, speed - 1, next);
	}
	if (dirmask & NORTH) {
		count--;
		nbp = create_shot(bp->b_type, bp->b_y - 1, bp->b_x, ABOVE,
			(count < j) ? i + 1 : i, bp->b_size, bp->b_owner,
			bp->b_score, TRUE, SPACE);
		move_slime(nbp, speed - 1, next);
	}
	if (dirmask & SOUTH) {
		count--;
		nbp = create_shot(bp->b_type, bp->b_y + 1, bp->b_x, BELOW,
			(count < j) ? i + 1 : i, bp->b_size, bp->b_owner,
			bp->b_score, TRUE, SPACE);
		move_slime(nbp, speed - 1, next);
	}

	free(bp);
}

/*
 * iswall:
 *	returns whether the given location is a wall
 */
static int
iswall(int y, int x)
{
	if (y < 0 || x < 0 || y >= HEIGHT || x >= WIDTH)
		return TRUE;
	switch (Maze[y][x]) {
	  case WALL1:
	  case WALL2:
	  case WALL3:
	  case WALL4:
	  case WALL5:
	  case DOOR:
	  case SLIME:
	  case LAVA:
		return TRUE;
	}
	return FALSE;
}

/*
 * zapshot:
 *	Take a shot out of the air.
 */
static void
zapshot(BULLET *blist, BULLET *obp)
{
	BULLET	*bp;

	for (bp = blist; bp != NULL; bp = bp->b_next) {
		/* Find co-located bullets not facing the same way: */
		if (bp->b_face != obp->b_face
		    && bp->b_x == obp->b_x && bp->b_y == obp->b_y)
		{
			/* Bullet collision: */
			explshot(blist, obp->b_y, obp->b_x);
			return;
		}
	}
}

/*
 * explshot -
 *	Make all shots at this location blow up
 */
static void
explshot(BULLET *blist, int y, int x)
{
	BULLET	*bp;

	for (bp = blist; bp != NULL; bp = bp->b_next)
		if (bp->b_x == x && bp->b_y == y) {
			bp->b_expl = TRUE;
			if (bp->b_owner != NULL)
				message(bp->b_owner, "Shot intercepted.");
		}
}

/*
 * play_at:
 *	Return a pointer to the player at the given location
 */
PLAYER *
play_at(int y, int x)
{
	PLAYER	*pp;

	for (pp = Player; pp < End_player; pp++)
		if (pp->p_x == x && pp->p_y == y)
			return pp;

	/* Internal fault: */
	logx(LOG_ERR, "play_at: not a player");
	abort();
}

/*
 * opposite:
 *	Return TRUE if the bullet direction faces the opposite direction
 *	of the player in the maze
 */
int
opposite(int face, char dir)
{
	switch (face) {
	  case LEFTS:
		return (dir == RIGHT);
	  case RIGHT:
		return (dir == LEFTS);
	  case ABOVE:
		return (dir == BELOW);
	  case BELOW:
		return (dir == ABOVE);
	  default:
		return FALSE;
	}
}

/*
 * is_bullet:
 *	Is there a bullet at the given coordinates?  If so, return
 *	a pointer to the bullet, otherwise return NULL
 */
BULLET *
is_bullet(int y, int x)
{
	BULLET	*bp;

	for (bp = Bullets; bp != NULL; bp = bp->b_next)
		if (bp->b_y == y && bp->b_x == x)
			return bp;
	return NULL;
}

/*
 * fixshots:
 *	change the underlying character of the shots at a location
 *	to the given character.
 */
void
fixshots(int y, int x, char over)
{
	BULLET	*bp;

	for (bp = Bullets; bp != NULL; bp = bp->b_next)
		if (bp->b_y == y && bp->b_x == x)
			bp->b_over = over;
}

/*
 * find_under:
 *	find the underlying character for a bullet when it lands
 *	on another bullet.
 */
static void
find_under(BULLET *blist, BULLET *bp)
{
	BULLET	*nbp;

	for (nbp = blist; nbp != NULL; nbp = nbp->b_next)
		if (bp->b_y == nbp->b_y && bp->b_x == nbp->b_x) {
			bp->b_over = nbp->b_over;
			break;
		}
}

/*
 * mark_player:
 *	mark a player as under a shot
 */
static void
mark_player(BULLET *bp)
{
	PLAYER	*pp;

	for (pp = Player; pp < End_player; pp++)
		if (pp->p_y == bp->b_y && pp->p_x == bp->b_x) {
			pp->p_undershot = TRUE;
			break;
		}
}

/*
 * mark_boot:
 *	mark a boot as under a shot
 */
static void
mark_boot(BULLET *bp)
{
	PLAYER	*pp;

	for (pp = Boot; pp < &Boot[NBOOTS]; pp++)
		if (pp->p_y == bp->b_y && pp->p_x == bp->b_x) {
			pp->p_undershot = TRUE;
			break;
		}
}
