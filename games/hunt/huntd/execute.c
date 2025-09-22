/*	$OpenBSD: execute.c,v 1.14 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: execute.c,v 1.2 1997/10/10 16:33:13 lukem Exp $	*/
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
#include <string.h>
#include <syslog.h>

#include "conf.h"
#include "hunt.h"
#include "server.h"

static void	cloak(PLAYER *);
static void	face(PLAYER *, int);
static void	fire(PLAYER *, int);
static void	fire_slime(PLAYER *, int);
static void	move_player(PLAYER *, int);
static void	pickup(PLAYER *, int, int, int, int);
static void	scan(PLAYER *);


/*
 * mon_execute:
 *	Execute a single monitor command
 */
void
mon_execute(PLAYER *pp)
{
	char	ch;

	ch = pp->p_cbuf[pp->p_ncount++];

	switch (ch) {
	  case CTRL('L'):
		/* Redraw messed-up screen */
		sendcom(pp, REDRAW);
		break;
	  case 'q':
		/* Quit client */
		(void) strlcpy(pp->p_death, "| Quit |", sizeof pp->p_death);
		break;
	  default:
		/* Ignore everything else */
		;
	}
}

/*
 * execute:
 *	Execute a single command from a player
 */
void
execute(PLAYER *pp)
{
	char	ch;

	ch = pp->p_cbuf[pp->p_ncount++];

	/* When flying, only allow refresh and quit. */
	if (pp->p_flying >= 0) {
		switch (ch) {
		  case CTRL('L'):
			sendcom(pp, REDRAW);
			break;
		  case 'q':
			(void) strlcpy(pp->p_death, "| Quit |",
			    sizeof pp->p_death);
			break;
		}
		return;
	}

	/* Decode the command character: */
	switch (ch) {
	  case CTRL('L'):
		sendcom(pp, REDRAW);	/* Refresh */
		break;
	  case 'h':
		move_player(pp, LEFTS); /* Move left */
		break;
	  case 'H':
		face(pp, LEFTS);	/* Face left */
		break;
	  case 'j':
		move_player(pp, BELOW); /* Move down */
		break;
	  case 'J':
		face(pp, BELOW);	/* Face down */
		break;
	  case 'k':
		move_player(pp, ABOVE); /* Move up */
		break;
	  case 'K':
		face(pp, ABOVE);	/* Face up */
		break;
	  case 'l':
		move_player(pp, RIGHT);	/* Move right */
		break;
	  case 'L':
		face(pp, RIGHT);	/* Face right */
		break;
	  case 'f':
	  case '1':
		fire(pp, 0);		/* SHOT */
		break;
	  case 'g':
	  case '2':
		fire(pp, 1);		/* GRENADE */
		break;
	  case 'F':
	  case '3':
		fire(pp, 2);		/* SATCHEL */
		break;
	  case 'G':
	  case '4':
		fire(pp, 3);		/* 7x7 BOMB */
		break;
	  case '5':
		fire(pp, 4);		/* 9x9 BOMB */
		break;
	  case '6':
		fire(pp, 5);		/* 11x11 BOMB */
		break;
	  case '7':
		fire(pp, 6);		/* 13x13 BOMB */
		break;
	  case '8':
		fire(pp, 7);		/* 15x15 BOMB */
		break;
	  case '9':
		fire(pp, 8);		/* 17x17 BOMB */
		break;
	  case '0':
		fire(pp, 9);		/* 19x19 BOMB */
		break;
	  case '@':
		fire(pp, 10);		/* 21x21 BOMB */
		break;
	  case 'o':
		fire_slime(pp, 0);	/* SLIME */
		break;
	  case 'O':
		fire_slime(pp, 1);	/* SSLIME */
		break;
	  case 'p':
		fire_slime(pp, 2);	/* large slime */
		break;
	  case 'P':
		fire_slime(pp, 3);	/* very large slime */
		break;
	  case 's':			/* start scanning */
		scan(pp);
		break;
	  case 'c':			/* start cloaking */
		cloak(pp);
		break;
	  case 'q':			/* quit */
		(void) strlcpy(pp->p_death, "| Quit |", sizeof pp->p_death);
		break;
	}
}

