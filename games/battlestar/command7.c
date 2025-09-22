/*	$OpenBSD: command7.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com7.c,v 1.3 1995/03/21 15:07:12 cgd Exp $	*/

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

#include "extern.h"

int
fight(int enemy, int strength)
{
	int     lifeline = 0;
	int     hurt;
	char    auxbuf[LINELENGTH];
	char   *next;
	int     i;
	int     exhaustion;

	stop_cypher = 1;	/* Don't parse the existing input line further */
fighton:
	ourtime++;
	snooze -= 5;
	if (snooze > ourtime)
		exhaustion = CYCLE / (snooze - ourtime);
	else {
		puts("You collapse exhausted and he pulverizes your skull.");
		die(0);
	}
	if (snooze - ourtime < 20)
		puts("You look tired! I hope you're able to fight.");
	next = getcom(auxbuf, LINELENGTH, "<fight!>-: ", NULL);
	for (i = 0; next && i < NWORD - 1; i++)
		next = getword(next, words[i], -1);
	parse();
	switch (wordvalue[wordnumber]) {

	case KILL:
	case SMITE:
		if (TestBit(inven, TWO_HANDED))
			hurt = rnd(70) - 2 * card(injuries, NUMOFINJURIES) - ucard(wear) - exhaustion;
		else if (TestBit(inven, SWORD) || TestBit(inven, BROAD))
			hurt = rnd(50) % (WEIGHT - carrying) - card(injuries, NUMOFINJURIES) - encumber - exhaustion;
		else if (TestBit(inven, KNIFE) || TestBit(inven, MALLET) || TestBit(inven, CHAIN) || TestBit(inven, MACE) || TestBit(inven, HALBERD))
			hurt = rnd(15) - card(injuries, NUMOFINJURIES) - exhaustion;
		else
			hurt = rnd(7) - encumber;
		if (hurt < 5)
			switch (rnd(3)) {

			case 0:
				puts("You swung wide and missed.");
				break;
			case 1:
				puts("He checked your blow. CLASH! CLANG!");
				break;
			case 2:
				puts("His filthy tunic hangs by one less thread.");
				break;
			}
		else if (hurt < 10) {
			switch (rnd(3)) {
			case 0:
				puts("He's bleeding.");
				break;
			case 1:
				puts("A trickle of blood runs down his face.");
				break;
			case 2:
				puts("A huge purple bruise is forming on the side of his face.");
				break;
			}
			lifeline++;
		}
		else if (hurt < 20) {
			switch (rnd(3)) {
			case 0:
				puts("He staggers back quavering.");
				break;
			case 1:
				puts("He jumps back with his hand over the wound.");
				break;
			case 2:
				puts("His shirt falls open with a swath across the chest.");
				break;
			}
			lifeline += 5;
		}
		else if (hurt < 30) {
			switch (rnd(3)) {
			case 0:
				printf("A bloody gash opens up on his %s side.\n",(rnd(2) ? "left" : "right"));
				break;
			case 1:
				puts("The steel bites home and scrapes along his ribs.");
				break;
			case 2:
				puts("You pierce him, and his breath hisses through clenched teeth.");
				break;
			}
			lifeline += 10;
		}
		else if (hurt < 40) {
			switch (rnd(3)) {
			case 0:
				puts("You smite him to the ground.");
				if (strength - lifeline > 20)
					puts("But in a flurry of steel he regains his feet!");
				break;
			case 1:
				puts("The force of your blow sends him to his knees.");
				puts("His arm swings lifeless at his side.");
				break;
			case 2:
				puts("Clutching his blood-drenched shirt, he collapses, stunned.");
				break;
			}
			lifeline += 20;
		}
		else {
			switch (rnd(3)) {
			case 0:
				puts("His ribs crack under your powerful swing, flooding his lungs with blood.");
				break;
			case 1:
				puts("You shatter his upheld arm in a spray of blood.  The blade continues deep");
				puts("into his back, severing the spinal cord.");
				lifeline += 25;
				break;
			case 2:
				puts("With a mighty lunge the steel slides in, and gasping, he falls to the ground.");
				lifeline += 25;
				break;
			}
			lifeline += 30;
		}
		break;

	case BACK:
		if (enemy == DARK && lifeline > strength * 0.33) {
			puts("He throws you back against the rock and pummels your face.");
			if (TestBit(inven, AMULET) || TestBit(wear, AMULET)) {
				printf("Lifting the amulet from you, ");
				if (TestBit(inven, MEDALION) || TestBit(wear, MEDALION)) {
					puts("his power grows and the walls of\nthe earth tremble.");
					puts("When he touches the medallion, your chest explodes and the foundations of the\nearth collapse.");
					puts("The planet is consumed by darkness.");
					die(0);
				}
				if (TestBit(inven, AMULET)) {
					ClearBit(inven, AMULET);
					carrying -= objwt[AMULET];
					encumber -= objcumber[AMULET];
				} else
					ClearBit(wear, AMULET);
				puts("he flees down the dark caverns.");
				ClearBit(location[position].objects, DARK);
				injuries[SKULL] = 1;
				followfight = ourtime;
				return (0);
			} else {
				puts("I'm afraid you have been killed.");
				die(0);
			}
		} else {
			puts("You escape stunned and disoriented from the fight.");
			puts("A victorious bellow echoes from the battlescene.");
			if (back && position != back)
				moveplayer(back, BACK);
			else if (ahead &&position != ahead)
				moveplayer(ahead, AHEAD);
			else if (left && position != left)
				moveplayer(left, LEFT);
			else if (right && position != right)
				moveplayer(right, RIGHT);
			else
				moveplayer(location[position].down, AHEAD);
			return (0);
		}

	case SHOOT:
		if (TestBit(inven, LASER)) {
			if (strength - lifeline <= 50) {
				printf("The %s took a direct hit!\n", objsht[enemy]);
				lifeline += 50;
			} else {
				puts("With his bare hand he deflects the laser blast and whips the pistol from you!");
				ClearBit(inven, LASER);
				SetBit(location[position].objects, LASER);
				carrying -= objwt[LASER];
				encumber -= objcumber[LASER];
			}
		} else
			puts("Unfortunately, you don't have a blaster handy.");
		break;

	case DROP:
	case DRAW:
		/* One call to cypher() does only the first command on the line */
		cypher();
		ourtime--;
		break;

	default:
		puts("You don't have a chance; he is too quick.");
		break;

	}
	if (lifeline >= strength) {
		printf("You have killed the %s.\n", objsht[enemy]);
		if (enemy == ELF || enemy == DARK)
			puts("A watery black smoke consumes his body and then vanishes with a peal of thunder!");
		ClearBit(location[position].objects, enemy);
		power += 2;
		notes[JINXED]++;
		return (0);
	}
	puts("He attacks...");
	/* some embellishments */
	hurt = rnd(NUMOFINJURIES) - (TestBit(inven, SHIELD) != 0) - (TestBit(wear, MAIL) != 0) - (TestBit(wear, HELM) != 0);
	hurt += (TestBit(wear, AMULET) != 0) + (TestBit(wear, MEDALION) != 0) + (TestBit(wear, TALISMAN) != 0);
	hurt = hurt < 0 ? 0 : hurt;
	hurt = hurt >= NUMOFINJURIES ? NUMOFINJURIES - 1 : hurt;
	if (!injuries[hurt]) {
		injuries[hurt] = 1;
		printf("I'm afraid you have suffered %s.\n", ouch[hurt]);
	} else
		puts("You emerge unscathed.");
	if (injuries[SKULL] && injuries[INCISE] && injuries[NECK]) {
		puts("I'm afraid you have suffered fatal injuries.");
		die(0);
	}
	goto fighton;
}
