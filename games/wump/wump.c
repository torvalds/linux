/*	$OpenBSD: wump.c,v 1.34 2018/12/20 09:55:44 schwarze Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Dave Taylor, of Intuitive Systems.
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
 * A no longer new version of the age-old favorite Hunt-The-Wumpus game that
 * has been a part of the BSD distribution for longer than us old folk
 * would care to remember.
 */

#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"

/* some defines to spec out what our wumpus cave should look like */

/* #define	MAX_ARROW_SHOT_DISTANCE	6	*/	/* +1 for '0' stopper */
#define	MAX_LINKS_IN_ROOM	25		/* a complex cave */

#define	MAX_ROOMS_IN_CAVE	250
#define	ROOMS_IN_CAVE		20
#define	MIN_ROOMS_IN_CAVE	10

#define	LINKS_IN_ROOM		3
#define	NUMBER_OF_ARROWS	5
#define	PIT_COUNT		3
#define	BAT_COUNT		3

#define	EASY			1		/* levels of play */
#define	HARD			2

/* some macro definitions for cleaner output */

#define	plural(n)	(n == 1 ? "" : "s")

/* simple cave data structure; +1 so we can index from '1' not '0' */
struct room_record {
	int tunnel[MAX_LINKS_IN_ROOM];
	int has_a_pit, has_a_bat;
} cave[MAX_ROOMS_IN_CAVE+1];

/*
 * global variables so we can keep track of where the player is, how
 * many arrows they still have, where el wumpo is, and so on...
 */
int player_loc = -1;			/* player location */
int wumpus_loc = -1;			/* The Bad Guy location */
int level = EASY;			/* level of play */
int arrows_left;			/* arrows unshot */
int oldstyle = 0;			/* dodecahedral cave? */

#ifdef DEBUG
int debug = 0;
#endif

int pit_num = -1;		/* # pits in cave */
int bat_num = -1;		/* # bats */
int room_num = ROOMS_IN_CAVE;		/* # rooms in cave */
int link_num = LINKS_IN_ROOM;		/* links per room  */
int arrow_num = NUMBER_OF_ARROWS;	/* arrow inventory */

char answer[20];			/* user input */

int	bats_nearby(void);
void	cave_init(void);
void	clear_things_in_cave(void);
void	display_room_stats(void);
void	dodecahedral_cave_init(void);
int	gcd(int, int);
int	getans(const char *);
void	initialize_things_in_cave(void);
void	instructions(void);
int	int_compare(const void *, const void *);
/* void	jump(int); */
void	kill_wump(void);
int	main(int, char **);
int	move_to(const char *);
void	move_wump(void);
void	no_arrows(void);
void	pit_kill(void);
void	pit_kill_bat(void);
int	pit_nearby(void);
void	pit_survive(void);
int	shoot(char *);
void	shoot_self(void);
int	take_action(void);
__dead void	usage(void);
void	wump_kill(void);
void	wump_bat_kill(void);
void	wump_walk_kill(void);
int	wump_nearby(void);