/*
 * move_player:
 *	Try to move player 'pp' in direction 'dir'.
 */
static void
move_player(PLAYER *pp, int dir)
{
	PLAYER	*newp;
	int	x, y;
	FLAG	moved;
	BULLET	*bp;

	y = pp->p_y;
	x = pp->p_x;

	switch (dir) {
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

	moved = FALSE;

	/* What would the player move over: */
	switch (Maze[y][x]) {
	  /* Players can move through spaces and doors, no problem: */
	  case SPACE:
	  case DOOR:
		moved = TRUE;
		break;
	  /* Can't move through walls: */
	  case WALL1:
	  case WALL2:
	  case WALL3:
	  case WALL4:
	  case WALL5:
		break;
	  /* Moving over a mine - try to pick it up: */
	  case MINE:
	  case GMINE:
		if (dir == pp->p_face)
			/* facing it: 2% chance of trip */
			pickup(pp, y, x, conf_ptrip_face, Maze[y][x]);
		else if (opposite(dir, pp->p_face))
			/* facing away: 95% chance of trip */
			pickup(pp, y, x, conf_ptrip_back, Maze[y][x]);
		else
			/* facing sideways: 50% chance of trip */
			pickup(pp, y, x, conf_ptrip_side, Maze[y][x]);
		/* Remove the mine: */
		Maze[y][x] = SPACE;
		moved = TRUE;
		break;
	  /* Moving into a bullet: */
	  case SHOT:
	  case GRENADE:
	  case SATCHEL:
	  case BOMB:
	  case SLIME:
	  case DSHOT:
		/* Find which bullet: */
		bp = is_bullet(y, x);
		if (bp != NULL)
			/* Detonate it: */
			bp->b_expl = TRUE;
		/* Remove it: */
		Maze[y][x] = SPACE;
		moved = TRUE;
		break;
	  /* Moving into another player: */
	  case LEFTS:
	  case RIGHT:
	  case ABOVE:
	  case BELOW:
		if (dir != pp->p_face)
			/* Can't walk backwards/sideways into another player: */
			sendcom(pp, BELL);
		else {
			/* Stab the other player */
			newp = play_at(y, x);
			checkdam(newp, pp, pp->p_ident, conf_stabdam, KNIFE);
		}
		break;
	  /* Moving into a player flying overhead: */
	  case FLYER:
		newp = play_at(y, x);
		message(newp, "Oooh, there's a short guy waving at you!");
		message(pp, "You couldn't quite reach him!");
		break;
	  /* Picking up a boot, or two: */
	  case BOOT_PAIR:
		pp->p_nboots++;
	  case BOOT:
		pp->p_nboots++;
		for (newp = Boot; newp < &Boot[NBOOTS]; newp++) {
			if (newp->p_flying < 0)
				continue;
			if (newp->p_y == y && newp->p_x == x) {
				newp->p_flying = -1;
				if (newp->p_undershot)
					fixshots(y, x, newp->p_over);
			}
		}
		if (pp->p_nboots == 2)
			message(pp, "Wow!  A pair of boots!");
		else
			message(pp, "You can hobble around on one boot.");
		Maze[y][x] = SPACE;
		moved = TRUE;
		break;
	}

	/* Can the player be moved? */
	if (moved) {
		/* Check the gun status: */
		if (pp->p_ncshot > 0)
			if (--pp->p_ncshot == conf_maxncshot)
				outyx(pp, STAT_GUN_ROW, STAT_VALUE_COL, " ok");
		/* Check for bullets flying past: */
		if (pp->p_undershot) {
			fixshots(pp->p_y, pp->p_x, pp->p_over);
			pp->p_undershot = FALSE;
		}
		/* Erase the player: */
		drawplayer(pp, FALSE);
		/* Save under: */
		pp->p_over = Maze[y][x];
		/* Move the player: */
		pp->p_y = y;
		pp->p_x = x;
		/* Draw the player in their new position */
		drawplayer(pp, TRUE);
	}
}

/*
 * face:
 *	Change the direction the player is facing
 */
static void
face(PLAYER *pp, int dir)
{
	if (pp->p_face != dir) {
		pp->p_face = dir;
		drawplayer(pp, TRUE);
	}
}

/*
 * fire:
 *	Fire a shot of the given type in the given direction
 */
static void
fire(PLAYER *pp, int req_index)
{
	if (pp == NULL)
		return;

	/* Drop the shot type down until we can afford it: */
	while (req_index >= 0 && pp->p_ammo < shot_req[req_index])
		req_index--;

	/* Can we shoot at all? */
	if (req_index < 0) {
		message(pp, "Not enough charges.");
		return;
	}

	/* Check if the gun is too hot: */
	if (pp->p_ncshot > conf_maxncshot)
		return;

	/* Heat up the gun: */
	if (pp->p_ncshot++ == conf_maxncshot) {
		/* The gun has overheated: */
		outyx(pp, STAT_GUN_ROW, STAT_VALUE_COL, "   ");
	}

	/* Use up some ammo: */
	pp->p_ammo -= shot_req[req_index];
	ammo_update(pp);

	/* Start the bullet moving: */
	add_shot(shot_type[req_index], pp->p_y, pp->p_x, pp->p_face,
		shot_req[req_index], pp, FALSE, pp->p_face);
	pp->p_undershot = TRUE;

	/* Show the bullet to everyone: */
	showexpl(pp->p_y, pp->p_x, shot_type[req_index]);
	sendcom(ALL_PLAYERS, REFRESH);
}

/*
 * fire_slime:
 *	Fire a slime shot in the given direction
 */
static void
fire_slime(PLAYER *pp, int req_index)
{
	if (pp == NULL)
		return;

	/* Check configuration: */
	if (!conf_ooze)
		return;

	/* Drop the slime type back util we can afford it: */
	while (req_index >= 0 && pp->p_ammo < slime_req[req_index])
		req_index--;

	/* Can we afford to slime at all? */
	if (req_index < 0) {
		message(pp, "Not enough charges.");
		return;
	}

	/* Is the gun too hot? */
	if (pp->p_ncshot > conf_maxncshot)
		return;

	/* Heat up the gun: */
	if (pp->p_ncshot++ == conf_maxncshot) {
		/* The gun has overheated: */
		outyx(pp, STAT_GUN_ROW, STAT_VALUE_COL, "   ");
	}

	/* Use up some ammo: */
	pp->p_ammo -= slime_req[req_index];
	ammo_update(pp);

	/* Start the slime moving: */
	add_shot(SLIME, pp->p_y, pp->p_x, pp->p_face,
		slime_req[req_index] * conf_slimefactor, pp, FALSE, pp->p_face);
	pp->p_undershot = TRUE;

	/* Show the object to everyone: */
	showexpl(pp->p_y, pp->p_x, SLIME);
	sendcom(ALL_PLAYERS, REFRESH);
}

/*
 * add_shot:
 *	Create a shot with the given properties
 */
void
add_shot(int type, int y, int x, char face, int charge, PLAYER *owner,
    int expl, char over)
{
	BULLET	*bp;
	int	size;

	/* Determine the bullet's size based on its type and charge: */
	switch (type) {
	  case SHOT:
	  case MINE:
		size = 1;
		break;
	  case GRENADE:
	  case GMINE:
		size = 2;
		break;
	  case SATCHEL:
		size = 3;
		break;
	  case BOMB:
		for (size = 3; size < MAXBOMB; size++)
			if (shot_req[size] >= charge)
				break;
		size++;
		break;
	  default:
		size = 0;
		break;
	}

	/* Create the bullet: */
	bp = create_shot(type, y, x, face, charge, size, owner,
		(owner == NULL) ? NULL : owner->p_ident, expl, over);

	/* Insert the bullet into the front of the bullet list: */
	bp->b_next = Bullets;
	Bullets = bp;
}

/*
 * create_shot:
 *	allocate storage for an (unlinked) bullet structure;
 *	initialize and return it
 */
BULLET *
create_shot(int type, int y, int x, char face, int charge, int size,
    PLAYER *owner, IDENT *score, int expl, char over)
{
	BULLET	*bp;

	bp = malloc(sizeof (BULLET));
	if (bp == NULL) {
		logit(LOG_ERR, "malloc");
		if (owner != NULL)
			message(owner, "Out of memory");
		return NULL;
	}

	bp->b_face = face;
	bp->b_x = x;
	bp->b_y = y;
	bp->b_charge = charge;
	bp->b_owner = owner;
	bp->b_score = score;
	bp->b_type = type;
	bp->b_size = size;
	bp->b_expl = expl;
	bp->b_over = over;
	bp->b_next = NULL;

	return bp;
}

/*
 * cloak:
 *	Turn on or increase length of a cloak
 */
static void
cloak(PLAYER *pp)
{
	/* Check configuration: */
	if (!conf_cloak)
		return;

	/* Can we afford it?: */
	if (pp->p_ammo <= 0) {
		message(pp, "No more charges");
		return;
	}

	/* Can't cloak with boots: */
	if (pp->p_nboots > 0) {
		message(pp, "Boots are too noisy to cloak!");
		return;
	}

	/* Consume a unit of ammo: */
	pp->p_ammo--;
	ammo_update(pp);

	/* Add to the duration of a cloak: */
	pp->p_cloak += conf_cloaklen;

	/* Disable scan, if enabled: */
	if (pp->p_scan >= 0)
		pp->p_scan = -1;

	/* Re-draw the player's scan/cloak status: */
	showstat(pp);
}

/*
 * scan:
 *	Turn on or increase length of a scan
 */
static void
scan(PLAYER *pp)
{
	/* Check configuration: */
	if (!conf_scan)
		return;

	/* Can we afford it?: */
	if (pp->p_ammo <= 0) {
		message(pp, "No more charges");
		return;
	}

	/* Consume one unit of ammo: */
	pp->p_ammo--;
	ammo_update(pp);

	/* Increase the scan time: */
	pp->p_scan += Nplayer * conf_scanlen;

	/* Disable cloak, if enabled: */
	if (pp->p_cloak >= 0)
		pp->p_cloak = -1;

	/* Re-draw the player's scan/cloak status: */
	showstat(pp);
}

/*
 * pickup:
 *	pick up a mine or grenade, with some probability of it exploding
 */
static void
pickup(PLAYER *pp, int y, int x, int prob, int obj)
{
	int	req;

	/* Figure out how much ammo the player is trying to pick up: */
	switch (obj) {
	  case MINE:
		req = BULREQ;
		break;
	  case GMINE:
		req = GRENREQ;
		break;
	  default:
#ifdef DIAGNOSTIC
		abort();
#endif
		return;
	}

	/* Does it explode? */
	if (rand_num(100) < prob)
		/* Ooooh, unlucky: (Boom) */
		add_shot(obj, y, x, LEFTS, req, (PLAYER *) NULL,
			TRUE, pp->p_face);
	else {
		/* Safely picked it up. Add to player's ammo: */
		pp->p_ammo += req;
		ammo_update(pp);
	}
}

void
ammo_update(PLAYER *pp)
{
	outyx(pp, STAT_AMMO_ROW, STAT_VALUE_COL - 1, "%4d", pp->p_ammo);
}
