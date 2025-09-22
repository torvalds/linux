/*	$OpenBSD: otto.c,v 1.13 2016/01/07 21:37:53 mestre Exp $	*/
/*	$NetBSD: otto.c,v 1.2 1997/10/10 16:32:39 lukem Exp $	*/
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

/*
 *	otto	- a hunt otto-matic player
 *
 *		This guy is buggy, unfair, stupid, and not extensible.
 *	Future versions of hunt will have a subroutine library for
 *	automatic players to link to.  If you write your own "otto"
 *	please let us know what subroutines you would expect in the
 *	subroutine library.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "display.h"
#include "hunt.h"

#define panic(m)	_panic(__FILE__,__LINE__,m)

useconds_t	Otto_pause 	= 55000;

int	Otto_mode;

# undef		WALL
# undef		NORTH
# undef		SOUTH
# undef		WEST
# undef		EAST
# undef		FRONT
# undef		LEFT
# undef		BACK
# undef		RIGHT

# define	SCREEN(y, x)	display_atyx(y, x)

# define	OPPONENT	"{}i!"
# define	PROPONENT	"^v<>"
# define	WALL		"+\\/#*-|"
# define	PUSHOVER	" bg;*#&"
# define	SHOTS		"$@Oo:"

/* number of "directions" */
# define	NUMDIRECTIONS	4
# define	direction(abs,rel)	(((abs) + (rel)) % NUMDIRECTIONS)

/* absolute directions (facings) - counterclockwise */
# define	NORTH		0
# define	WEST		1
# define	SOUTH		2
# define	EAST		3
# define	ALLDIRS		0xf

/* relative directions - counterclockwise */
# define	FRONT		0
# define	LEFT		1
# define	BACK		2
# define	RIGHT		3

# define	ABSCHARS	"NWSE"
# define	RELCHARS	"FLBR"
# define	DIRKEYS		"khjl"

static	char	command[1024];	/* XXX */
static	int	comlen;

# define	DEADEND		0x1
# define	ON_LEFT		0x2
# define	ON_RIGHT	0x4
# define	ON_SIDE		(ON_LEFT|ON_RIGHT)
# define	BEEN		0x8
# define	BEEN_SAME	0x10

struct	item	{
	char	what;
	int	distance;
	int	flags;
};

static	struct	item	flbr[NUMDIRECTIONS];

# define	fitem	flbr[FRONT]
# define	litem	flbr[LEFT]
# define	bitem	flbr[BACK]
# define	ritem	flbr[RIGHT]

static	int		facing;
static	int		row, col;
static	int		num_turns;		/* for wandering */
static	char		been_there[HEIGHT][WIDTH2];

static	void		attack(int, struct item *);
static	void		duck(int);
static	void		face_and_move_direction(int, int);
static	int		go_for_ammo(char);
static	void		ottolook(int, struct item *);
static	void		look_around(void);
static	int		stop_look(struct item *, char, int, int);
static	void		wander(void);
static	void		_panic(const char *, int, const char *);

int
otto(int y, int x, char face, char *buf, size_t buflen)
{
	int		i;

	if (usleep(Otto_pause) < 0)
		panic("usleep");

	/* save away parameters so other functions may use/update info */
	switch (face) {
	case '^':	facing = NORTH; break;
	case '<':	facing = WEST; break;
	case 'v':	facing = SOUTH; break;
	case '>':	facing = EAST; break;
	default:	panic("unknown face");
	}
	row = y; col = x;
	been_there[row][col] |= 1 << facing;

	/* initially no commands to be sent */
	comlen = 0;

	/* find something to do */
	look_around();
	for (i = 0; i < NUMDIRECTIONS; i++) {
		if (strchr(OPPONENT, flbr[i].what) != NULL) {
			attack(i, &flbr[i]);
			memset(been_there, 0, sizeof been_there);
			goto done;
		}
	}

	if (strchr(SHOTS, bitem.what) != NULL && !(bitem.what & ON_SIDE)) {
		duck(BACK);
		memset(been_there, 0, sizeof been_there);
	} else if (go_for_ammo(BOOT_PAIR)) {
		memset(been_there, 0, sizeof been_there);
	} else if (go_for_ammo(BOOT)) {
		memset(been_there, 0, sizeof been_there);
	} else if (go_for_ammo(GMINE))
		memset(been_there, 0, sizeof been_there);
	else if (go_for_ammo(MINE))
		memset(been_there, 0, sizeof been_there);
	else
		wander();

done:
	if (comlen) {
		if (comlen > buflen)
			panic("not enough buffer space");
		memcpy(buf, command, comlen);
	}
	return comlen;
}

