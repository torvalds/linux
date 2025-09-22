/*	$OpenBSD: command1.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com1.c,v 1.3 1995/03/21 15:06:51 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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
#include <stdlib.h>
#include <unistd.h>

#include "extern.h"

int
moveplayer(int thataway, int token)
{
	wordnumber++;
	if ((!notes[CANTMOVE] && !notes[LAUNCHED]) ||
	    TestBit(location[position].objects, LAND) ||
	    (fuel > 0 && notes[LAUNCHED])) {
		if (thataway) {
			position = thataway;
			newway(token);
			ourtime++;
		} else {
			puts("You can't go this way.");
			newway(token);
			whichway(location[position]);
			return (0);
		}
	} else
		if (notes[CANTMOVE] && !notes[LAUNCHED])
			puts("You aren't able to move; you better drop something.");
		else
			puts("You are out of fuel; now you will rot in space forever!");
	return (1);
}

/*
 * Converts day to night and vice versa.
 * Day objects are permanent.  Night objects
 * are added at dusk, and subtracted at dawn.
 */
void
convert(int tothis)			
{				
	const struct objs *p;
	unsigned int i, j;

	if (tothis == TONIGHT) {
		for (i = 1; i <= NUMOFROOMS; i++)
			for (j = 0; j < NUMOFWORDS; j++)
				nightfile[i].objects[j] = dayfile[i].objects[j];
		for (p = nightobjs; p->room != 0; p++)
			SetBit(nightfile[p->room].objects, p->obj);
		location = nightfile;
	} else {
		for (i = 1; i <= NUMOFROOMS; i++)
			for (j = 0; j < NUMOFWORDS; j++)
				dayfile[i].objects[j] = nightfile[i].objects[j];
		for (p = nightobjs; p->room != 0; p++)
			ClearBit(dayfile[p->room].objects, p->obj);
		location = dayfile;
	}
}

