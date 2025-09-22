/*	$OpenBSD: print.c,v 1.10 2023/10/10 09:42:56 tb Exp $	*/
/*	$NetBSD: print.c,v 1.4 1995/03/24 05:02:02 cgd Exp $	*/

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

#include "mille.h"

/*
 * @(#)print.c	1.1 (Berkeley) 4/1/82
 */

# define	COMP_STRT	20
# define	CARD_STRT	2

void
prboard(void)
{
	PLAY	*pp;
	int	i, j, k, temp;

	for (k = 0; k < 2; k++) {
		pp = &Player[k];
		temp = k * COMP_STRT + CARD_STRT;
		for (i = 0; i < NUM_SAFE; i++)
			if (pp->safety[i] == S_PLAYED && !pp->sh_safety[i]) {
				mvaddstr(i, temp, C_name[i + S_CONV]);
				if (pp->coups[i])
					mvaddch(i, temp - CARD_STRT, '*');
				pp->sh_safety[i] = TRUE;
			}
		show_card(14, temp, pp->battle, &pp->sh_battle);
		show_card(16, temp, pp->speed, &pp->sh_speed);
		for (i = C_25; i <= C_200; i++) {
			const char	*name;
			int	end;

			if (pp->nummiles[i] == pp->sh_nummiles[i])
				continue;

			name = C_name[i];
			temp = k * 40;
			end = pp->nummiles[i];
			for (j = pp->sh_nummiles[i]; j < end; j++)
				mvwaddstr(Miles, i + 1, (j << 2) + temp, name);
			pp->sh_nummiles[i] = end;
		}
	}
	prscore(TRUE);
	temp = CARD_STRT;
	pp = &Player[PLAYER];
	for (i = 0; i < HAND_SZ; i++)
		show_card(i + 6, temp, pp->hand[i], &pp->sh_hand[i]);
	mvprintw(6, COMP_STRT + CARD_STRT, "%2td", Topcard - Deck);
	show_card(8, COMP_STRT + CARD_STRT, Discard, &Sh_discard);
	if (End == 1000) {
		move(EXT_Y, EXT_X);
		standout();
		addstr("Extension");
		standend();
	}
	wrefresh(Board);
	wrefresh(Miles);
	wrefresh(Score);
}

/*
 * show_card:
 *	Show the given card if it is different from the last one shown
 */
void
show_card(int y, int x, CARD c, CARD *lc)
{
	if (c == *lc)
		return;

	mvprintw(y, x, C_fmt, C_name[c]);
	*lc = c;
}

static char	Score_fmt[] = "%4d";

void
prscore(bool for_real)
{
	PLAY	*pp;
	int	x;

	stdscr = Score;
	for (pp = Player; pp < &Player[2]; pp++) {
		x = (pp - Player) * 6 + 21;
		show_score(1, x, pp->mileage, &pp->sh_mileage);
		if (pp->safescore != pp->sh_safescore) {
			mvprintw(2, x, Score_fmt, pp->safescore);
			if (pp->safescore == 400)
				mvaddstr(3, x + 1, "300");
			else
				mvaddstr(3, x + 1, "  0");
			mvprintw(4, x, Score_fmt, pp->coupscore);
			pp->sh_safescore = pp->safescore;
		}
		if (Window == W_FULL || Finished) {
#ifdef EXTRAP
			if (for_real)
				finalscore(pp);
			else
				extrapolate(pp);
#else
			finalscore(pp);
#endif
			show_score(11, x, pp->hand_tot, &pp->sh_hand_tot);
			show_score(13, x, pp->total, &pp->sh_total);
			show_score(14, x, pp->games, &pp->sh_games);
		}
		else {
			show_score(6, x, pp->hand_tot, &pp->sh_hand_tot);
			show_score(8, x, pp->total, &pp->sh_total);
			show_score(9, x, pp->games, &pp->sh_games);
		}
	}
	stdscr = Board;
}

/*
 * show_score:
 *	Show a score value if it is different from the last time we
 *	showed it.
 */
void
show_score(int y, int x, int s, int *ls)
{
	if (s == *ls)
		return;

	mvprintw(y, x, Score_fmt, s);
	*ls = s;
}