static int
stop_look(struct item *itemp, char c, int dist, int side)
{
	switch (c) {

	case SPACE:
		if (side)
			itemp->flags &= ~DEADEND;
		return 0;

	case MINE:
	case GMINE:
	case BOOT:
	case BOOT_PAIR:
		if (itemp->distance == -1) {
			itemp->distance = dist;
			itemp->what = c;
			if (side < 0)
				itemp->flags |= ON_LEFT;
			else if (side > 0)
				itemp->flags |= ON_RIGHT;
		}
		return 0;

	case SHOT:
	case GRENADE:
	case SATCHEL:
	case BOMB:
	case SLIME:
		if (itemp->distance == -1 || (!side
		    && (itemp->flags & ON_SIDE
		    || itemp->what == GMINE || itemp->what == MINE))) {
			itemp->distance = dist;
			itemp->what = c;
			itemp->flags &= ~ON_SIDE;
			if (side < 0)
				itemp->flags |= ON_LEFT;
			else if (side > 0)
				itemp->flags |= ON_RIGHT;
		}
		return 0;

	case '{':
	case '}':
	case 'i':
	case '!':
		itemp->distance = dist;
		itemp->what = c;
		itemp->flags &= ~(ON_SIDE|DEADEND);
		if (side < 0)
			itemp->flags |= ON_LEFT;
		else if (side > 0)
			itemp->flags |= ON_RIGHT;
		return 1;

	default:
		/* a wall or unknown object */
		if (side)
			return 0;
		if (itemp->distance == -1) {
			itemp->distance = dist;
			itemp->what = c;
		}
		return 1;
	}
}

