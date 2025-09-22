/*	$OpenBSD: command3.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com3.c,v 1.3 1995/03/21 15:07:00 cgd Exp $	*/

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

#include "extern.h"

void
dig(void)
{
	if (TestBit(inven, SHOVEL)) {
		puts("OK");
		ourtime++;
		switch (position) {
		case 144:	/* copse near beach */
			if (!notes[DUG]) {
				SetBit(location[position].objects, DEADWOOD);
				SetBit(location[position].objects, COMPASS);
				SetBit(location[position].objects, KNIFE);
				SetBit(location[position].objects, MACE);
				notes[DUG] = 1;
			}
			break;

		default:
			puts("Nothing happens.");
		}
	} else	
		puts("You don't have a shovel.");
}

int
jump(void)
{
	int     n;

	switch (position) {
	default:
		puts("Nothing happens.");
		return (-1);

	case 242:
		position = 133;
		break;
	case 214:
	case 215:
	case 162:
	case 159:
		position = 145;
		break;
	case 232:
		position = FINAL;
		break;
	case 3:
		position = 1;
		break;
	case 172:
		position = 201;
	}
	puts("Ahhhhhhh...");
	injuries[12] = injuries[8] = injuries[7] = injuries[6] = 1;
	for (n = 0; n < NUMOFOBJECTS; n++)
		if (TestBit(inven, n)) {
			ClearBit(inven, n);
			SetBit(location[position].objects, n);
		}
	carrying = 0;
	encumber = 0;
	return (0);
}

void
bury(void)
{
	int     value;

	if (TestBit(inven, SHOVEL)) {
		while (wordtype[++wordnumber] != OBJECT &&
		    wordtype[wordnumber] != NOUNS && wordnumber <= wordcount)
			continue;
		value = wordvalue[wordnumber];
		if (wordtype[wordnumber] == NOUNS && (TestBit(location[position].objects, value) || value == BODY)) {
			switch (value) {
			case BODY:
				wordtype[wordnumber] = OBJECT;
				if (TestBit(inven, MAID) || TestBit(location[position].objects, MAID))
					value = MAID;
				if (TestBit(inven, DEADWOOD) || TestBit(location[position].objects, DEADWOOD))
					value = DEADWOOD;
				if (TestBit(inven, DEADGOD) || TestBit(location[position].objects, DEADGOD))
					value = DEADGOD;
				if (TestBit(inven, DEADTIME) || TestBit(location[position].objects, DEADTIME))
					value = DEADTIME;
				if (TestBit(inven, DEADNATIVE) || TestBit(location[position].objects, DEADNATIVE))
					value = DEADNATIVE;
				break;

			case NATIVE:
			case NORMGOD:
				puts("She screams as you wrestle her into the hole.");
			case TIMER:
				power += 7;
				ego -= 10;
			case AMULET:
			case MEDALION:
			case TALISMAN:
				wordtype[wordnumber] = OBJECT;
				break;

			default:
				puts("Wha..?");
			}
		}
		if (wordtype[wordnumber] == OBJECT && position > 88 && (TestBit(inven, value) || TestBit(location[position].objects, value))) {
			puts("Buried.");
			if (TestBit(inven, value)) {
				ClearBit(inven, value);
				carrying -= objwt[value];
				encumber -= objcumber[value];
			}
			ClearBit(location[position].objects, value);
			switch (value) {
			case MAID:
			case DEADWOOD:
			case DEADNATIVE:
			case DEADTIME:
			case DEADGOD:
				ego += 2;
				printf("The %s should rest easier now.\n", objsht[value]);
			}
		} else
			puts("It doesn't seem to work.");
	} else
		puts("You aren't holding a shovel.");
}

void
drink(void)
{
	int     n;

	if (TestBit(inven, POTION)) {
		puts("The cool liquid runs down your throat but turns to fire and you choke.");
		puts("The heat reaches your limbs and tingles your spirit.  You feel like falling");
		puts("asleep.");
		ClearBit(inven, POTION);
		WEIGHT = MAXWEIGHT;
		CUMBER = MAXCUMBER;
		for (n = 0; n < NUMOFINJURIES; n++)
			injuries[n] = 0;
		ourtime++;
		zzz();
	} else
		puts("I'm not thirsty.");
}

