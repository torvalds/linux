/*	$OpenBSD: cypher.c,v 1.21 2022/08/08 17:57:05 op Exp $	*/
/*	$NetBSD: cypher.c,v 1.3 1995/03/21 15:07:15 cgd Exp $	*/

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

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "pathnames.h"

static void verb_with_all(unsigned int *, int, int (*)(void), const char *);

/*
 * Prompt user to input an integer, which is stored in *value.
 * On failure prints a warning, leaves *value untouched, and returns -1.
 */
int
getnum(int *value, const char *fmt, ...)
{
	char buffer[BUFSIZ];
	va_list ap;
	const char *errstr;
	int n;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	fflush(stdout);
	va_end(ap);

	if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
		warnx("error reading input");
		return (-1);
	}
	buffer[strcspn(buffer, "\n")] = '\0';

	n = strtonum(buffer, INT_MIN, INT_MAX, &errstr);
	if (errstr) {
		warnx("number %s: %s", errstr, buffer);
		return (-1);
	}
	*value = n;
	return (0);
}

/* returns 0 if error or no more commands to do,
 *         1 if there are more commands remaining on the current input line
 */
int
cypher(void)
{
	int     n;
	int     junk;
	int     lflag = -1;
	char   *line = NULL, *filename;
	size_t  linesize = 0;
	ssize_t linelen;

	while (wordnumber <= wordcount) {
		if (wordtype[wordnumber] != VERB &&
		    !(wordtype[wordnumber] == OBJECT &&
		    wordvalue[wordnumber] == KNIFE)) {
			printf("%s: How's that?\n",
			    (wordnumber == wordcount) ? words[wordnumber - 1] : words[wordnumber]);
			return (0);
		}

		switch (wordvalue[wordnumber]) {

		case AUXVERB:
			/* Take the following word as the verb */
			wordnumber++;
			return(cypher());
			break;

		case UP:
			if (location[position].access || tempwiz) {
				if (!location[position].access)
					puts("Zap!  A gust of wind lifts you up.");
				if (!moveplayer(location[position].up, AHEAD))
					return (0);
			} else {
				puts("There is no way up.");
				return (0);
			}
			lflag = 0;
			break;

		case DOWN:
			if (!moveplayer(location[position].down, AHEAD))
				return (0);
			lflag = 0;
			break;

		case LEFT:
			if (!moveplayer(left, LEFT))
				return (0);
			lflag = 0;
			break;

		case RIGHT:
			if (!moveplayer(right, RIGHT))
				return (0);
			lflag = 0;
			break;

		case AHEAD:
			if (!moveplayer(ahead, AHEAD))
				return (0);
			lflag = 0;
			break;

		case BACK:
			if (!moveplayer(back, BACK))
				return (0);
			lflag = 0;
			break;

		case SHOOT:
			verb_with_all(location[position].objects, OBJ_PERSON,
			    shoot, "shoot at");
			break;

		case TAKE:
			if (wordnumber < wordcount && wordvalue[wordnumber + 1] == EVERYTHING) {
				int things;
				things = 0;
				for (n = 0; n < NUMOFOBJECTS; n++)
					if (TestBit(location[position].objects, n) && objsht[n]) {
						things++;
						wordvalue[wordnumber + 1] = n;
/* Some objects (type NOUNS) have special treatment in take().  For these
 * we must set the type to NOUNS.  However for SWORD and BODY all it does
 * is find which of many objects is meant, so we need do nothing here.
 * BATHGOD must become NORMGOD as well.  NOUNS with no special case must be
 * included here to get the right error.  DOOR cannot occur as an object so
 * need not be included.
 */
						switch (n) {
						case BATHGOD:
							wordvalue[wordnumber + 1] = NORMGOD;
							/* FALLTHROUGH */
						case NORMGOD:
						case AMULET:
						case MEDALION:
						case TALISMAN:
						case MAN:
						case TIMER:
						case NATIVE:
							wordtype[wordnumber + 1] = NOUNS;
							break;
						default:
							wordtype[wordnumber + 1] = OBJECT;
						}
						wordnumber = take(location[position].objects);
						wordnumber += 2;
					}
				if (!things)
					puts("Nothing to take!");
			} else
				take(location[position].objects);
			break;

		case DROP:
			if (wordnumber < wordcount && wordvalue[wordnumber + 1] == EVERYTHING) {
				int things;
				things = 0;
				for (n = 0; n < NUMOFOBJECTS; n++)
					if (TestBit(inven, n)) {
						things++;
						wordvalue[wordnumber + 1] = n;
						wordnumber = drop("Dropped");
					}
				wordnumber++;
				wordnumber++;
				if (!things)
					puts("Nothing to drop!");
			} else
				drop("Dropped");
			break;


		case KICK:
		case THROW:
			if (wordnumber < wordcount && wordvalue[wordnumber + 1] == EVERYTHING) {
				int things, wv;
				things = 0;
				wv = wordvalue[wordnumber];
				for (n = 0; n < NUMOFOBJECTS; n++)
					if (TestBit(inven, n) ||
					  (TestBit(location[position].objects, n) && objsht[n])) {
						things++;
						wordvalue[wordnumber + 1] = n;
						wordnumber = throw(wordvalue[wordnumber] == KICK ? "Kicked" : "Thrown");
					}
				wordnumber += 2;
				if (!things)
					printf("Nothing to %s!\n", wv == KICK ? "kick" : "throw");
			} else
				throw(wordvalue[wordnumber] == KICK ? "Kicked" : "Thrown");
			break;

		case TAKEOFF:
			verb_with_all(wear, 0, takeoff, "take off");
			break;

		case DRAW:
			verb_with_all(wear, 0, draw, "draw");
			break;

		case PUTON:
			verb_with_all(location[position].objects, 0, puton, "put on");
			break;

		case WEARIT:
			verb_with_all(inven, 0, wearit, "wear");
			break;

		case EAT:
			verb_with_all(inven, 0, eat, "eat");
			break;

		case PUT:
			put();
			break;

		case INVEN:
			if (ucard(inven)) {
				puts("You are holding:\n");
				for (n = 0; n < NUMOFOBJECTS; n++)
					if (TestBit(inven, n))
						printf("\t%s\n", objsht[n]);
				printf("\n= %d kilogram%s ", carrying,
				    (carrying == 1 ?  "." : "s."));
				if (WEIGHT)
					printf("(%d%%)\n", carrying * 100 / WEIGHT);
				else
					printf("(can't lift any weight%s)\n",
					    (carrying ? " or move with what you have" : ""));
				if (CUMBER)
					printf("Your arms are %d%% full.\n",
					    encumber * 100 / CUMBER);
				else
					printf("You can't pick anything up.\n");
			} else
				puts("You aren't carrying anything.");

			if (ucard(wear)) {
				puts("\nYou are wearing:\n");
				for (n = 0; n < NUMOFOBJECTS; n++)
					if (TestBit(wear, n))
						printf("\t%s\n", objsht[n]);
			} else
				puts("\nYou are stark naked.");
			if (card(injuries, NUMOFINJURIES)) {
				puts("\nYou have suffered:\n");
				for (n = 0; n < NUMOFINJURIES; n++)
					if (injuries[n])
						printf("\t%s\n", ouch[n]);
				printf("\nYou can still carry up to %d kilogram%s\n", WEIGHT, (WEIGHT == 1 ? "." : "s."));
			} else
				puts("\nYou are in perfect health.");
			break;

		case USE:
			lflag = use();
			break;

		case OPEN:
			dooropen();
			break;

		case LOOK:
			if (!notes[CANTSEE] || TestBit(inven, LAMPON) ||
			    TestBit(location[position].objects, LAMPON)
			    || matchlight) {
				beenthere[position] = 2;
				writedes();
				printobjs();
				if (matchlight) {
					puts("\nYour match splutters out.");
					matchlight = 0;
				}
			} else
				puts("I can't see anything.");
			return (0);	/* No commands after a look */
			break;

		case SU:
			if (tempwiz) {
				getnum(&position, "\nRoom (was %d) = ", position);
				getnum(&ourtime, "Time (was %d) = ", ourtime);
				getnum(&fuel, "Fuel (was %d) = ", fuel);
				getnum(&torps, "Torps (was %d) = ", torps);
				getnum(&CUMBER, "CUMBER (was %d) = ", CUMBER);
				getnum(&WEIGHT, "WEIGHT (was %d) = ", WEIGHT);
				getnum(&ourclock, "Clock (was %d) = ", ourclock);
				if (getnum(&junk, "Wizard (was %d) = ", tempwiz) != -1 && !junk)
					tempwiz = 0;
				printf("\nDONE.\n");
				return (0);	/* No commands after a SU */
			} else
				puts("You aren't a wizard.");
			break;

		case SCORE:
			printf("\tPLEASURE\tPOWER\t\tEGO\n");
			printf("\t%3d\t\t%3d\t\t%3d\n\n", pleasure, power, ego);
			printf("This gives you the rating of %s in %d turns.\n", rate(), ourtime);
			printf("You have visited %d out of %d rooms this run (%d%%).\n", card(beenthere, NUMOFROOMS), NUMOFROOMS, card(beenthere, NUMOFROOMS) * 100 / NUMOFROOMS);
			break;

		/* case KNIFE: */
		case KILL:
			murder();
			break;

		case UNDRESS:
			undress();
			break;

		case RAVAGE:
			ravage();
			break;

		case SAVE:
			printf("\nSave file name (default %s):  ",
			    DEFAULT_SAVE_FILE);
			linelen = getline(&line, &linesize, stdin);
			if (linelen == -1 || *line == '\n')
				filename = save_file_name(DEFAULT_SAVE_FILE);
			else {
				if (line[linelen - 1] == '\n')
					line[linelen - 1] = '\0';
				filename = save_file_name(line);
			}
			save(filename);
			free(filename);
			break;

		case VERBOSE:
			verbose = 1;
			printf("[Maximum verbosity]\n");
			break;

		case BRIEF:
			verbose = 0;
			printf("[Standard verbosity]\n");
			break;

		case FOLLOW:
			lflag = follow();
			break;

		case GIVE:
			give();
			break;

		case KISS:
			kiss();
			break;

		case LOVE:
			love();
			break;

		case RIDE:
			lflag = ride();
			break;

		case DRIVE:
			lflag = drive();
			break;

		case LIGHT:
			light();
			break;

		case LAUNCH:
			if (!launch())
				return (0);
			else
				lflag = 0;
			break;

		case LANDIT:
			if (!land())
				return (0);
			else
				lflag = 0;
			break;

		case TIME:
			chime();
			break;

		case SLEEP:
			zzz();
			break;

		case DIG:
			dig();
			break;

		case JUMP:
			lflag = jump();
			break;

		case BURY:
			bury();
			break;

		case SWIM:
			puts("Surf's up!");
			break;

		case DRINK:
			drink();
			break;

		case QUIT:
			die(0);

		default:
			puts("How's that?");
			return (0);
			break;

		}
		if (!lflag)
			newlocation();

		free(line);

		if (wordnumber < wordcount && !stop_cypher &&
		    (*words[wordnumber] == ',' || *words[wordnumber] == '.')) {
			wordnumber++;
			return (1);
		} else
			return (0);
	}
	return (0);
}

int
inc_wordnumber(const char *v, const char *adv)
{
	wordnumber++;
	if (wordnumber >= wordcount) {
		printf("%c%s %s?\n",
		    toupper((unsigned char)v[0]), v + 1, adv);
		return(-1);
	}
	return(0);
}

static void
verb_with_all(unsigned int *testarray, int objflg, int (*verbfunc)(void),
              const char *verbname)
{
	int things, n;

	things = 0;
	if (wordnumber < wordcount && wordvalue[wordnumber + 1] == EVERYTHING) {
		for (n = 0; n < NUMOFOBJECTS; n++)
			if (TestBit(testarray, n) &&
			    (objsht[n] || (objflg & objflags[n]))) {
				things++;
				wordvalue[wordnumber + 1] = n;
				/* Assume it's a NOUN if no short description */
				if (objsht[n])
					wordtype[wordnumber + 1] = OBJECT;
				else
					wordtype[wordnumber + 1] = NOUNS;
				wordnumber = verbfunc();
			}
		wordnumber += 2;
		if (!things)
			printf("Nothing to %s!\n", verbname);
	} else
		verbfunc();
}
