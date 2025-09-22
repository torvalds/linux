/*	$OpenBSD: init_field.c,v 1.8 2016/01/04 17:33:24 mestre Exp $	*/
/*	$NetBSD: init_field.c,v 1.3 1995/04/22 10:08:52 cgd Exp $	*/

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

#include "robots.h"

/*
 * init_field:
 *	Lay down the initial pattern which is constant across all levels,
 *	and initialize all the global variables.
 */
void
init_field(void)
{
	int	i;
	static bool	first = TRUE;
	static char	*desc[] = {
				"Directions:",
				"",
				"y k u",
				" \\|/",
				"h- -l",
				" /|\\",
				"b j n",
				"",
				"Commands:",
				"",
				"w:  wait for end",
				"t:  teleport",
				"q:  quit",
				"^L: redraw screen",
				"",
				"Legend:",
				"",
				"+:  robot",
				"*:  junk heap",
				"@:  you",
				"",
				"Score: 0",
				NULL
	};

	Dead = FALSE;
	Waiting = FALSE;
#ifndef NCURSES_VERSION
	flushok(stdscr, TRUE);
#endif
	Score = 0;

	erase();
	move(0, 0);
	addch('+');
	for (i = 1; i < Y_FIELDSIZE; i++) {
		move(i, 0);
		addch('|');
	}
	move(Y_FIELDSIZE, 0);
	addch('+');
	for (i = 1; i < X_FIELDSIZE; i++)
		addch('-');
	addch('+');
	if (first)
		refresh();
	move(0, 1);
	for (i = 1; i < X_FIELDSIZE; i++)
		addch('-');
	addch('+');
	for (i = 1; i < Y_FIELDSIZE; i++) {
		move(i, X_FIELDSIZE);
		addch('|');
	}
	if (first)
		refresh();
	for (i = 0; desc[i] != NULL; i++) {
		move(i, X_FIELDSIZE + 2);
		addstr(desc[i]);
	}
	if (first)
		refresh();
	first = FALSE;
#ifdef	FANCY
	if (Pattern_roll)
		Next_move = &Move_list[-1];
#endif
}
