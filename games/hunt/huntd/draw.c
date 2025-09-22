/*	$OpenBSD: draw.c,v 1.11 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: draw.c,v 1.2 1997/10/10 16:33:04 lukem Exp $	*/
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
#include <string.h>

#include "conf.h"
#include "hunt.h"
#include "server.h"

static char	translate(char);
static int	player_sym(PLAYER *, int, int);
static void	drawstatus(PLAYER *);
static void	see(PLAYER *, int);

/*
 * drawmaze:
 *	Draw the entire maze on a player's screen.
 */
void
drawmaze(PLAYER *pp)
{
	int	x;
	char	*sp;
	int	y;
	char	*endp;

	/* Clear the client's screen: */
	clrscr(pp);
	/* Draw the top row of the maze: */
	outstr(pp, pp->p_maze[0], WIDTH);
	for (y = 1; y < HEIGHT - 1; y++) {
		endp = &pp->p_maze[y][WIDTH];
		for (x = 0, sp = pp->p_maze[y]; sp < endp; x++, sp++)
			if (*sp != SPACE) {
				cgoto(pp, y, x);
				/* Draw the player as themselves */
				if (pp->p_x == x && pp->p_y == y)
					outch(pp, translate(*sp));
				/* Possibly draw other players as team nrs */
				else if (is_player(*sp))
					outch(pp, player_sym(pp, y, x));
				else
					outch(pp, *sp);
			}
	}
	/* Draw the last row of the maze: */
	cgoto(pp, HEIGHT - 1, 0);
	outstr(pp, pp->p_maze[HEIGHT - 1], WIDTH);
	drawstatus(pp);
}

/*
 * drawstatus - put up the status lines (this assumes the screen
 *		size is 80x24 with the maze being 64x24)
 */
static void
drawstatus(PLAYER *pp)
{
	int	i;
	PLAYER	*np;

	outyx(pp, STAT_AMMO_ROW, STAT_LABEL_COL, "Ammo:");
	ammo_update(pp);

	outyx(pp, STAT_GUN_ROW,  STAT_LABEL_COL, "Gun:");
	outyx(pp, STAT_GUN_ROW,  STAT_VALUE_COL, "%3s",
		(pp->p_ncshot < conf_maxncshot) ? "ok" : "");

	outyx(pp, STAT_DAM_ROW,  STAT_LABEL_COL, "Damage:");
	outyx(pp, STAT_DAM_ROW,  STAT_VALUE_COL, "%2d/%2d",
		pp->p_damage, pp->p_damcap);

	outyx(pp, STAT_KILL_ROW, STAT_LABEL_COL, "Kills:");
	outyx(pp, STAT_KILL_ROW, STAT_VALUE_COL, "%3d",
		(pp->p_damcap - conf_maxdam) / 2);

	outyx(pp, STAT_PLAY_ROW, STAT_LABEL_COL, "Player:");
	for (i = STAT_PLAY_ROW + 1, np = Player; np < End_player; np++, i++) {
		outyx(pp, i, STAT_NAME_COL, "%5.2f%c%-10.10s %c",
			np->p_ident->i_score, stat_char(np),
			np->p_ident->i_name, np->p_ident->i_team);
	}

	outyx(pp, STAT_MON_ROW, STAT_LABEL_COL, "Monitor:");
	for (i = STAT_MON_ROW + 1, np = Monitor; np < End_monitor; np++, i++) {
		outyx(pp, i++, STAT_NAME_COL, "%5.5s %-10.10s %c",
			" ", np->p_ident->i_name, np->p_ident->i_team);
	}
}

/*
 * look
 *	check and update the visible area around the player
 */
void
look(PLAYER *pp)
{
	int	x, y;

	x = pp->p_x;
	y = pp->p_y;

	/*
	 * The player is aware of all objects immediately adjacent to
	 * their position:
	 */
	check(pp, y - 1, x - 1);
	check(pp, y - 1, x    );
	check(pp, y - 1, x + 1);
	check(pp, y    , x - 1);
	check(pp, y    , x    );
	check(pp, y    , x + 1);
	check(pp, y + 1, x - 1);
	check(pp, y + 1, x    );
	check(pp, y + 1, x + 1);

	switch (pp->p_face) {
	  /* The player can see down corridors in directions except behind: */
	  case LEFTS:
		see(pp, LEFTS);
		see(pp, ABOVE);
		see(pp, BELOW);
		break;
	  case RIGHT:
		see(pp, RIGHT);
		see(pp, ABOVE);
		see(pp, BELOW);
		break;
	  case ABOVE:
		see(pp, ABOVE);
		see(pp, LEFTS);
		see(pp, RIGHT);
		break;
	  case BELOW:
		see(pp, BELOW);
		see(pp, LEFTS);
		see(pp, RIGHT);
		break;
	  /* But they don't see too far when they are flying about: */
	  case FLYER:
		break;
	}

	/* Move the cursor back over the player: */
	cgoto(pp, y, x);
}

/*
 * see
 *	Look down a corridor, or towards an open space. This
 *	is a simulation of visibility from the player's perspective.
 */