int
shoot(void)
{
	int     firstnumber, value;

	firstnumber = wordnumber;
	if (!TestBit(inven, LASER))
		puts("You aren't holding a blaster.");
	else {
		wordnumber++;
		while(wordnumber <= wordcount && wordtype[wordnumber] == OBJECT) {
			value = wordvalue[wordnumber];
			printf("%s:\n", objsht[value]);
			if (TestBit(location[position].objects, value)) {
				ClearBit(location[position].objects, value);
				ourtime++;
				printf("The %s explode%s\n", objsht[value],
				    (IS_PLURAL(value) ? "." : "s."));
				if (value == BOMB)
					die(0);
			} else
				printf("I don't see any %s around here.\n", objsht[value]);
			if (wordnumber < wordcount - 1 && wordvalue[++wordnumber] == AND)
				wordnumber++;
			else
				return (firstnumber);
		}
		/* special cases with their own return()'s */

		if (wordnumber <= wordcount && wordtype[wordnumber] == NOUNS) {
			ourtime++;
			switch (wordvalue[wordnumber]) {

			case DOOR:
				switch(position) {
				case 189:
				case 231:
					puts("The door is unhinged.");
					location[189].north = 231;
					location[231].south = 189;
					whichway(location[position]);
					break;
				case 30:
					puts("The wooden door splinters.");
					location[30].west = 25;
					whichway(location[position]);
					break;
				case 31:
					puts("The laser blast has no effect on the door.");
					break;
				case 20:
					puts("The blast hits the door and it explodes into flame.  The magnesium burns");
					puts("so rapidly that we have no chance to escape.");
					die(0);
				default:
					puts("Nothing happens.");
				}
				break;

			case NORMGOD:
			case BATHGOD:
				if (TestBit(location[position].objects, BATHGOD)) {
					puts("The goddess is hit in the chest and splashes back against the rocks.");
					puts("Dark blood oozes from the charred blast hole.  Her naked body floats in the");
					puts("pools and then off downstream.");
					ClearBit(location[position].objects, BATHGOD);
					SetBit(location[180].objects, DEADGOD);
					power += 5;
					ego -= 10;
					notes[JINXED]++;
				} else
					if (TestBit(location[position].objects, NORMGOD)) {
						puts("The blast catches the goddess in the stomach, knocking her to the ground.");
						puts("She writhes in the dirt as the agony of death taunts her.");
						puts("She has stopped moving.");
						ClearBit(location[position].objects, NORMGOD);
						SetBit(location[position].objects, DEADGOD);
						power += 5;
						ego -= 10;
						notes[JINXED]++;
						if (wintime)
							live();
						break;
					} else
						puts("I don't see any goddess around here.");
				break;

			case TIMER:
				if (TestBit(location[position].objects, TIMER)) {
					puts("The old man slumps over the bar.");
					power++;
					ego -= 2;
					notes[JINXED]++;
					ClearBit(location[position].objects, TIMER);
					SetBit(location[position].objects, DEADTIME);
				} else
					puts("What old-timer?");
				break;
			case MAN:
				if (TestBit(location[position].objects, MAN)) {
					puts("The man falls to the ground with blood pouring all over his white suit.");
					puts("Your fantasy is over.");
					die(0);
				} else
					puts("What man?");
				break;
			case NATIVE:
				if (TestBit(location[position].objects, NATIVE)) {
					puts("The girl is blown backwards several feet and lies in a pool of blood.");
					ClearBit(location[position].objects, NATIVE);
					SetBit(location[position].objects, DEADNATIVE);
					power += 5;
					ego -= 2;
					notes[JINXED]++;
				} else
					puts("There is no girl here.");
				break;
			case -1:
				puts("Shoot what?");
				break;

			default:
				printf("You can't shoot the %s.\n", objsht[wordvalue[wordnumber]]);
			}
		} else
			puts("You must be a looney.");
	}
	return (firstnumber);
}
