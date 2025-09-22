/*	$OpenBSD: text.c,v 1.9 2015/11/30 08:19:25 tb Exp $	*/

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

const char *const instruct[] = {
	"    This program reacts to keystrokes immediately, without waiting",
	"for a newline.  Consequently, special characters such as RUBOUT",
	"and ESC will not perform their special functions during most of",
	"this program.  The program should still usually stop on a CONTROL-D,",
	"though.\n",
	"    These instructions are presented in small chunks designed not",
	"to roll off the top of your screen.  No more data will appear",
	"after the characters '-->' are printed until you type a space or",
	"newline.  In this way, you can finish reading one section before",
	"continuing to another.  Like this:",
	"",
	"    The two sides are called `red' and `white'. The computer can play",
	"one side, or, if there are two players, the computer may act as merely",
	"a gamekeeper, letting the players make the moves.  Once you tell",
	"the computer what color(s) you want to play, the decision remains",
	"in effect until you quit the program, even if you play more than",
	"one game, since the program keeps a running score.\n",
	"    The program will prompt for a move in one of two ways.  If the",
	"player has the opportunity to double, then only his color will be",
	"displayed.  The player can then do one of several things:  he can",
	"double by typing 'd', he can roll by typing a space (` ') or newline,",
	"or if he is not sure, he can reprint the board by typing `r'.\n",
	"    If the player cannot double, his roll will be thrust in front of",
	"him, followed by the request 'Move:' asking for a move without giving",
	"him the chance to double.  He can still ask for the board by typing",
	"`r'.  In either of these two states, the player can quit by typing `q'",
	"or can save the game by typing 's'.  In both cases, the player will be",
	"asked for confirmation.",
	"",
	"    A player can move his men using one of two forms of input.",
	"The first form is <s>-<f>, where <s> is the starting position and",
	"<f> is the finishing position of the player's man.  For example,",
	"if white wanted to move a piece from position 13 to position 8,",
	"his move could be entered as 13-8.  The second form is <s>/<r>,",
	"where <s> is the starting position and <r> is the roll actually",
	"made.  So white could have entered as 13/5 instead of 13-8.\n",
	"    A player must move each roll of the dice separately.  For",
	"example, if a player rolled 4 3, and wanted to move from 13 to",
	"6, he could enter the move as 13/4,9/3 or 13/3,10/4 or 13-10,10-6",
	"or 13-9,9-6, but not as 13-6.  The last two entries can be shortened",
	"to 13-10-6 and 13-9-6.  If he wanted to move more than one piece",
	"from the same position, such as 13-10,13-9, he could abbreviate",
	"this using the <s>/<r> format by entering more than one <r>:  13/34.",
	"A player can use both forms for the same roll, e.g. 13/3,13-9, and",
	"must separate individual moves by either a comma or a space.",
	"The letter 'b' represents the bar, and the letter 'h' represents",
	"a player's home, as do the numbers 25 and 0 (or 0 and 25 as",
	"appropriate).  A turn is finished with a newline.",
	"",
	"    If a typed character does not make sense under the above",
	"constraints, a bell will sound and the character will be ignored.",
	"Finally, the board can be redisplayed after one or more of the",
	"moves has been entered by typing `r'.  This cannot be done in the",
	"middle of a move (e.g., immediately after a `-' or `/').  After",
	"printing the board, the program will go back to inputting the move,",
	"which can still be backspaced over and changed.\n",
	"    Now you should be ready to begin the game.  Good luck!",
	"",
	0};

int
text(const char *const *t)
{
	int     i;
	const char   *s, *a;

	while (*t != 0) {
		s = a = *t;
		for (i = 0; *a != '\0'; i--)
			a++;
		if (i)
			printw("%s\n", s);
		else {
			addstr("-->");
			while ((i = readc()) != ' ' && i != '\n');
			clear();
		}
		t++;
	}
	return(0);
}