static void
ottolook(int rel_dir, struct item *itemp)
{
	int		r, c;
	char		ch;

	r = 0;
	itemp->what = 0;
	itemp->distance = -1;
	itemp->flags = DEADEND|BEEN;		/* true until proven false */

	switch (direction(facing, rel_dir)) {

	case NORTH:
		if (been_there[row - 1][col] & NORTH)
			itemp->flags |= BEEN_SAME;
		for (r = row - 1; r >= 0; r--)
			for (c = col - 1; c < col + 2; c++) {
				ch = SCREEN(r, c);
				if (stop_look(itemp, ch, row - r, c - col))
					goto cont_north;
				if (c == col && !been_there[r][c])
					itemp->flags &= ~BEEN;
			}
	cont_north:
		if (itemp->flags & DEADEND) {
			itemp->flags |= BEEN;
			if (r >= 0)
				been_there[r][col] |= NORTH;
			for (r = row - 1; r > row - itemp->distance; r--)
				been_there[r][col] = ALLDIRS;
		}
		break;

	case SOUTH:
		if (been_there[row + 1][col] & SOUTH)
			itemp->flags |= BEEN_SAME;
		for (r = row + 1; r < HEIGHT; r++)
			for (c = col - 1; c < col + 2; c++) {
				ch = SCREEN(r, c);
				if (stop_look(itemp, ch, r - row, col - c))
					goto cont_south;
				if (c == col && !been_there[r][c])
					itemp->flags &= ~BEEN;
			}
	cont_south:
		if (itemp->flags & DEADEND) {
			itemp->flags |= BEEN;
			if (r < HEIGHT)
				been_there[r][col] |= SOUTH;
			for (r = row + 1; r < row + itemp->distance; r++)
				been_there[r][col] = ALLDIRS;
		}
		break;

	case WEST:
		if (been_there[row][col - 1] & WEST)
			itemp->flags |= BEEN_SAME;
		for (c = col - 1; c >= 0; c--)
			for (r = row - 1; r < row + 2; r++) {
				ch = SCREEN(r, c);
				if (stop_look(itemp, ch, col - c, row - r))
					goto cont_west;
				if (r == row && !been_there[r][c])
					itemp->flags &= ~BEEN;
			}
	cont_west:
		if (itemp->flags & DEADEND) {
			itemp->flags |= BEEN;
			been_there[r][col] |= WEST;
			for (c = col - 1; c > col - itemp->distance; c--)
				been_there[row][c] = ALLDIRS;
		}
		break;

	case EAST:
		if (been_there[row][col + 1] & EAST)
			itemp->flags |= BEEN_SAME;
		for (c = col + 1; c < WIDTH; c++)
			for (r = row - 1; r < row + 2; r++) {
				ch = SCREEN(r, c);
				if (stop_look(itemp, ch, c - col, r - row))
					goto cont_east;
				if (r == row && !been_there[r][c])
					itemp->flags &= ~BEEN;
			}
	cont_east:
		if (itemp->flags & DEADEND) {
			itemp->flags |= BEEN;
			been_there[r][col] |= EAST;
			for (c = col + 1; c < col + itemp->distance; c++)
				been_there[row][c] = ALLDIRS;
		}
		break;

	default:
		panic("unknown look");
	}
}

static void
look_around(void)
{
	int	i;

	for (i = 0; i < NUMDIRECTIONS; i++) {
		ottolook(i, &flbr[i]);
	}
}

/*
 *	as a side effect modifies facing and location (row, col)
 */

static void
face_and_move_direction(int rel_dir, int distance)
{
	int	old_facing;
	char	cmd;

	old_facing = facing;
	cmd = DIRKEYS[facing = direction(facing, rel_dir)];

	if (rel_dir != FRONT) {
		int	i;
		struct	item	items[NUMDIRECTIONS];

		command[comlen++] = toupper((unsigned char)cmd);
		if (distance == 0) {
			/* rotate ottolook's to be in right position */
			for (i = 0; i < NUMDIRECTIONS; i++)
				items[i] =
					flbr[(i + old_facing) % NUMDIRECTIONS];
			memcpy(flbr, items, sizeof flbr);
		}
	}
	while (distance--) {
		command[comlen++] = cmd;
		switch (facing) {

		case NORTH:	row--; break;
		case WEST:	col--; break;
		case SOUTH:	row++; break;
		case EAST:	col++; break;
		}
		if (distance == 0)
			look_around();
	}
}

static void
attack(int rel_dir, struct item *itemp)
{
	if (!(itemp->flags & ON_SIDE)) {
		face_and_move_direction(rel_dir, 0);
		command[comlen++] = 'o';
		command[comlen++] = 'o';
		duck(FRONT);
		command[comlen++] = ' ';
	} else if (itemp->distance > 1) {
		face_and_move_direction(rel_dir, 2);
		duck(FRONT);
	} else {
		face_and_move_direction(rel_dir, 1);
		if (itemp->flags & ON_LEFT)
			rel_dir = LEFT;
		else
			rel_dir = RIGHT;
		(void) face_and_move_direction(rel_dir, 0);
		command[comlen++] = 'f';
		command[comlen++] = 'f';
		duck(FRONT);
		command[comlen++] = ' ';
	}
}