void
news(void)
{
	int     n;
	int     hurt;

	if (ourtime > 30 && position < 32) {
		puts("An explosion of shuddering magnitude splinters bulkheads and");
		puts("ruptures the battlestar's hull.  You are sucked out into the");
		puts("frozen void of space and killed.");
		die(0);
	}
	if (ourtime > 20 && position < 32)
		puts("Explosions rock the battlestar.");
	if (ourtime > snooze) {
		puts("You drop from exhaustion...");
		zzz();
	}
	if (ourtime > snooze - 5)
		puts("You're getting tired.");
	if (ourtime > (rythmn + CYCLE)) {
		if (location == nightfile) {
			convert(TODAY);
			if (OUTSIDE && ourtime - rythmn - CYCLE < 10) {
				puts("Dew lit sunbeams stretch out from a watery sunrise and herald the dawn.");
				puts("You awake from a misty dream-world into stark reality.");
				puts("It is day.");
			}
		} else {
			convert(TONIGHT);
			ClearBit(location[POOLS].objects, BATHGOD);
			if (OUTSIDE && ourtime - rythmn - CYCLE < 10) {
				puts("The dying sun sinks into the ocean, leaving a blood-stained sunset.");
				puts("The sky slowly fades from orange to violet to black.  A few stars");
				puts("flicker on, and it is night.");
				puts("The world seems completely different at night.");
			}
		}
		rythmn = ourtime - ourtime % CYCLE;
	}
	if (!tempwiz)
		if ((TestBit(inven, TALISMAN) || TestBit(wear, TALISMAN)) && (TestBit(inven, MEDALION) || TestBit(wear, MEDALION)) && (TestBit(inven, AMULET) || TestBit(wear, AMULET))) {
			tempwiz = 1;
			puts("The three amulets glow and reenforce each other in power.\nYou are now a wizard.");
		}
	if (TestBit(location[position].objects, ELF)) {
		printf("%s\n", objdes[ELF]);
		fight(ELF, rnd(30));
	}
	if (TestBit(location[position].objects, DARK)) {
		printf("%s\n", objdes[DARK]);
		fight(DARK, 100);
	}
	if (TestBit(location[position].objects, WOODSMAN)) {
		printf("%s\n", objdes[WOODSMAN]);
		fight(WOODSMAN, 50);
	}
	switch (position) {

	case 267:
	case 257:		/* entering a cave */
	case 274:
	case 246:
		notes[CANTSEE] = 1;
		break;
	case 160:
	case 216:		/* leaving a cave */
	case 230:
	case 231:
	case 232:
		notes[CANTSEE] = 0;
		break;
	}
	if (TestBit(location[position].objects, GIRL))
		meetgirl = 1;
	if (meetgirl && CYCLE * 1.5 - ourtime < 10) {
		SetBit(location[GARDEN].objects, GIRLTALK);
		SetBit(location[GARDEN].objects, LAMPON);
		SetBit(location[GARDEN].objects, ROPE);
	}
	if (position == DOCK && (beenthere[position] || ourtime > CYCLE)) {
		ClearBit(location[DOCK].objects, GIRL);
		ClearBit(location[DOCK].objects, MAN);
	}
	if (meetgirl && ourtime - CYCLE * 1.5 > 10) {
		ClearBit(location[GARDEN].objects, GIRLTALK);
		ClearBit(location[GARDEN].objects, LAMPON);
		ClearBit(location[GARDEN].objects, ROPE);
		meetgirl = 0;
	}
	if (TestBit(location[position].objects, CYLON)) {
		puts("Oh my God, you're being shot at by an alien spacecraft!");
		printf("The targeting computer says we have %d seconds to attack!\n",
		    ourclock);
		fflush(stdout);
		sleep(1);
		if (!visual()) {
			hurt = rnd(NUMOFINJURIES);
			injuries[hurt] = 1;
			puts("Laser blasts sear the cockpit, and the alien veers off in a victory roll.");
			puts("The viper shudders under a terrible explosion.");
			printf("I'm afraid you have suffered %s.\n", ouch[hurt]);
		} else
			ClearBit(location[position].objects, CYLON);
	}
	if (injuries[SKULL] && injuries[INCISE] && injuries[NECK]) {
		puts("I'm afraid you have suffered fatal injuries.");
		die(0);
	}
	for (n = 0; n < NUMOFINJURIES; n++)
		if (injuries[n] == 1) {
			injuries[n] = 2;
			if (WEIGHT > 5)
				WEIGHT -= 5;
			else
				WEIGHT = 0;
		}
	if (injuries[ARM] == 2) {
		if (CUMBER > 5)
			CUMBER -= 5;
		else
			CUMBER = 0;
		injuries[ARM]++;
	}
	if (injuries[RIBS] == 2) {
		if (CUMBER > 2)
			CUMBER -= 2;
		else
			CUMBER = 0;
		injuries[RIBS]++;
	}
	if (injuries[SPINE] == 2) {
		WEIGHT = 0;
		injuries[SPINE]++;
	}
	if (carrying > WEIGHT || encumber > CUMBER)
		notes[CANTMOVE] = 1;
	else
		notes[CANTMOVE] = 0;
}

void
crash(void)
{
	int     hurt1, hurt2;

	fuel--;
	if (!location[position].flyhere ||
	    (TestBit(location[position].objects, LAND) && fuel <= 0)) {
		if (!location[position].flyhere)
			puts("You're flying too low.  We're going to crash!");
		else {
			puts("You're out of fuel.  We'll have to crash land!");
			if (!location[position].down) {
				puts("Your viper strikes the ground and explodes into fiery fragments.");
				puts("Thick black smoke billows up from the wreckage.");
				die(0);
			}
			position = location[position].down;
		}
		notes[LAUNCHED] = 0;
		SetBit(location[position].objects, CRASH);
		ourtime += rnd(CYCLE / 4);
		puts("The viper explodes into the ground and you lose consciousness...");
		zzz();
		hurt1 = rnd(NUMOFINJURIES - 2) + 2;
		hurt2 = rnd(NUMOFINJURIES - 2) + 2;
		injuries[hurt1] = 1;
		injuries[hurt2] = 1;
		injuries[0] = 1;	/* abrasions */
		injuries[1] = 1;	/* lacerations */
		printf("I'm afraid you have suffered %s and %s.\n",
		    ouch[hurt1], ouch[hurt2]);
	}
}

void
newlocation(void)
{
	news();
	if (beenthere[position] <= ROOMDESC)
	     beenthere[position]++;
	if (notes[LAUNCHED])
		crash();	/* decrements fuel & crash */
	if (matchlight) {
		puts("Your match splutters out.");
		matchlight = 0;
	}
	if (!notes[CANTSEE] || TestBit(inven, LAMPON) ||
	    TestBit(location[position].objects, LAMPON)) {
		writedes();
		printobjs();
	} else
		puts("It's too dark to see anything in here!");
	whichway(location[position]);
}
