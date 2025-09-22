/*	$OpenBSD: command5.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com5.c,v 1.3 1995/03/21 15:07:07 cgd Exp $	*/

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

void
kiss(void)
{
	if (inc_wordnumber(words[wordnumber], "whom"))
		return;
	/* The goddess must be "taken" first if bathing. */
	if (wordtype[wordnumber] == NOUNS && wordvalue[wordnumber] == NORMGOD
	    && TestBit(location[position].objects, BATHGOD)) {
		wordvalue[--wordnumber] = TAKE;
		cypher();
		return;
	}
	if (wordtype[wordnumber] == NOUNS) {
	    if (TestBit(location[position].objects, wordvalue[wordnumber])) {
		pleasure++;
		printf("Kissed.\n");
		switch (wordvalue[wordnumber]) {
		case NORMGOD:
 			switch (godready++) {
			case 0:
				puts("She squirms and avoids your advances.");
				break;
			case 1:
				puts("She is coming around; she didn't fight it as much.");
				break;
			case 2:
				puts("She's beginning to like it.");
				break;
			default:
				puts("She's gone limp.");

			}
			break;
		case NATIVE:
			puts("Her lips are warm and her body robust.  She pulls you down to the ground.");
			break;
		case TIMER:
			puts("The old man blushes.");
			break;
		case MAN:
			puts("The dwarf punches you in the kneecap.");
			break;
		default:
			pleasure--;
		}
	    } else
		puts("I see nothing like that here.");
	} else
		puts("I'd prefer not to.");
	wordnumber++;
}

void
love(void)
{
	int     n;

	if (inc_wordnumber(words[wordnumber], "whom"))
		return;
	if (wordtype[wordnumber] == NOUNS) {
	    if ((TestBit(location[position].objects, BATHGOD) ||
	        TestBit(location[position].objects, NORMGOD)) &&
		   wordvalue[wordnumber] == NORMGOD) {
			wordnumber++;
			if (loved) {
				printf("Loved.\n");
				return;
			}
			if (godready >= 2) {
				puts("She cuddles up to you, and her mouth starts to work:\n'That was my sister's amulet.  The lovely goddess, Purl, was she.  The Empire\ncaptured her just after the Darkness came.  My other sister, Vert, was killed\nby the Dark Lord himself.  He took her amulet and warped its power.\nYour quest was foretold by my father before he died, but to get the Dark Lord's\namulet you must use cunning and skill.  I will leave you my amulet,");
				puts("which you may use as you wish.  As for me, I am the last goddess of the\nwaters.  My father was the Island King, and the rule is rightfully mine.'\n\nShe pulls the throne out into a large bed.");
				power++;
				pleasure += 15;
				ego++;
				if (card(injuries, NUMOFINJURIES)) {
					puts("Her kisses revive you; your wounds are healed.\n");
					for (n = 0; n < NUMOFINJURIES; n++)
						injuries[n] = 0;
					WEIGHT = MAXWEIGHT;
					CUMBER = MAXCUMBER;
				}
				printf("Goddess:\n");
				if (!loved)
					SetBit(location[position].objects, MEDALION);
				loved = 1;
				ourtime += 10;
				printf("Loved.\n");
				zzz();
				return;
			} else {
				puts("You wish!");
				return;
			}
		}
	    if (TestBit(location[position].objects, wordvalue[wordnumber])) {
		if (wordvalue[wordnumber] == NATIVE) {
			puts("The girl is easy prey.  She peels off her sarong and indulges you.");
			power++;
			pleasure += 5;
			printf("Girl:\n");
			ourtime += 10;
			printf("Loved.\n");
			zzz();
		}
		if (wordvalue[wordnumber] == MAN ||
		    wordvalue[wordnumber] == BODY ||
		    wordvalue[wordnumber] == ELF ||
		    wordvalue[wordnumber] == TIMER)
			puts("Kinky!");
		else
			puts("It doesn't seem to work.");
	    } else
		puts("Where's your lover?");
	} else
		puts("It doesn't seem to work.");
	wordnumber++;
}