static void
duck(int rel_dir)
{
	int	dir;

	switch (dir = direction(facing, rel_dir)) {

	case NORTH:
	case SOUTH:
		if (strchr(PUSHOVER, SCREEN(row, col - 1)) != NULL)
			command[comlen++] = 'h';
		else if (strchr(PUSHOVER, SCREEN(row, col + 1)) != NULL)
			command[comlen++] = 'l';
		else if (dir == NORTH
			&& strchr(PUSHOVER, SCREEN(row + 1, col)) != NULL)
				command[comlen++] = 'j';
		else if (dir == SOUTH
			&& strchr(PUSHOVER, SCREEN(row - 1, col)) != NULL)
				command[comlen++] = 'k';
		else if (dir == NORTH)
			command[comlen++] = 'k';
		else
			command[comlen++] = 'j';
		break;

	case WEST:
	case EAST:
		if (strchr(PUSHOVER, SCREEN(row - 1, col)) != NULL)
			command[comlen++] = 'k';
		else if (strchr(PUSHOVER, SCREEN(row + 1, col)) != NULL)
			command[comlen++] = 'j';
		else if (dir == WEST
			&& strchr(PUSHOVER, SCREEN(row, col + 1)) != NULL)
				command[comlen++] = 'l';
		else if (dir == EAST
			&& strchr(PUSHOVER, SCREEN(row, col - 1)) != NULL)
				command[comlen++] = 'h';
		else if (dir == WEST)
			command[comlen++] = 'h';
		else
			command[comlen++] = 'l';
		break;
	}
}

/*
 *	go for the closest mine if possible
 */

static int
go_for_ammo(char mine)
{
	int	i, rel_dir, dist;

	rel_dir = -1;
	dist = WIDTH;
	for (i = 0; i < NUMDIRECTIONS; i++) {
		if (flbr[i].what == mine && flbr[i].distance < dist) {
			rel_dir = i;
			dist = flbr[i].distance;
		}
	}
	if (rel_dir == -1)
		return FALSE;

	if (!(flbr[rel_dir].flags & ON_SIDE)
	|| flbr[rel_dir].distance > 1) {
		if (dist > 4)
			dist = 4;
		face_and_move_direction(rel_dir, dist);
	} else
		return FALSE;		/* until it's done right */
	return TRUE;
}

static void
wander(void)
{
	int	i, j, rel_dir, dir_mask, dir_count;

	for (i = 0; i < NUMDIRECTIONS; i++)
		if (!(flbr[i].flags & BEEN) || flbr[i].distance <= 1)
			break;
	if (i == NUMDIRECTIONS)
		memset(been_there, 0, sizeof been_there);
	dir_mask = dir_count = 0;
	for (i = 0; i < NUMDIRECTIONS; i++) {
		j = (RIGHT + i) % NUMDIRECTIONS;
		if (flbr[j].distance <= 1 || flbr[j].flags & DEADEND)
			continue;
		if (!(flbr[j].flags & BEEN_SAME)) {
			dir_mask = 1 << j;
			dir_count = 1;
			break;
		}
		if (j == FRONT
		&& num_turns > 4 + (arc4random_uniform(
				((flbr[FRONT].flags & BEEN) ? 7 : HEIGHT))))
			continue;
		dir_mask |= 1 << j;
		dir_count = 1;
		break;
	}
	if (dir_count == 0) {
		duck(arc4random_uniform(NUMDIRECTIONS));
		num_turns = 0;
		return;
	} else {
		rel_dir = ffs(dir_mask) - 1;
	}
	if (rel_dir == FRONT)
		num_turns++;
	else
		num_turns = 0;

	face_and_move_direction(rel_dir, 1);
}

/* Otto always re-enters the game, cloaked. */
int
otto_quit(int old_status)
{
	return Q_CLOAK;
}

static void
_panic(const char *file, int line, const char *msg)
{

	fprintf(stderr, "%s:%d: panic! %s\n", file, line, msg);
	abort();
}