int
main(int argc, char *argv[])
{
	int c;

	if (pledge("stdio rpath proc exec", NULL) == -1)
		err(1, "pledge");

#ifdef DEBUG
	while ((c = getopt(argc, argv, "a:b:hop:r:t:d")) != -1)
#else
	while ((c = getopt(argc, argv, "a:b:hop:r:t:")) != -1)
#endif
		switch (c) {
		case 'a':
			arrow_num = atoi(optarg);
			break;
		case 'b':
			bat_num = atoi(optarg);
			break;
#ifdef DEBUG
		case 'd':
			debug = 1;
			break;
#endif
		case 'h':
			level = HARD;
			break;
		case 'o':
			oldstyle = 1;
			break;
		case 'p':
			pit_num = atoi(optarg);
			break;
		case 'r':
			room_num = atoi(optarg);
			if (room_num < MIN_ROOMS_IN_CAVE)
				errx(1,
	"no self-respecting wumpus would live in such a small cave!");
			if (room_num > MAX_ROOMS_IN_CAVE)
				errx(1,
	"even wumpii can't furnish caves that large!");
			break;
		case 't':
			link_num = atoi(optarg);
			if (link_num < 2)
				errx(1,
	"wumpii like extra doors in their caves!");
			break;
		default:
			usage();
	}

	if (oldstyle) {
		room_num = 20;
		link_num = 3;
		/* Original game had exactly 2 bats and 2 pits */
		if (bat_num < 0)
			bat_num = 2;
		if (pit_num < 0)
			pit_num = 2;
	} else {
		if (bat_num < 0)
			bat_num = BAT_COUNT;
		if (pit_num < 0)
			pit_num = PIT_COUNT;
	}

	if (link_num > MAX_LINKS_IN_ROOM ||
	    link_num > room_num - (room_num / 4))
		errx(1,
"too many tunnels!  The cave collapsed!\n(Fortunately, the wumpus escaped!)");

	if (level == HARD) {
		if (room_num / 2 - bat_num)
			bat_num += arc4random_uniform(room_num / 2 - bat_num);
		if (room_num / 2 - pit_num)
			pit_num += arc4random_uniform(room_num / 2 - pit_num);
	}

	/* Leave at least two rooms free--one for the player to start in, and
	 * potentially one for the wumpus.
	 */
	if (bat_num > room_num / 2 - 1)
		errx(1,
"the wumpus refused to enter the cave, claiming it was too crowded!");

	if (pit_num > room_num / 2 - 1)
		errx(1,
"the wumpus refused to enter the cave, claiming it was too dangerous!");

	instructions();

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (oldstyle)
		dodecahedral_cave_init();
	else
		cave_init();

	/* and we're OFF!  da dum, da dum, da dum, da dum... */
	(void)printf(
"\nYou're in a cave with %d rooms and %d tunnels leading from each room.\n\
There are %d bat%s and %d pit%s scattered throughout the cave, and your\n\
quiver holds %d custom super anti-evil Wumpus arrows.  Good luck.\n",
	    room_num, link_num, bat_num, plural(bat_num), pit_num,
	    plural(pit_num), arrow_num);

	for (;;) {
		initialize_things_in_cave();
		arrows_left = arrow_num;
		do {
			display_room_stats();
			(void)printf("Move or shoot? (m-s) ");
			(void)fflush(stdout);
			(void)fpurge(stdin);
			if (!fgets(answer, sizeof(answer), stdin))
				break;
		} while (!take_action());
		(void)fpurge(stdin);

		if (!getans("\nCare to play another game? (y-n) ")) {
			(void)printf("\n");
			return 0;
		}
		clear_things_in_cave();
		if (!getans("In the same cave? (y-n) ")) {
			if (oldstyle)
				dodecahedral_cave_init();
			else
				cave_init();
		}
	}
}

void
display_room_stats(void)
{
	int i;

	/*
	 * Routine will explain what's going on with the current room, as well
	 * as describe whether there are pits, bats, & wumpii nearby.  It's
	 * all pretty mindless, really.
	 */
	(void)printf(
"\nYou are in room %d of the cave, and have %d arrow%s left.\n",
	    player_loc, arrows_left, plural(arrows_left));

	if (bats_nearby())
		(void)printf("*rustle* *rustle* (must be bats nearby)\n");
	if (pit_nearby())
		(void)printf("*whoosh* (I feel a draft from some pits).\n");
	if (wump_nearby())
		(void)printf("*sniff* (I can smell the evil Wumpus nearby!)\n");

	(void)printf("There are tunnels to rooms %d, ",
	   cave[player_loc].tunnel[0]);

	for (i = 1; i < link_num - 1; i++)
/*		if (cave[player_loc].tunnel[i] <= room_num) */
			(void)printf("%d, ", cave[player_loc].tunnel[i]);
	(void)printf("and %d.\n", cave[player_loc].tunnel[link_num - 1]);
}

int
take_action(void)
{
	/*
	 * Do the action specified by the player, either 'm'ove, 's'hoot
	 * or something exceptionally bizarre and strange!  Returns 1
	 * iff the player died during this turn, otherwise returns 0.
	 */
	switch (*answer) {
		case 'M':
		case 'm':			/* move */
			return(move_to(answer + 1));
		case 'S':
		case 's':			/* shoot */
			return(shoot(answer + 1));
		case 'Q':
		case 'q':
		case 'x':
			exit(0);
		case '\n':
			return(0);
		}
	if (arc4random_uniform(15) == 1)
		(void)printf("Que pasa?\n");
	else
		(void)printf("I don't understand!\n");
	return(0);
}

