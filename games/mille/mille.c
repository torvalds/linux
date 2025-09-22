/*	$OpenBSD: mille.c,v 1.26 2021/10/23 11:22:49 mestre Exp $	*/
/*	$NetBSD: mille.c,v 1.4 1995/03/24 05:01:48 cgd Exp $	*/

/*
 * Copyright (c) 1982, 1993
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
#include <signal.h>
#include <stdlib.h>
#ifdef DEBUG
#include <string.h>
#endif
#include <unistd.h>

#include "mille.h"

/*
 * @(#)mille.c	1.3 (Berkeley) 5/10/83
 */

int
main(int ac, char *av[])
{
	bool	restore;
	extern char *__progname;

#ifdef DEBUG
	if (strcmp(av[0], "a.out") == 0) {
		outf = fopen("q", "w");
		setvbuf(outf, NULL, _IONBF, 0);
		Debug = TRUE;
	}
#endif
	restore = FALSE;
	switch (ac) {
	  case 2:
		rest_f(av[1]);
		restore = TRUE;
	  case 1:
		break;
	  default:
		fprintf(stderr, "usage: %s [file]\n", __progname);
		return 1;
	}
	Play = PLAYER;
	initscr();

	if (pledge("stdio rpath wpath cpath tty", NULL) == -1)
		err(1, "pledge");

	if ((LINES < 24) || (COLS < 80)) {
		endwin();
		fprintf(stderr, "Screen must be at least 24x80\n");
		return 1;
	}
	delwin(stdscr);
	stdscr = Board = newwin(BOARD_Y, BOARD_X, 0, 0);
	Score = newwin(SCORE_Y, SCORE_X, 0, 40);
	Miles = newwin(MILES_Y, MILES_X, 17, 0);
	leaveok(Score, TRUE);
	leaveok(Miles, TRUE);
	clearok(curscr, TRUE);
	cbreak();
	noecho();
	signal(SIGINT, rub);
	for (;;) {
		if (!restore || (Player[PLAYER].total >= 5000
		    || Player[COMP].total >= 5000)) {
			if (Player[COMP].total < Player[PLAYER].total)
				Player[PLAYER].games++;
			else if (Player[COMP].total > Player[PLAYER].total)
				Player[COMP].games++;
			Player[COMP].total = 0;
			Player[PLAYER].total = 0;
		}
		do {
			if (!restore)
				Handstart = Play = other(Handstart);
			if (!restore || On_exit) {
				shuffle();
				init();
			}
			newboard();
			if (restore)
				mvwaddstr(Score, ERR_Y, ERR_X, Initstr);
			prboard();
			do {
				domove();
				if (Finished)
					newscore();
				prboard();
			} while (!Finished);
			check_more();
			restore = On_exit = FALSE;
		} while (Player[COMP].total < 5000
		    && Player[PLAYER].total < 5000);
	}
}

/*
 *	Routine to trap rubouts, and make sure they really want to
 * quit.
 */
void
rub(int dummy)
{
	(void)signal(SIGINT, SIG_IGN);
	if (getyn(REALLYPROMPT))
		die(0);
	(void)signal(SIGINT, rub);
}

/*
 *	Time to go beddy-by
 */
void
die(int code)
{
	(void)signal(SIGINT, SIG_IGN);
	if (outf)
		fflush(outf);
	mvcur(0, COLS - 1, LINES - 1, 0);
	endwin();
	exit(code);
}
