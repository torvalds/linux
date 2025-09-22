/*	$OpenBSD: command2.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com2.c,v 1.3 1995/03/21 15:06:55 cgd Exp $	*/

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
wearit(void)
{				/* synonyms = {sheathe, sheath} */
	int     firstnumber, value;

	firstnumber = wordnumber;
	wordnumber++;
	while (wordnumber <= wordcount && (wordtype[wordnumber] == OBJECT ||
	    (wordtype[wordnumber] == NOUNS && wordvalue[wordnumber] != DOOR))) {
		value = wordvalue[wordnumber];
		if (value >= 0 && objsht[value] == NULL)
			break;
		switch (value) {

		case -1:
			puts("Wear what?");
			return (firstnumber);

		default:
			printf("You can't wear %s%s!\n",
			    A_OR_AN_OR_BLANK(value), objsht[value]);
			return (firstnumber);

		case KNIFE:
	/*	case SHIRT:	*/
		case ROBE:
		case LEVIS:	/* wearable things */
		case SWORD:
		case MAIL:
		case HELM:
		case SHOES:
		case PAJAMAS:
		case COMPASS:
		case LASER:
		case AMULET:
		case TALISMAN:
		case MEDALION:
		case ROPE:
		case RING:
		case BRACELET:
		case GRENADE:

			if (TestBit(inven, value)) {
				ClearBit(inven, value);
				SetBit(wear, value);
				carrying -= objwt[value];
				encumber -= objcumber[value];
				ourtime++;
				printf("You are now wearing %s%s.\n",
				    A_OR_AN_OR_THE(value), objsht[value]);
			} else
				if (TestBit(wear, value))
					printf("You are already wearing the %s.\n",
					    objsht[value]);
				else
					printf("You aren't holding the %s.\n",
					    objsht[value]);
			if (wordnumber < wordcount - 1 &&
			    wordvalue[++wordnumber] == AND)
				wordnumber++;
			else
				return (firstnumber);
		}		/* end switch */
	}			/* end while */
	puts("Don't be ridiculous.");
	return (firstnumber);
}

int
put(void)
{				/* synonyms = {buckle, strap, tie} */
	if (inc_wordnumber(words[wordnumber], "what"))
		return(-1);
	if (wordvalue[wordnumber] == ON) {
		wordvalue[wordnumber] = PUTON;
		wordtype[wordnumber] = VERB;
		return (cypher() - 1);
	}
	if (wordvalue[wordnumber] == DOWN) {
		wordvalue[wordnumber] = DROP;
		wordtype[wordnumber] = VERB;
		return (cypher() - 1);
	}
	puts("I don't understand what you want to put.");
	return (-1);
}

int
draw(void)
{				/* synonyms = {pull, carry} */
	return (take(wear));
}

int
use(void)
{
	if (inc_wordnumber(words[wordnumber], "what"))
		return(-1);
	if (wordvalue[wordnumber] == AMULET && TestBit(inven, AMULET) &&
	    position != FINAL) {
		puts("The amulet begins to glow.");
		if (TestBit(inven, MEDALION)) {
			puts("The medallion comes to life too.");
			if (position == 114) {
				location[position].down = 160;
				whichway(location[position]);
				puts("The waves subside and it is possible to descend to the sea cave now.");
				ourtime++;
				wordnumber++;
				return (-1);
			}
		}
		puts("A light mist falls over your eyes and the sound of purling water trickles in");
		puts("your ears.   When the mist lifts you are standing beside a cool stream.");
		if (position == 229)
			position = 224;
		else
			position = 229;
		ourtime++;
		notes[CANTSEE] = 0;
		wordnumber++;
		return (0);
	}
	else if (position == FINAL)
		puts("The amulet won't work in here.");
	else if (wordvalue[wordnumber] == COMPASS && TestBit(inven, COMPASS))
		printf("Your compass points %s.\n", truedirec(NORTH,'-'));
	else if (wordvalue[wordnumber] == COMPASS)
		puts("You aren't holding the compass.");
	else if (wordvalue[wordnumber] == AMULET)
		puts("You aren't holding the amulet.");
	else
		puts("There is no apparent use.");
	wordnumber++;
	return (-1);
}