int
move_to(const char *room_number)
{
	int i, just_moved_by_bats, next_room, tunnel_available;

	/*
	 * This is responsible for moving the player into another room in the
	 * cave as per their directions.  If room_number is a null string,
	 * then we'll prompt the user for the next room to go into.   Once
	 * we've moved into the room, we'll check for things like bats, pits,
	 * and so on.  This routine returns 1 if something occurs that kills
	 * the player and 0 otherwise...
	 */
	tunnel_available = just_moved_by_bats = 0;
	next_room = atoi(room_number);

	/* crap for magic tunnels */
/*	if (next_room == room_num + 1 &&
 *	    cave[player_loc].tunnel[link_num-1] != next_room)
 *		++next_room;
 */
	while (next_room < 1 || next_room > room_num /* + 1 */) {
		if (next_room < 0 && next_room != -1)
(void)printf("Sorry, but we're constrained to a semi-Euclidean cave!\n");
		if (next_room > room_num /* + 1 */)
(void)printf("What?  The cave surely isn't quite that big!\n");
/*		if (next_room == room_num + 1 &&
 *		    cave[player_loc].tunnel[link_num-1] != next_room) {
 *			(void)printf("What?  The cave isn't that big!\n");
 *			++next_room;
 *		}
 */		(void)printf("To which room do you wish to move? ");
		(void)fflush(stdout);
		if (!fgets(answer, sizeof(answer), stdin))
			return(1);
		next_room = atoi(answer);
	}

	/* now let's see if we can move to that room or not */
	tunnel_available = 0;
	for (i = 0; i < link_num; i++)
		if (cave[player_loc].tunnel[i] == next_room)
			tunnel_available = 1;

	if (!tunnel_available) {
		(void)printf("*Oof!*  (You hit the wall)\n");
		if (arc4random_uniform(6) == 1) {
(void)printf("Your colorful comments awaken the wumpus!\n");
			move_wump();
			if (wumpus_loc == player_loc) {
				wump_walk_kill();
				return(1);
			}
		}
		return(0);
	}

	/* now let's move into that room and check it out for dangers */
/*	if (next_room == room_num + 1)
 *		jump(next_room = arc4random_uniform(room_num) + 1);
 */
	player_loc = next_room;
	for (;;) {
		if (next_room == wumpus_loc) {		/* uh oh... */
			if (just_moved_by_bats)
				wump_bat_kill();
			else
				wump_kill();
			return(1);
		}
		if (cave[next_room].has_a_pit) {
			if (arc4random_uniform(12) < 2) {
				pit_survive();
				return(0);
			} else {
				if (just_moved_by_bats)
					pit_kill_bat();
				else
					pit_kill();
				return(1);
			}
		}

		if (cave[next_room].has_a_bat) {
			(void)printf(
"*flap*  *flap*  *flap*  (humongous bats pick you up and move you%s!)\n",
			    just_moved_by_bats ? " again": "");
			next_room = player_loc =
			    arc4random_uniform(room_num) + 1;
			just_moved_by_bats = 1;
		}

		else
			break;
	}
	return(0);
}

