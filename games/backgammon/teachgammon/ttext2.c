/*	$OpenBSD: ttext2.c,v 1.8 2015/11/30 08:19:25 tb Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

#include "back.h"
#include "tutor.h"

const char   *const doubl[] = {
	"\nDoubling:",
	"\n   If a player thinks he is in a good position, he may double the",
	"value of the game.  However, his opponent may not accept the proposal",
	"and can forfeit the game before the price gets too high.  A player",
	"must double before he rolls, and once his double has been accepted,",
	"he cannot double again until his opponent has doubled.  Thus, unless",
	"the game swings back and forth in advantage between the two players",
	"a great deal, the value of the game should be low.  At any rate,",
	"the value of the game can never go above 64, or six doubles.",
	"However, if a player wins a backgammon at 64 points, he wins 192",
	"points!",
	"",
	0};

const char   *const stragy[] = {
	"\nStrategy:",
	"\n   Some general hints when playing:  Try not to leave men open:",
	"it is good to make as many points as possible.  Often, two men",
	"from different positions can be brought together to form a new",
	"point.  Although walls (six points in a row) are difficult to form,",
	"many points nestled closely together still produce a formidable",
	"barrier.  Also, while it is good to move back men forward, doing",
	"so lessens the opportunity for you to hit the opponent's blots.",
	"Finally, remember that once the two player's men have passed each other",
	"on the board, there is no chance of either side being hit, so the",
	"game reduces to a race off the board.  Additional hints on strategy",
	"are presented in the practice game.",
	"",
	0};

const char   *const prog[] = {
	"\nThe Program and How It Works:",
	"\n   A general rule of thumb is to type a question mark if you",
	"don't know what to do:  you should get some help.  When it is your",
	"turn, only your color will be printed out, with nothing after it.",
	"You may double by typing `d', but if you type a space or newline,",
	"you will get your roll.  (Remember, you must double before you",
	"roll.)  Also, typing `r' will reprint the board, and typing `q'",
	"will quit the game.  The program will print 'Move:' when it wants",
	"your move.  You may indicate each die's move with <s>-<f>, where",
	"<s> is the starting position and <f> is the finishing position, or",
	"<s>/<r> where <r> is the roll made.  <s>-<f1>-<f2> is short for",
	"<s>-<f1>,<f1>-<f2> and <s>/<r1><r2> is short for <s>/<r1>,<s>/<r2>.",
	"Moves may be separated by a comma or a space.",
	"",
	"\n   Any input which does not make sense will not be echoed; instead,",
	"a bell will sound.  Backspacing and killing lines may echo differently",
	"than usual.  You may examine the board by typing `r' if you have",
	"made a partial move or before you type a newline to see what the",
	"board will look like.  You must end your move with a newline.  If",
	"you cannot double, your roll will be printed and you will not be",
	"given the opportunity to double.  Home and bar can be represented",
	"by the appropriate number, 0 or 25 as the case may be, or by the",
	"letters `h' or `b' as appropriate.  You may also type `r' or `q'",
	"when the program prints 'Move:' to redraw the screen or quit the",
	"game.  At the beginning of the session, you will get to decide if",
	"you want to play red or white (or both if you want to play a friend),",
	"and you will not get to change your mind later, since the computer",
	"keeps a running score.",
	"",
	0};

const char   *const lastch[] = {
	"\nTutorial (Practice Game):",
	"\n   This tutorial, for simplicity's sake, will let you play one",
	"predetermined game.  All the rolls have been pre-arranged, and",
	"only one response will let you advance to the next move.  Although",
	"a given roll will may have several legal moves, the tutorial will",
	"only accept one (not including the same moves in a different order),",
	"claiming that that move is 'best'--obviously, a subjective statement.",
	"At any rate, be patient with it and have fun learning about",
	"backgammon.  To speed things up a little, doubling will not",
	"take place in the tutorial.  Quitting the tutorial leaves only",
	"the tutorial; you will still be able to play a real backgammon",
	"game after quitting.",
	"\n   This is your last chance to look over the rules before the",
	"tutorial starts.",
	"",
	0};

int
text(const char  *const *txt)
{
	const char  *const *begin;
	const char   *a;
	char    b;
	const char   *c;
	int     i;

	begin = txt;
	while (*txt) {
		a = *(txt++);
		if (*a != '\0') {
			c = a;
			for (i = 0; *(c++) != '\0'; i--);
			printw("%s\n", a);
		} else {
			addstr(prompt);
			/* if begscr is set we're past the rules screens */
			if (!begscr) {
				for (;;) {
					if ((b = readc()) == '?') {
						clear();
						text(list);
						addstr(prompt);
						continue;
					}
					i = 0;
					if (b == '\n')
						break;
					while (i < 11) {
						if (b == opts[i])
							break;
						i++;
					}
					if (i == 11)
						beep();
					else
						break;
				}
			} else {
				b = readc();
				i = 0;
			}
			if (begscr) {
				move(18, 0);
				clrtobot();
			} else
				clear();
			if (i)
				return(i);
			/* move to start of current line? */
			begin = txt;
		}
	}
	return(0);
}