int
zzz(void)
{
	int     oldtime;
	int     n;

	oldtime = ourtime;
	if ((snooze - ourtime) < (0.75 * CYCLE)) {
		ourtime += 0.75 * CYCLE - (snooze - ourtime);
		printf("<zzz>");
		for (n = 0; n < ourtime - oldtime; n++)
			printf(".");
		printf("\n");
		snooze += 3 * (ourtime - oldtime);
		if (notes[LAUNCHED]) {
			fuel -= (ourtime - oldtime);
			if (location[position].down) {
				position = location[position].down;
				crash();
			} else
				notes[LAUNCHED] = 0;
		}
		if (OUTSIDE && rnd(100) < 50) {
			puts("You are awakened abruptly by the sound of someone nearby.");
			switch (rnd(4)) {
			case 0:
				if (ucard(inven)) {
					n = rnd(NUMOFOBJECTS);
					while (!TestBit(inven, n))
						n = rnd(NUMOFOBJECTS);
					ClearBit(inven, n);
					if (n != AMULET && n != MEDALION && n != TALISMAN)
						SetBit(location[position].objects, n);
					carrying -= objwt[n];
					encumber -= objcumber[n];
				}
				puts("A fiendish little Elf is stealing your treasures!");
				fight(ELF, 10);
				break;
			case 1:
				SetBit(location[position].objects, DEADWOOD);
				break;
			case 2:
				SetBit(location[position].objects, HALBERD);
				break;
			default:
				break;
			}
		}
	} else
		return (0);
	return (1);
}

void
chime(void)
{
	if ((ourtime / CYCLE + 1) % 2 && OUTSIDE)
		switch ((ourtime % CYCLE) / (CYCLE / 7)) {
		case 0:
			puts("It is just after sunrise.");
			break;
		case 1:
			puts("It is early morning.");
			break;
		case 2:
			puts("It is late morning.");
			break;
		case 3:
			puts("It is near noon.");
			break;
		case 4:
			puts("It is early afternoon.");
			break;
		case 5:
			puts("It is late afternoon.");
			break;
		case 6:
			puts("It is near sunset.");
			break;
		}
	else if (OUTSIDE)
		switch ((ourtime % CYCLE) / (CYCLE / 7)) {
		case 0:
			puts("It is just after sunset.");
			break;
		case 1:
			puts("It is early evening.");
			break;
		case 2:
			puts("The evening is getting old.");
			break;
		case 3:
			puts("It is near midnight.");
			break;
		case 4:
			puts("These are the wee hours of the morning.");
			break;
		case 5:
			puts("The night is waning.");
			break;
		case 6:
			puts("It is almost morning.");
			break;
		}
	else
		puts("I can't tell the time in here.");
}