int
shoot(char *room_list)
{
	int chance, next, roomcnt;
	int j, arrow_location, link, ok;
	char *p;

	/*
	 * Implement shooting arrows.  Arrows are shot by the player indicating
	 * a space-separated list of rooms that the arrow should pass through;
	 * if any of the rooms they specify are not accessible via tunnel from
	 * the room the arrow is in, it will instead fly randomly into another
	 * room.  If the player hits the wumpus, this routine will indicate
	 * such.  If it misses, this routine may *move* the wumpus one room.
	 * If it's the last arrow, then the player dies...  Returns 1 if the
	 * player has won or died, 0 if nothing has happened.
	 */
	arrow_location = player_loc;
	for (roomcnt = 1;; ++roomcnt, room_list = NULL) {
		if (!(p = strtok(room_list, " \t\n"))) {
			if (roomcnt == 1) {
				(void)printf("Enter a list of rooms to shoot into:\n");
				(void)fflush(stdout);
				if (!(p = strtok(fgets(answer, sizeof(answer), stdin),
							" \t\n"))) {
					(void)printf(
				"The arrow falls to the ground at your feet.\n");
					return(0);
					}
			} else
				break;
		}
		if (roomcnt > 5) {
			(void)printf(
"The arrow wavers in its flight and can go no further than room %d!\n",
					arrow_location);
			break;
		}

		next = atoi(p);
		if (next == 0)
			break;	/* Old wumpus used room 0 as the terminator */

		chance = arc4random_uniform(10);
		if (roomcnt == 4 && chance < 2) {
			(void)printf(
"Your finger slips on the bowstring!  *twaaaaaang*\n\
The arrow is weakly shot and can go no further than room %d!\n",arrow_location);
			break;
		} else if (roomcnt == 5 && chance < 6) {
			(void)printf(
"The arrow wavers in its flight and can go no further than room %d!\n",
					arrow_location);
			break;
		}

		for (j = 0, ok = 0; j < link_num; j++)
			if (cave[arrow_location].tunnel[j] == next)
				ok = 1;

		if (ok) {
/*			if (next > room_num) {
 *				(void)printf(
 * "A faint gleam tells you the arrow has gone through a magic tunnel!\n");
 *				arrow_location =
 *				    arc4random_uniform(room_num) + 1;
 *			} else
 */				arrow_location = next;
		} else {
			link = (arc4random_uniform(link_num));
			if (cave[arrow_location].tunnel[link] == player_loc)
				(void)printf(
"*thunk*  The arrow can't find a way from %d to %d and flies back into\n\
your room!\n",
				    arrow_location, next);
/*			else if (cave[arrow_location].tunnel[link] > room_num)
 *				(void)printf(
 *"*thunk*  The arrow flies randomly into a magic tunnel, thence into\n\
 *room %d!\n",
 *				    cave[arrow_location].tunnel[link]);
 */			else
				(void)printf(
"*thunk*  The arrow can't find a way from %d to %d and flies randomly\n\
into room %d!\n", arrow_location, next, cave[arrow_location].tunnel[link]);

			arrow_location = cave[arrow_location].tunnel[link];
		}

		/*
		 * now we've gotten into the new room let us see if El Wumpo is
		 * in the same room ... if so we've a HIT and the player WON!
		 */
		if (arrow_location == wumpus_loc) {
			kill_wump();
			return(1);
		}

		if (arrow_location == player_loc) {
			shoot_self();
			return(1);
		}
	}

	if (!--arrows_left) {
		no_arrows();
		return(1);
	}

	{
		/* each time you shoot, it's more likely the wumpus moves */
		static int lastchance = 2;

		lastchance += 2;
		if (arc4random_uniform(level == EASY ? 12 : 9) < lastchance) {
			move_wump();
			if (wumpus_loc == player_loc) {
				wump_walk_kill();
				/* Reset for next game */
				lastchance = arc4random_uniform(3);
				return(1);
			}

		}
	}
	(void)printf("The arrow hit nothing.\n");
	return(0);
}

int
gcd(int a, int b)
{
	int r;

	if (!(r = (a % b)))
		return(b);
	return(gcd(b, r));
}

