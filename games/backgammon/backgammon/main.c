/*	$OpenBSD: main.c,v 1.25 2021/10/23 11:22:48 mestre Exp $	*/

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

#include <err.h>

#include "back.h"
#include "backlocal.h"

#define MVPAUSE	5		/* time to sleep when stuck */

extern const char   *const instruct[];		/* text of instructions */

const char   *const helpm[] = {		/* help message */
	"Enter a space or newline to roll, or",
	"     R   to reprint the board\tD   to double",
	"     S   to save the game\tQ   to quit",
	0
};

const char   *const contin[] = {		/* pause message */
	"(Type a newline to continue.)",
	"",
	0
};

/*   *** Do game control through dm! ***
 * static char user1a[] =
 * 	"Sorry, you cannot play backgammon when there are more than ";
 * static char user1b[] =
 * 	" users\non the system.";
 * static char user2a[] =
 * 	"\nThere are now more than ";
 * static char user2b[] =
 * 	" users on the system, so you cannot play\nanother game.  ";
 */
static const char rules[] = "\nDo you want the rules of the game?";
static const char noteach[] = "Teachgammon not available!\n\007";
static const char need[] = "Do you need instructions for this program?";
static const char askcol[] =
	"Enter 'r' to play red, 'w' to play white, 'b' to play both:";
static const char rollr[] = "Red rolls a ";
static const char rollw[] = ".  White rolls a ";
static const char rstart[] = ".  Red starts.\n";
static const char wstart[] = ".  White starts.\n";
static const char toobad1[] = "Too bad, ";
static const char unable[] = " is unable to use that roll.\n";
static const char toobad2[] = ".  Too bad, ";
static const char cantmv[] = " can't move.\n";
static const char bgammon[] = "Backgammon!  ";
static const char gammon[] = "Gammon!  ";
static const char again[] = ".\nWould you like to play again?";
static const char svpromt[] = "Would you like to save this game?";

