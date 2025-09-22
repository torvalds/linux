/*	$OpenBSD: command6.c,v 1.1 2020/12/15 00:38:18 daniel Exp $	*/
/*	$NetBSD: com6.c,v 1.5 1995/04/27 21:30:23 mycroft Exp $	*/

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

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "extern.h"

int
launch(void)
{
	if (TestBit(location[position].objects, VIPER) && !notes[CANTLAUNCH]) {
		if (fuel > 4) {
			ClearBit(location[position].objects, VIPER);
			position = location[position].up;
			notes[LAUNCHED] = 1;
			ourtime++;
			fuel -= 4;
			puts("You climb into the viper and prepare for launch.");
			puts("With a touch of your thumb the turbo engines ignite, thrusting you back into\nyour seat.");
			return (1);
		} else
			puts("Not enough fuel to launch.");
	} else
		puts("Can't launch.");
	return (0);
}

int
land(void)
{
	if (notes[LAUNCHED] && TestBit(location[position].objects, LAND) &&
	    location[position].down) {
		notes[LAUNCHED] = 0;
		position = location[position].down;
		SetBit(location[position].objects, VIPER);
		fuel -= 2;
		ourtime++;
		puts("You are down.");
		return (1);
	} else
		puts("You can't land here.");
	return (0);
}

/* endgame */
void
die(int sigraised)
{
	printf("bye.\nYour rating was %s.\n", rate());
	post(' ');
	exit(0);
}

void
live(void)
{
	puts("\nYou win!");
	post('!');
	exit(0);
}

static FILE *score_fp;

void
open_score_file(void)
{
	char		 scorefile[PATH_MAX];
	const char	*home;
	int		 ret;

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");
	ret = snprintf(scorefile, sizeof(scorefile), "%s/%s", home,
	    ".battlestar.scores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".battlestar.scores");
	if ((score_fp = fopen(scorefile, "a")) == NULL)
		warn("can't append to high scores file (%s)", scorefile);
}

void
post(char ch)
{
	time_t tv;
	char   *date;
	sigset_t sigset, osigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, &osigset);
	tv = time(NULL);
	date = ctime(&tv);
	date[24] = '\0';

	if (score_fp != NULL) {
		fprintf(score_fp, "%s  %31s  %c%20s", date, username, ch, rate());
		if (tempwiz)
			fprintf(score_fp, "   WIZARD!\n");
		else
			fprintf(score_fp, "\n");
	}
	sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
}

const char   *
rate(void)
{
	int     score;

	score = max(max(pleasure, power), ego);
	if (score == pleasure) {
		if (score < 5)
			return ("novice");
		else if (score < 20)
			return ("junior voyeur");
		else if (score < 35)
			return ("Don Juan");
		else
			return ("Marquis De Sade");
	} else
		if (score == power) {
			if (score < 5)
				return ("serf");
			else if (score < 8)
				return ("Samurai");
			else if (score < 13)
				return ("Klingon");
			else if (score < 22)
				return ("Darth Vader");
			else
				return ("Sauron the Great");
		} else{
			if (score < 5)
				return ("Polyanna");
			else if (score < 10)
				return ("philanthropist");
			else if (score < 20)
				return ("Tattoo");
			else
				return ("Mr. Roarke");
		}
}

int
drive(void)
{
	if (TestBit(location[position].objects, CAR)) {
		puts("You hop in the car and turn the key.  There is a perceptible grating noise,");
		puts("and an explosion knocks you unconscious...");
		ClearBit(location[position].objects, CAR);
		SetBit(location[position].objects, CRASH);
		injuries[5] = injuries[6] = injuries[7] = injuries[8] = 1;
		ourtime += 15;
		zzz();
		return (0);
	} else
		puts("There is nothing to drive here.");
	return (-1);
}

int
ride(void)
{
	if (TestBit(location[position].objects, HORSE)) {
		puts("You climb onto the stallion and kick it in the guts.  The stupid steed launches");
		puts("forward through bush and fern.  You are thrown and the horse gallops off.");
		ClearBit(location[position].objects, HORSE);
		while (!(position = rnd(NUMOFROOMS + 1)) || !OUTSIDE ||
		    !beenthere[position] || location[position].flyhere)
			continue;
		SetBit(location[position].objects, HORSE);
		if (location[position].north)
			position = location[position].north;
		else if (location[position].south)
			position = location[position].south;
		else if (location[position].east)
			position = location[position].east;
		else
			position = location[position].west;
		return (0);
	}
	else puts("There is no horse here.");
	return (-1);
}

void
light(void)
{				/* synonyms = {strike, smoke} */
	if (TestBit(inven, MATCHES) && matchcount) {
		puts("Your match splutters to life.");
		ourtime++;
		matchlight = 1;
		matchcount--;
		if (position == 217) {
			puts("The whole bungalow explodes with an intense blast.");
			die(0);
		}
	} else
		puts("You're out of matches.");
}

void
dooropen(void)
{				/* synonyms = {open, unlock} */
	wordnumber++;
	if (wordnumber <= wordcount && wordtype[wordnumber] == NOUNS
	    && wordvalue[wordnumber] == DOOR) {
		switch(position) {
		case 189:
		case 231:
			if (location[189].north == 231)
				puts("The door is already open.");
			else
				puts("The door does not budge.");
			break;
		case 30:
			if (location[30].west == 25)
				puts("The door is gone.");
			else
				puts("The door is locked tight.");
			break;
		case 31:
			puts("That's one immovable door.");
			break;
		case 20:
			puts("The door is already ajar.");
			break;
		default:
			puts("What door?");
		}
	} else
		puts("That doesn't open.");
}