void
cave_init(void)
{
	int i, j, k, link;
	int delta;

	/*
	 * This does most of the interesting work in this program actually!
	 * In this routine we'll initialize the Wumpus cave to have all rooms
	 * linking to all others by stepping through our data structure once,
	 * recording all forward links and backwards links too.  The parallel
	 * "linkcount" data structure ensures that no room ends up with more
	 * than three links, regardless of the quality of the random number
	 * generator that we're using.
	 */

	/* Note that throughout the source there are commented-out vestigial
	 * remains of the 'magic tunnel', which was a tunnel to room
	 * room_num +1.  It was necessary if all paths were two-way and
	 * there was an odd number of rooms, each with an odd number of
	 * exits.  It's being kept in case cave_init ever gets reworked into
	 * something more traditional.
	 */

	/* initialize the cave first off. */
	for (i = 1; i <= room_num; ++i)
		for (j = 0; j < link_num ; ++j)
			cave[i].tunnel[j] = -1;

	/* choose a random 'hop' delta for our guaranteed link.
	 * To keep the cave connected, require greatest common
	 * divisor of (delta + 1) and room_num to be 1
	 */
	do {
		delta = arc4random_uniform(room_num - 1) + 1;
	} while (gcd(room_num, delta + 1) != 1);

	for (i = 1; i <= room_num; ++i) {
		link = ((i + delta) % room_num) + 1;	/* connection */
		cave[i].tunnel[0] = link;		/* forw link */
		cave[link].tunnel[1] = i;		/* back link */
	}
	/* now fill in the rest of the cave with random connections.
	 * This is a departure from historical versions of wumpus.
	 */
	for (i = 1; i <= room_num; i++)
		for (j = 2; j < link_num ; j++) {
			if (cave[i].tunnel[j] != -1)
				continue;
try_again:		link = arc4random_uniform(room_num) + 1;
			/* skip duplicates */
			for (k = 0; k < j; k++)
				if (cave[i].tunnel[k] == link)
					goto try_again;
			/* don't let a room connect to itself */
			if (link == i)
				goto try_again;
			cave[i].tunnel[j] = link;
			if (arc4random() % 2 == 1)
				continue;
			for (k = 0; k < link_num; ++k) {
				/* if duplicate, skip it */
				if (cave[link].tunnel[k] == i)
					k = link_num;
				else {
					/* if open link, use it, force exit */
					if (cave[link].tunnel[k] == -1) {
						cave[link].tunnel[k] = i;
						k = link_num;
					}
				}
			}
		}
	/*
	 * now that we're done, sort the tunnels in each of the rooms to
	 * make it easier on the intrepid adventurer.
	 */
	for (i = 1; i <= room_num; ++i)
		qsort(cave[i].tunnel, (u_int)link_num,
		    sizeof(cave[i].tunnel[0]), int_compare);

#ifdef DEBUG
	if (debug)
		for (i = 1; i <= room_num; ++i) {
			(void)printf("<room %d  has tunnels to ", i);
			for (j = 0; j < link_num; ++j)
				(void)printf("%d ", cave[i].tunnel[j]);
			(void)printf(">\n");
		}
#endif
}

void
dodecahedral_cave_init(void)
{
	int vert[20][3] = {
		{1, 4, 7},
		{0, 2, 9},
		{1, 3, 11},
		{2, 4, 13},
		{0, 3, 5},
		{4, 6, 14},
		{5, 7, 16},
		{0, 6, 8},
		{7, 9, 17},
		{1, 8, 10},
		{9, 11, 18},
		{2, 10, 12},
		{11, 13, 19},
		{3, 12, 14},
		{5, 13, 15},
		{14, 16, 19},
		{6, 15, 17},
		{8, 16, 18},
		{10, 17, 19},
		{12, 15, 18},
	};
	int loc[20];
	int i, j, temp;

	if (room_num != 20 || link_num != 3)
		errx(1, "wrong parameters for dodecahedron");
	for (i = 0; i < 20; i++)
		loc[i] = i;
	for (i = 0; i < 20; i++) {
		j = arc4random_uniform(20 - i);
		if (j) {
			temp = loc[i];
			loc[i] = loc[i + j];
			loc[i + j] = temp;
		}
	}
	/* cave is offset by 1 */
	for (i = 0; i < 20; i++) {
		for (j = 0; j < 3; j++)
			cave[loc[i] + 1].tunnel[j] = loc[vert[i][j]] + 1;
	}

	/*
	 * now that we're done, sort the tunnels in each of the rooms to
	 * make it easier on the intrepid adventurer.
	 */
	for (i = 1; i <= room_num; ++i)
		qsort(cave[i].tunnel, (u_int)link_num,
		    sizeof(cave[i].tunnel[0]), int_compare);

#ifdef DEBUG
	if (debug)
		for (i = 1; i <= room_num; ++i) {
			(void)printf("<room %d  has tunnels to ", i);
			for (j = 0; j < link_num; ++j)
				(void)printf("%d ", cave[i].tunnel[j]);
			(void)printf(">\n");
		}
#endif
}

void
clear_things_in_cave(void)
{
	int i;

	/*
	 * remove bats and pits from the current cave in preparation for us
	 * adding new ones via the initialize_things_in_cave() routines.
	 */
	for (i = 1; i <= room_num; ++i)
		cave[i].has_a_bat = cave[i].has_a_pit = 0;
}

