/*	$OpenBSD: init.c,v 1.10 2016/01/08 18:09:59 mestre Exp $	*/
/*	$NetBSD: init.c,v 1.5 1995/03/24 05:01:40 cgd Exp $	*/

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

#include <stdlib.h>
#include <string.h>

#include "mille.h"

/*
 * @(#)init.c	1.1 (Berkeley) 4/1/82
 */

void
init(void)
{
	PLAY	*pp;
	int	i, j;
	CARD	card;

	memset(Numseen, 0, sizeof Numseen);
	Numgos = 0;

	for (i = 0; i < 2; i++) {
		pp = &Player[i];
		pp->hand[0] = C_INIT;
		for (j = 0; j < NUM_SAFE; j++) {
			pp->safety[j] = S_UNKNOWN;
			pp->coups[j] = FALSE;
		}
		for (j = 1; j < HAND_SZ; j++) {
			pp->hand[j] = *--Topcard;
			if (i == COMP) {
				account(card = *Topcard);
				if (is_safety(card))
					pp->safety[card - S_CONV] = S_IN_HAND;
			}
		}
		pp->mileage = 0;
		pp->hand_tot = 0;
		pp->safescore = 0;
		pp->coupscore = 0;
		pp->can_go = FALSE;
		pp->speed = C_INIT;
		pp->battle = C_INIT;
		pp->new_speed = FALSE;
		pp->new_battle = FALSE;
		for (j = 0; j < NUM_MILES; j++)
			pp->nummiles[j] = 0;
	}
	if (Order)
		sort(Player[PLAYER].hand);
	Discard = C_INIT;
	Finished = FALSE;
	End = 700;
}

void
shuffle(void)
{
	int	i, r;
	CARD	temp;

	for (i = DECK_SZ - 1; i > 0; i--) {
		r = arc4random_uniform(i + 1);
		temp = Deck[r];
		Deck[r] = Deck[i];
		Deck[i] = temp;
	}
	Topcard = &Deck[DECK_SZ];
}

void
newboard(void)
{
	int		i;
	PLAY		*pp;
	static int	first = TRUE;

	if (first) {
		werase(Board);
		werase(Score);
		mvaddstr(5, 0, "--HAND--");
		mvaddch(6, 0, 'P');
		mvaddch(7, 0, '1');
		mvaddch(8, 0, '2');
		mvaddch(9, 0, '3');
		mvaddch(10, 0, '4');
		mvaddch(11, 0, '5');
		mvaddch(12, 0, '6');
		mvaddstr(13, 0, "--BATTLE--");
		mvaddstr(15, 0, "--SPEED--");
		mvaddstr(5, 20, "--DECK--");
		mvaddstr(7, 20, "--DISCARD--");
		mvaddstr(13, 20, "--BATTLE--");
		mvaddstr(15, 20, "--SPEED--");
		mvwaddstr(Miles, 0, 0, "--MILEAGE--");
		mvwaddstr(Miles, 0, 41, "--MILEAGE--");
		Sh_discard = -1;
		for (pp = Player; pp <= &Player[COMP]; pp++) {
			for (i = 0; i < HAND_SZ; i++)
				pp->sh_hand[i] = -1;
			pp->sh_battle = -1;
			pp->sh_speed = -1;
			pp->sh_mileage = -1;
		}
		first = FALSE;
	}
	else {
		for (i = 0; i < 5; i++) {
			move(i, 0);
			clrtoeol();
		}
		wmove(Miles, 1, 0);
		wclrtobot(Miles);
		wmove(Board, MOVE_Y + 1, MOVE_X);
		wclrtoeol(Board);
		wmove(Board, MOVE_Y + 2, MOVE_X);
		wclrtoeol(Board);
	}
	Sh_discard = -1;
	for (pp = Player; pp <= &Player[COMP]; pp++) {
		for (i = 0; i < NUM_SAFE; i++)
			pp->sh_safety[i] = FALSE;
		for (i = 0; i < NUM_MILES; i++)
			pp->sh_nummiles[i] = 0;
		pp->sh_safescore = -1;
	}
	newscore();
}

void
newscore(void)
{
	int		i, new;
	PLAY		*pp;
	static int	was_full = -1;
	static int	last_win = -1;

	if (was_full < 0)
		was_full = (Window != W_FULL);
	stdscr = Score;
	move(0, 22);
	new = FALSE;
	if (inch() != 'Y') {
		erase();
		mvaddstr(0, 22,  "You   Comp   Value");
		mvaddstr(1, 2, "Milestones Played");
		mvaddstr(2, 8, "Each Safety");
		mvaddstr(3, 5, "All 4 Safeties");
		mvaddstr(4, 3, "Each Coup Fourre");
		mvaddstr(2, 37, "100");
		mvaddstr(3, 37, "300");
		mvaddstr(4, 37, "300");
		new = TRUE;
	}
	else if ((Window == W_FULL || Finished) ^ was_full) {
		move(5, 1);
		clrtobot();
		new = TRUE;
	}
	else if (Window != last_win)
		new = TRUE;
	if (new) {
		for (i = 0; i < SCORE_Y; i++)
			mvaddch(i, 0, '|');
		move(SCORE_Y - 1, 1);
		for (i = 0; i < SCORE_X; i++)
			addch('_');
		for (pp = Player; pp <= &Player[COMP]; pp++) {
			pp->sh_hand_tot = -1;
			pp->sh_total = -1;
			pp->sh_games = -1;
			pp->sh_safescore = -1;
		}
	}
	Player[PLAYER].was_finished = !Finished;
	Player[COMP].was_finished = !Finished;
	if (Window == W_FULL || Finished) {
		if (!was_full || new) {
			mvaddstr(5, 5, "Trip Completed");
			mvaddstr(6, 10, "Safe Trip");
			mvaddstr(7, 5, "Delayed Action");
			mvaddstr(8, 10, "Extension");
			mvaddstr(9, 11, "Shut-Out");
			mvaddstr(10, 21, "----   ----   -----");
			mvaddstr(11, 9, "Hand Total");
			mvaddstr(12, 20, "-----  -----");
			mvaddstr(13, 6, "Overall Total");
			mvaddstr(14, 15, "Games");
			mvaddstr(5, 37, "400");
			mvaddstr(6, 37, "300");
			mvaddstr(7, 37, "300");
			mvaddstr(8, 37, "200");
			mvaddstr(9, 37, "500");
		}
	}
	else
		if (was_full || new) {
			mvaddstr(5, 21, "----   ----   -----");
			mvaddstr(6, 9, "Hand Total");
			mvaddstr(7, 20, "-----  -----");
			mvaddstr(8, 6, "Overall Total");
			mvaddstr(9, 15, "Games");
			mvaddstr(11, 2, "p: pick");
			mvaddstr(12, 2, "u: use #");
			mvaddstr(13, 2, "d: discard #");
			mvaddstr(14, 2, "w: toggle window");
			mvaddstr(11, 21, "q: quit");
			if (!Order)
				mvaddstr(12, 21, "o: order hand");
			else
				mvaddstr(12, 21, "o: stop ordering");
			mvaddstr(13, 21, "s: save");
			mvaddstr(14, 21, "r: reprint");
		}
	stdscr = Board;
	was_full = (Window == W_FULL || Finished);
	last_win = Window;
}