void
murder(void)
{
	int     n;

	if (inc_wordnumber(words[wordnumber], "whom"))
		return;
	for (n = 0; n < NUMOFOBJECTS &&
	    !((n == SWORD || n == KNIFE || n == TWO_HANDED || n == MACE ||
	    n == CLEAVER || n == BROAD || n == CHAIN || n == SHOVEL ||
	    n == HALBERD) && TestBit(inven, n)); n++)
		continue;
	if (n == NUMOFOBJECTS) {
		if (TestBit(inven, LASER)) {
			printf("Your laser should do the trick.\n");
			switch(wordvalue[wordnumber]) {
			case NORMGOD:
			case TIMER:
			case NATIVE:
			case MAN:
				wordvalue[--wordnumber] = SHOOT;
				cypher();
				break;
			case -1:
				puts("Kill what?");
				break;
			default:
				if (wordtype[wordnumber] != OBJECT ||
				    wordvalue[wordnumber] == EVERYTHING)
					puts("You can't kill that!");
				else
					printf("You can't kill %s%s!\n",
					    A_OR_AN_OR_BLANK(wordvalue[wordnumber]),
					    objsht[wordvalue[wordnumber]]);
				break;
			}
		} else
			puts("You don't have suitable weapons to kill.");
	} else {
		printf("Your %s should do the trick.\n", objsht[n]);
		switch (wordvalue[wordnumber]) {

		case NORMGOD:
			if (TestBit(location[position].objects, BATHGOD)) {
				puts("The goddess's head slices off.  Her corpse floats in the water.");
				ClearBit(location[position].objects, BATHGOD);
				SetBit(location[position].objects, DEADGOD);
				power += 5;
				notes[JINXED]++;
			} else
				if (TestBit(location[position].objects, NORMGOD)) {
					puts("The goddess pleads but you strike her mercilessly.  Her broken body lies in a\npool of blood.");
					ClearBit(location[position].objects, NORMGOD);
					SetBit(location[position].objects, DEADGOD);
					power += 5;
					notes[JINXED]++;
					if (wintime)
						live();
				} else
					puts("I don't see her anywhere.");
			break;
		case TIMER:
			if (TestBit(location[position].objects, TIMER)) {
				puts("The old man offers no resistance.");
				ClearBit(location[position].objects, TIMER);
				SetBit(location[position].objects, DEADTIME);
				power++;
				notes[JINXED]++;
			} else
				puts("Who?");
			break;
		case NATIVE:
			if (TestBit(location[position].objects, NATIVE)) {
				puts("The girl screams as you cut her body to shreds.  She is dead.");
				ClearBit(location[position].objects, NATIVE);
				SetBit(location[position].objects, DEADNATIVE);
				power += 5;
				notes[JINXED]++;
			} else
				puts("What girl?");
			break;
		case MAN:
			if (TestBit(location[position].objects, MAN)) {
				puts("You strike him to the ground, and he coughs up blood.");
				puts("Your fantasy is over.");
				die(0);
			}
		case -1:
			puts("Kill what?");
			break;

		default:
			if (wordtype[wordnumber] != OBJECT ||
			    wordvalue[wordnumber] == EVERYTHING)
				puts("You can't kill that!");
			else
				printf("You can't kill the %s!\n",
				    objsht[wordvalue[wordnumber]]);
		}
	}
	wordnumber++;
}

void
ravage(void)
{
	if (inc_wordnumber(words[wordnumber], "whom"))
		return;
	if (wordtype[wordnumber] == NOUNS && (TestBit(location[position].objects, wordvalue[wordnumber])
	    || (wordvalue[wordnumber] == NORMGOD && TestBit(location[position].objects, BATHGOD)))) {
		ourtime++;
		switch (wordvalue[wordnumber]) {
		case NORMGOD:
			puts("You attack the goddess, and she screams as you beat her.  She falls down");
			if (TestBit(location[position].objects, BATHGOD))
				puts("crying and tries to cover her nakedness.");
			else
				puts("crying and tries to hold her torn and bloodied dress around her.");
			power += 5;
			pleasure += 8;
			ego -= 10;
			wordnumber--;
			godready = -30000;
			murder();
			win = -30000;
			break;
		case NATIVE:
			puts("The girl tries to run, but you catch her and throw her down.  Her face is");
			puts("bleeding, and she screams as you tear off her clothes.");
			power += 3;
			pleasure += 5;
			ego -= 10;
			wordnumber--;
			murder();
			if (rnd(100) < 50) {
				puts("Her screams have attracted attention.  I think we are surrounded.");
				SetBit(location[ahead].objects, WOODSMAN);
				SetBit(location[ahead].objects, DEADWOOD);
				SetBit(location[ahead].objects, MALLET);
				SetBit(location[back].objects, WOODSMAN);
				SetBit(location[back].objects, DEADWOOD);
				SetBit(location[back].objects, MALLET);
				SetBit(location[left].objects, WOODSMAN);
				SetBit(location[left].objects, DEADWOOD);
				SetBit(location[left].objects, MALLET);
				SetBit(location[right].objects, WOODSMAN);
				SetBit(location[right].objects, DEADWOOD);
				SetBit(location[right].objects, MALLET);
			}
			break;
		default:
			puts("You are perverted.");
			wordnumber++;
		}
	} else {
		printf("%s:  Who?\n", words[wordnumber]);
		wordnumber++;
	}
}

int
follow(void)
{
	if (followfight == ourtime) {
		puts("The Dark Lord leaps away and runs down secret tunnels and corridors.");
		puts("You chase him through the darkness and splash in pools of water.");
		puts("You have cornered him.  His laser sword extends as he steps forward.");
		position = FINAL;
		fight(DARK, 75);
		SetBit(location[position].objects, TALISMAN);
		SetBit(location[position].objects, AMULET);
		return (0);
	} else
		if (followgod == ourtime) {
			puts("The goddess leads you down a steamy tunnel and into a high, wide chamber.");
			puts("She sits down on a throne.");
			position = 268;
			SetBit(location[position].objects, NORMGOD);
			notes[CANTSEE] = 1;
			return (0);
		} else
			puts("There is no one to follow.");
	return (-1);
}

void
undress(void)
{
	if (inc_wordnumber(words[wordnumber], "whom"))
		return;
	if (wordvalue[wordnumber] == NORMGOD &&
	    (TestBit(location[position].objects, NORMGOD)) && godready >= 2) {
		wordnumber--;
		love();
	} else {
		wordnumber--;
		ravage();
	}
}	
