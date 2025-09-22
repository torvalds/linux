/*	$OpenBSD: endgame.c,v 1.9 2016/01/04 17:33:24 mestre Exp $	*/
/*	$NetBSD: endgame.c,v 1.3 1995/03/23 08:32:40 cgd Exp $	*/

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

#include <curses.h>
#include <termios.h>

#include "hangman.h"

/*
 * endgame:
 *	Do what's necessary at the end of the game
 */
void
endgame(void)
{
	char	ch;

	prman();
	if (Errors >= MAXERRS)
		Errors = MAXERRS + 2;
	prword();
	prdata();
	move(MESGY, MESGX);
	if (Errors > MAXERRS)
		printw("Sorry, the word was \"%s\"\n", Word);
	else
		printw("You got it!\n");

	for (;;) {
		mvaddstr(MESGY + 1, MESGX, "Another word? ");
		leaveok(stdscr, FALSE);
		refresh();
		if ((ch = readch()) == 'n' || ch == CTRL('D'))
			die(0);
		else if (ch == 'y')
			break;
		mvaddstr(MESGY + 2, MESGX, "Please type 'y' or 'n'");
	}

	leaveok(stdscr, TRUE);
	move(MESGY, MESGX);
	deleteln();
	deleteln();
	deleteln();
}