int
main (int argc, char **argv)
{
	int     i,l;		/* non-descript indices */
	char    c;		/* non-descript character storage */

	signal(SIGINT, getout);	/* trap interrupts */

	/* use whole screen for text */
	begscr = 0;

	getarg(argc, argv);

	initcurses();

	/* check if restored game and save flag for later */
	if ((rfl = rflag)) {
		if (pledge("stdio rpath wpath cpath tty", NULL) == -1)
			err(1, "pledge");

		wrboard();	/* print board */
		/* if new game, pretend to be a non-restored game */
		if (cturn == 0)
			rflag = 0;
	} else {
		if (pledge("stdio rpath wpath cpath tty exec", NULL) == -1)
			err(1, "pledge");

		rscore = wscore = 0;	/* zero score */

		if (aflag) {	/* print rules */
			addstr(rules);
			if (yorn(0)) {
				endwin();
				execl(TEACH, "teachgammon", (char *)NULL);

				err(1, "%s", noteach);
			} else {/* if not rules, then instructions */
				addstr(need);
				if (yorn(0)) {	/* print instructions */
					clear();
					text(instruct);
				}
			}
		}

		if (pledge("stdio rpath wpath cpath tty", NULL) == -1)
			err(1, "pledge");

		init();		/* initialize board */

		if (pnum == 2) {/* ask for color(s) */
			printw("\n%s", askcol);
			while (pnum == 2) {
				c = readc();
				switch (c) {

				case 'R':	/* red */
					pnum = -1;
					break;

				case 'W':	/* white */
					pnum = 1;
					break;

				case 'B':	/* both */
					pnum = 0;
					break;

				case 'P':	/* Control the dice */
					iroll = 1;
					addstr("\nDice controlled!\n");
					addstr(askcol);
					break;

				default:	/* error */
					beep();
				}
			}
		}

		wrboard();		/* print board */

		move(18, 0);
	}
	/* limit text to bottom of screen */
	begscr = 17;

	for (;;)  {			/* begin game! */
		/* initial roll if needed */
		if ((!rflag) || raflag)
			roll();

		/* perform ritual of first roll */
		if (!rflag) {
			move(17, 0);
			while (D0 == D1)	/* no doubles */
				roll();

			/* print rolls */
			printw("%s%d%s%d", rollr, D0, rollw, D1);

			/* winner goes first */
			if (D0 > D1) {
				addstr(rstart);
				cturn = 1;
			} else {
				addstr(wstart);
				cturn = -1;
			}
		}
		/* initialize variables according to whose turn it is */

		if (cturn == 1) {	/* red */
			home = 25;
			bar = 0;
			inptr = &in[1];
			inopp = &in[0];
			offptr = &off[1];
			offopp = &off[0];
			Colorptr = &color[1];
			colorptr = &color[3];
			colen = 3;
		} else {		/* white */
			home = 0;
			bar = 25;
			inptr = &in[0];
			inopp = &in[1];
			offptr = &off[0];
			offopp = &off[1];
			Colorptr = &color[0];
			colorptr = &color[2];
			colen = 5;
		}

		/* do first move (special case) */
		if (!(rflag && raflag)) {
			if (cturn == pnum)	/* computer's move */
				domove(0);
			else {	/* player's move */
				mvlim = movallow();
				/* reprint roll */
				move(cturn == -1 ? 18 : 19, 0);
				proll();
				getmove();	/* get player's move */
			}
		}
		move(17, 0);
		clrtoeol();
		begscr = 18;
		/* no longer any difference between normal and recovered game. */
		rflag = 0;

		/* move as long as it's someone's turn */
		while (cturn == 1 || cturn == -1) {

			/* board maintainence */
			moveplayers();	/* fix board */

			/* do computer's move */
			if (cturn == pnum) {
				domove(1);

				/* see if double refused */
				if (cturn == -2 || cturn == 2)
					break;

				/* check for winning move */
				if (*offopp == 15) {
					cturn *= -2;
					break;
				}
				continue;

			}
			/* (player's move) */

			/* clean screen if safe */
			if (hflag) {
				move(20, 0);
				clrtobot();
				hflag = 1;
			}
			/* if allowed, give him a chance to double */
			if (dflag && dlast != cturn && gvalue < 64) {
				move(cturn == -1 ? 18: 19, 0);
				addstr(*Colorptr);
				c = readc();

				/* character cases */
				switch (c) {

				case 'R':		/* reprint board */
					wrboard();
					break;

				case 'S':		/* save game */
					raflag = 1;
					save(1);
					break;

				case 'Q':		/* quit */
					quit();
					break;

				case 'D':		/* double */
					dble();
					break;

				case ' ':		/* roll */
				case '\n':
					roll();
					printw(" rolls %d %d.  ", D0, D1);

					/* see if he can move */
					if ((mvlim = movallow()) == 0) {

						/* can't move */
						printw("%s%s%s", toobad1, *colorptr, unable);
						if (pnum) {
							moveplayers();
							sleep(MVPAUSE);
						}
						nexturn();
						break;
					}

					getmove();

					/* okay to clean screen */
					hflag = 1;
					break;

				default:		/* invalid character */

					/* print help message */
					move(20, 0);
					text(helpm);
					move(cturn == -1 ? 18 : 19, 0);

					/* don't erase */
					hflag = 0;
				}
			} else {/* couldn't double */

				/* print roll */
				roll();
				move(cturn == -1 ? 18: 19, 0);
				proll();

				/* can he move? */
				if ((mvlim = movallow()) == 0) {

					/* he can't */
					printw("%s%s%s", toobad2, *colorptr, cantmv);
					moveplayers();
					sleep(MVPAUSE);
					nexturn();
					continue;
				}

				getmove();
			}
		}

		/* don't worry about who won if quit */
		if (cturn == 0)
			break;

		/* fix cturn = winner */
		cturn /= -2;

		/* final board pos. */
		moveplayers();

		/* backgammon? */
		mflag = 0;
		l = bar + 7 * cturn;
		for (i = bar; i != l; i += cturn)
			if (board[i] * cturn)
				mflag++;

		/* compute game value */
		move(20, 0);
		if (*offopp == 15) {
			if (mflag) {
				addstr(bgammon);
				gvalue *= 3;
			}
			else if (*offptr <= 0) {
				addstr(gammon);
				gvalue *= 2;
			}
		}
		/* report situation */
		if (cturn == -1) {
			addstr("Red wins ");
			rscore += gvalue;
		} else {
			addstr("White wins ");
			wscore += gvalue;
		}
		printw("%d point%s.\n", gvalue, (gvalue > 1) ? "s":"");

		/* write score */
		wrscore();

		/* see if he wants another game */
		addstr(again);
		if ((i = yorn('S')) == 0)
			break;

		init();
		if (i == 2) {
			addstr("  Save.\n");
			cturn = 0;
			save(0);
		}
		/* yes, reset game */
		wrboard();
	}

	/* give him a chance to save if game was recovered */
	if (rfl && cturn) {
		addstr(svpromt);
		if (yorn(0)) {
			/* re-initialize for recovery */
			init();
			cturn = 0;
			save(0);
		}
	}
	/* leave peacefully */
	getout(0);
	/* NOT REACHED */
}