void
initialize_things_in_cave(void)
{
	int i, loc;

	/* place some bats, pits, the wumpus, and the player. */
	for (i = 0; i < bat_num; ++i) {
		do {
			loc = arc4random_uniform(room_num) + 1;
		} while (cave[loc].has_a_bat);
		cave[loc].has_a_bat = 1;
#ifdef DEBUG
		if (debug)
			(void)printf("<bat in room %d>\n", loc);
#endif
	}

	for (i = 0; i < pit_num; ++i) {
		do {
			loc = arc4random_uniform(room_num) + 1;
		} while (cave[loc].has_a_pit || cave[loc].has_a_bat);
		/* Above used to be &&;  || makes sense but so does just
		 * checking cave[loc].has_a_pit  */
		cave[loc].has_a_pit = 1;
#ifdef DEBUG
		if (debug)
			(void)printf("<pit in room %d>\n", loc);
#endif
	}

	wumpus_loc = arc4random_uniform(room_num) + 1;
#ifdef DEBUG
	if (debug)
		(void)printf("<wumpus in room %d>\n", wumpus_loc);
#endif

	do {
		player_loc = arc4random_uniform(room_num) + 1;
	} while (player_loc == wumpus_loc || cave[player_loc].has_a_pit ||
			cave[player_loc].has_a_bat);
	/* Replaced (level == HARD ?
	 *  (link_num / room_num < 0.4 ? wump_nearby() : 0) : 0)
	 * with bat/pit checks in initial room.  If this is kept there is
	 * a slight chance that no room satisfies all four conditions.
	 */
}

int
getans(const char *prompt)
{
	char buf[20];

	/*
	 * simple routine to ask the yes/no question specified until the user
	 * answers yes or no, then return 1 if they said 'yes' and 0 if they
	 * answered 'no'.
	 */
	for (;;) {
		(void)printf("%s", prompt);
		(void)fflush(stdout);
		if (!fgets(buf, sizeof(buf), stdin))
			return(0);
		if (*buf == 'N' || *buf == 'n')
			return(0);
		if (*buf == 'Y' || *buf == 'y')
			return(1);
		(void)printf(
"I don't understand your answer; please enter 'y' or 'n'!\n");
	}
}

int
bats_nearby(void)
{
	int i;

	/* check for bats in the immediate vicinity */
	for (i = 0; i < link_num; ++i)
		if (cave[cave[player_loc].tunnel[i]].has_a_bat)
			return(1);
	return(0);
}

int
pit_nearby(void)
{
	int i;

	/* check for pits in the immediate vicinity */
	for (i = 0; i < link_num; ++i)
		if (cave[cave[player_loc].tunnel[i]].has_a_pit)
			return(1);
	return(0);
}

int
wump_nearby(void)
{
	int i, j;

	/* check for a wumpus within TWO caves of where we are */
	for (i = 0; i < link_num; ++i) {
		if (cave[player_loc].tunnel[i] == wumpus_loc)
			return(1);
		for (j = 0; j < link_num; ++j)
			if (cave[cave[player_loc].tunnel[i]].tunnel[j] ==
			    wumpus_loc)
				return(1);
	}
	return(0);
}

void
move_wump(void)
{
	wumpus_loc = cave[wumpus_loc].tunnel[arc4random_uniform(link_num)];
#ifdef DEBUG
	if (debug)
		(void)printf("Wumpus moved to room %d\n",wumpus_loc);
#endif
}

int
int_compare(const void *a, const void *b)
{
	return(*(const int *)a < *(const int *)b ? -1 : 1);
}

void
instructions(void)
{
	const char *pager;
	pid_t pid;
	int status;
	int fd;

	/*
	 * read the instructions file, if needed, and show the user how to
	 * play this game!
	 */
	if (!getans("Instructions? (y-n) "))
		return;

	if ((fd = open(_PATH_WUMPINFO, O_RDONLY)) == -1) {
		(void)printf(
"Sorry, but the instruction file seems to have disappeared in a\n\
puff of greasy black smoke! (poof)\n");
		return;
	}

	if (!isatty(1))
		pager = "/bin/cat";
	else {
		if (!(pager = getenv("PAGER")) || (*pager == 0))
			pager = _PATH_PAGER;
	}
	switch (pid = fork()) {
	case 0: /* child */
		if (dup2(fd, 0) == -1)
			err(1, "dup2");
		(void)execl(_PATH_BSHELL, "sh", "-c", pager, (char *)NULL);
		err(1, "exec sh -c %s", pager);
		/* NOT REACHED */
	case -1:
		err(1, "fork");
		/* NOT REACHED */
	default:
		(void)waitpid(pid, &status, 0);
		close(fd);
		break;
	}
}