static void
see(PLAYER *pp, int face)
{
	char	*sp;
	int	y, x;

	/* Start from the player's position: */
	x = pp->p_x;
	y = pp->p_y;

	#define seewalk(dx, dy)						\
		x += (dx);						\
		y += (dy);						\
		sp = &Maze[y][x];					\
		while (See_over[(int)*sp]) {				\
			x += (dx);					\
			y += (dy);					\
			sp += ((dx) + (dy) * sizeof Maze[0]);		\
			check(pp, y + dx, x + dy);			\
			check(pp, y, x);				\
			check(pp, y - dx, x - dy);			\
		}

	switch (face) {
	  case LEFTS:
		seewalk(-1, 0); break;
	  case RIGHT:
		seewalk(1, 0); break;
	  case ABOVE:
		seewalk(0, -1); break;
	  case BELOW:
		seewalk(0, 1); break;
	}
}

/*
 * check
 *	The player is aware of a cell in the maze.
 *	Ensure it is shown properly on their screen.
 */
void
check(PLAYER *pp, int y, int x)
{
	int	index;
	int	ch;
	PLAYER	*rpp;

	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			check(pp, y, x);
		for (pp = Monitor; pp < End_monitor; pp++)
			check(pp, y, x);
		return;
	}

	index = y * sizeof Maze[0] + x;
	ch = ((char *) Maze)[index];
	if (ch != ((char *) pp->p_maze)[index]) {
		rpp = pp;
		cgoto(rpp, y, x);
		if (x == rpp->p_x && y == rpp->p_y)
			outch(rpp, translate(ch));
		else if (is_player(ch))
			outch(rpp, player_sym(rpp, y, x));
		else
			outch(rpp, ch);
		((char *) rpp->p_maze)[index] = ch;
	}
}

/*
 * showstat
 *	Update the status of a player on everyone's screen
 */
void
showstat(PLAYER *pp)
{

	outyx(ALL_PLAYERS,
		STAT_PLAY_ROW + 1 + (pp - Player), STAT_SCAN_COL,
		"%c", stat_char(pp));
}

/*
 * drawplayer:
 *	Draw the player on the screen and show him to everyone who's scanning
 *	unless he is cloaked.
 *	The 'draw' flag when false, causes the 'saved under' character to
 *	be drawn instead of the player; effectively un-drawing the player.
 */
void
drawplayer(PLAYER *pp, FLAG draw)
{
	PLAYER	*newp;
	int	x, y;

	x = pp->p_x;
	y = pp->p_y;

	/* Draw or un-draw the player into the master map: */
	Maze[y][x] = draw ? pp->p_face : pp->p_over;

	/* The monitors can always see this player: */
	for (newp = Monitor; newp < End_monitor; newp++)
		check(newp, y, x);

	/* Check if other players can see this player: */
	for (newp = Player; newp < End_player; newp++) {
		if (!draw) {
			/* When un-drawing, show everyone what was under */
			check(newp, y, x);
			continue;
		}
		if (newp == pp) {
			/* The player can always see themselves: */
			check(newp, y, x);
			continue;
		}
		/* Check if the other player just run out of scans */
		if (newp->p_scan == 0) {
			/* The other player is no longer scanning: */
			newp->p_scan--;
			showstat(newp);
		/* Check if the other play is scannning */
		} else if (newp->p_scan > 0) {
			/* If this player's not cloacked, draw him: */
			if (pp->p_cloak < 0)
				check(newp, y, x);
			/* And this uses up a scan. */
			newp->p_scan--;
		}
	}

	/* Use up one point of cloak time when drawing: */
	if (draw && pp->p_cloak >= 0) {
		pp->p_cloak--;
		/* Check if we ran out of cloak: */
		if (pp->p_cloak < 0)
			showstat(pp);
	}
}

/*
 * message:
 *	Write a message at the bottom of the screen.
 */
void
message(PLAYER *pp, char *s)
{
	cgoto(pp, HEIGHT, 0);
	outstr(pp, s, strlen(s));
	ce(pp);
}

/*
 * translate:
 *	Turn a player character into a more personal player character.
 *	ie: {,},!,i becomes <,>,v,^
 */
static char
translate(char ch)
{
	switch (ch) {
	  case LEFTS:
		return '<';
	  case RIGHT:
		return '>';
	  case ABOVE:
		return '^';
	  case BELOW:
		return 'v';
	}
	return ch;
}

/*
 * player_sym:
 *	Return the symbol for the player at (y,x) when viewed by player 'pp'.
 *	ie: - unteamed players appear as {,},!,i
 *	    - unteamed monitors see all players as team digits
 *	    - teamed players see other players on their team, as a digit
 */
static int
player_sym(PLAYER *pp, int y, int x)
{
	PLAYER	*npp;

	npp = play_at(y, x);
	if (npp->p_ident->i_team == ' ')
		return Maze[y][x];
	if (pp->p_ident->i_team == '*')
		return npp->p_ident->i_team;
	if (pp->p_ident->i_team != npp->p_ident->i_team)
		return Maze[y][x];
	return pp->p_ident->i_team;
}