int
give(void)
{
	int obj = -1, result = -1, person = 0, firstnumber, last1, last2;

	last1 = last2 = wordcount + 2;
	firstnumber = wordnumber;
	while (wordtype[++wordnumber] != OBJECT &&
	    wordvalue[wordnumber] != AMULET &&
	    wordvalue[wordnumber] != MEDALION &&
	    wordvalue[wordnumber] != TALISMAN &&
	    wordnumber <= wordcount)
		continue;
	if (wordnumber <= wordcount) {
		obj = wordvalue[wordnumber];
		if (obj == EVERYTHING)
			wordtype[wordnumber] = -1;
		last1 = wordnumber;
	}
	wordnumber = firstnumber;
	while ((wordtype[++wordnumber] != NOUNS ||
	    wordvalue[wordnumber] == obj) && wordnumber <= wordcount)
		continue;
	if (wordtype[wordnumber] == NOUNS) {
		person = wordvalue[wordnumber];
		last2 = wordnumber;
	}
	/* Setting wordnumber to last1 - 1 looks wrong if last1 is 0, e.g.,
	 * plain `give'.  However, detecting this case is liable to detect
	 * `give foo' as well, which would give a confusing error.  We
	 * need to make sure the -1 value can cause no problems if it arises.
	 * If in the below we get to the drop("Given") then drop will look
	 * at word 0 for an object to give, and fail, which is OK; then
	 * result will be -1 and we get to the end, where wordnumber gets
	 * set to something more sensible.  If we get to "I don't think
	 * that is possible" then again wordnumber is set to something
	 * sensible.  The wordnumber we leave with still isn't right if
	 * you include words the game doesn't know in your command, but
	 * that's no worse than what other commands than give do in
	 * the same place.  */
	wordnumber = last1 - 1;
	if (person && TestBit(location[position].objects, person)) {
		if (person == NORMGOD && godready < 2 && !(obj == RING || obj == BRACELET))
			puts("The goddess won't look at you.");
		else
			result = drop("Given");
	} else {
		puts("I don't think that is possible.");
		wordnumber = max(last1, last2) + 1;
		return (0);
	}
	if (result != -1 && (TestBit(location[position].objects, obj) || obj == AMULET || obj == MEDALION || obj == TALISMAN)) {
		ClearBit(location[position].objects, obj);
		ourtime++;
		ego++;
		switch (person) {
		case NATIVE:
			puts("She accepts it shyly.");
			ego += 2;
			break;
		case NORMGOD:
			if (obj == RING || obj == BRACELET) {
				puts("She takes the charm and puts it on.  A little kiss on the cheek is");
				puts("your reward.");
				ego += 5;
				godready += 3;
			}
			if (obj == AMULET || obj == MEDALION || obj == TALISMAN) {
				win++;
				ego += 5;
				power -= 5;
				if (win >= 3) {
					puts("The powers of the earth are now legitimate.  You have destroyed the Darkness");
					puts("and restored the goddess to her throne.  The entire island celebrates with");
					puts("dancing and spring feasts.  As a measure of her gratitude, the goddess weds you");
					puts("in the late summer and crowns you Prince Liverwort, Lord of Fungus.");
					puts("\nBut, as the year wears on and autumn comes along, you become restless and");
					puts("yearn for adventure.  The goddess, too, realizes that the marriage can't last.");
					puts("She becomes bored and takes several more natives as husbands.  One evening,");
					puts("after having been out drinking with the girls, she kicks the throne particularly");
					puts("hard and wakes you up.  (If you want to win this game, you're going to have to\nshoot her!)");
					ClearBit(location[position].objects, MEDALION);
					wintime = ourtime;
				}
			}
			break;
		case TIMER:
			if (obj == COINS) {
				puts("He fingers the coins for a moment and then looks up agape.  `Kind you are and");
				puts("I mean to repay you as best I can.'  Grabbing a pencil and cocktail napkin...\n");
				printf("+-----------------------------------------------------------------------------+\n");
				printf("|				   xxxxxxxx\\				      |\n");
				printf("|				       xxxxx\\	CLIFFS			      |\n");
				printf("|		FOREST			  xxx\\				      |\n");
				printf("|				\\\\	     x\\        	OCEAN		      |\n");
				printf("|				||	       x\\			      |\n");
				printf("|				||  ROAD	x\\			      |\n");
				printf("|				||		x\\			      |\n");
				printf("|		SECRET		||	  .........			      |\n");
				printf("|		 - + -		||	   ........			      |\n");
				printf("|		ENTRANCE	||		...      BEACH		      |\n");
				printf("|				||		...		  E	      |\n");
				printf("|				||		...		  |	      |\n");
				printf("|				//		...	    N <-- + --- S     |\n");
				printf("|		PALM GROVE     //		...		  |	      |\n");
				printf("|			      //		...		  W	      |\n");
				printf("+-----------------------------------------------------------------------------+\n");
				puts("\n`This map shows a secret entrance to the catacombs.");
				puts("You will know when you arrive because I left an old pair of shoes there.'");
			}
			break;
		}
	}
	wordnumber = max(last1, last2) + 1;
	return (firstnumber);
}