void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: %s [-ho] [-a arrows] [-b bats] [-p pits] "
	    "[-r rooms] [-t tunnels]\n", getprogname());
	exit(1);
}

/* messages */
void
wump_kill(void)
{
	(void)printf(
"*ROAR* *chomp* *snurfle* *chomp*!\n\
Much to the delight of the Wumpus, you walk right into his mouth,\n\
making you one of the easiest dinners he's ever had!  For you, however,\n\
it's a rather unpleasant death.  The only good thing is that it's been\n\
so long since the evil Wumpus cleaned his teeth that you immediately\n\
pass out from the stench!\n");
}

void
wump_walk_kill(void)
{
	(void)printf(
"Oh dear.  All the commotion has managed to awaken the evil Wumpus, who\n\
has chosen to walk into this very room!  Your eyes open wide as they behold\n\
the great sucker-footed bulk that is the Wumpus; the mouth of the Wumpus\n\
also opens wide as the evil beast beholds dinner.\n\
*ROAR* *chomp* *snurfle* *chomp*!\n");
}

void
wump_bat_kill(void)
{
	(void)printf(
"Flap, flap.  The bats fly you right into the room with the evil Wumpus!\n\
The Wumpus, seeing a fine dinner flying overhead, takes a swipe at you,\n\
and the bats, not wanting to serve as hors d'oeuvres, drop their\n\
soon-to-be-dead weight and take off in the way that only bats flying out\n\
of a very bad place can.  As you fall towards the large, sharp, and very\n\
foul-smelling teeth of the Wumpus, you think, \"Man, this is going to hurt.\"\n\
It does.\n");
}

void
kill_wump(void)
{
	(void)printf(
"*thwock!* *groan* *crash*\n\n\
A horrible roar fills the cave, and you realize, with a smile, that you\n\
have slain the evil Wumpus and won the game!  You don't want to tarry for\n\
long, however, because not only is the Wumpus famous, but the stench of\n\
dead Wumpus is also quite well known--a stench powerful enough to slay the\n\
mightiest adventurer at a single whiff!!\n");
}

void
no_arrows(void)
{
	(void)printf(
"\nYou turn and look at your quiver, and realize with a sinking feeling\n\
that you've just shot your last arrow (figuratively, too).  Sensing this\n\
with its psychic powers, the evil Wumpus rampages through the cave, finds\n\
you, and with a mighty *ROAR* eats you alive!\n");
}

void
shoot_self(void)
{
	(void)printf(
"\n*Thwack!*  A sudden piercing feeling informs you that your wild arrow\n\
has ricocheted back and wedged in your side, causing extreme agony.  The\n\
evil Wumpus, with its psychic powers, realizes this and immediately rushes\n\
to your side, not to help, alas, but to EAT YOU!\n\
(*CHOMP*)\n");
}

/*
 * void
 * jump(int where)
 * {
 * 	(void)printf(
 * "\nWith a jaunty step you enter the magic tunnel.  As you do, you\n\
 * notice that the walls are shimmering and glowing.  Suddenly you feel\n\
 * a very curious, warm sensation and find yourself in room %d!!\n", where);
 * }
 */

void
pit_kill(void)
{
	(void)printf(
"*AAAUUUUGGGGGHHHHHhhhhhhhhhh...*\n\
The whistling sound and updraft as you walked into this room of the\n\
cave apparently weren't enough to clue you in to the presence of the\n\
bottomless pit.  You have a lot of time to reflect on this error as\n\
you fall many miles to the core of the earth.  Look on the bright side;\n\
you can at least find out if Jules Verne was right...\n");
}

void
pit_kill_bat(void)
{
	(void)printf(
"*AAAUUUUGGGGGHHHHHhhhhhhhhhh...*\n\
It appears the bats have decided to drop you into a bottomless pit.  At\n\
least, that's what the whistling sound and updraft would suggest.  Look on\n\
the bright side; you can at least find out if Jules Verne was right...\n");
}

void
pit_survive(void)
{
	(void)printf(
"Without conscious thought you grab for the side of the cave and manage\n\
to grasp onto a rocky outcrop.  Beneath your feet stretches the limitless\n\
depths of a bottomless pit!  Rock crumbles beneath your feet!\n");
}
