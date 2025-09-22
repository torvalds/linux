/*	$OpenBSD: support.c,v 1.13 2015/12/31 18:10:20 mestre Exp $	*/
/*	$NetBSD: support.c,v 1.3 1995/03/21 15:08:59 cgd Exp $	*/

/*-
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
#include <string.h>

#include "cribbage.h"
#include "cribcur.h"

#define	NTV	10		/* number scores to test */

/* score to test reachability of, and order to test them in */
int tv[NTV] = {8, 7, 9, 6, 11, 12, 13, 14, 10, 5};

/*
 * computer chooses what to play in pegging...
 * only called if no playable card will score points
 */
int
cchose(CARD h[], int n, int s)
{
	int i, j, l;

	if (n <= 1)
		return (0);
	if (s < 4) {		/* try for good value */
		if ((j = anysumto(h, n, s, 4)) >= 0)
			return (j);
		if ((j = anysumto(h, n, s, 3)) >= 0 && s == 0)
			return (j);
	}
	if (s > 0 && s < 20) {
				/* try for retaliation to 31 */
		for (i = 1; i <= 10; i++) {
			if ((j = anysumto(h, n, s, 21 - i)) >= 0) {
				if ((l = numofval(h, n, i)) > 0) {
					if (l > 1 || VAL(h[j].rank) != i)
						return (j);
				}
			}
		}
	}
	if (s < 15) {
				/* for retaliation after 15 */
		for (i = 0; i < NTV; i++) {
			if ((j = anysumto(h, n, s, tv[i])) >= 0) {
				if ((l = numofval(h, n, 15 - tv[i])) > 0) {
					if (l > 1 ||
					    VAL(h[j].rank) != 15 - tv[i])
						return (j);
				}
			}
		}
	}
	j = -1;
				/* remember: h is sorted */
	for (i = n - 1; i >= 0; --i) {
		l = s + VAL(h[i].rank);
		if (l > 31)
			continue;
		if (l != 5 && l != 10 && l != 21) {
			j = i;
			break;
		}
	}
	if (j >= 0)
		return (j);
	for (i = n - 1; i >= 0; --i) {
		l = s + VAL(h[i].rank);
		if (l > 31)
			continue;
		if (j < 0)
			j = i;
		if (l != 5 && l != 21) {
			j = i;
			break;
		}
	}
	if (j < 0)
		errx(1, "cchose internal error %d %d", j, n);
	return (j);
}

/*
 * plyrhand:
 *	Evaluate and score a player hand or crib
 */
int
plyrhand(CARD hand[], char *s)
{
	static char prompt[BUFSIZ];
	int i, j;
	bool win;

	prhand(hand, CINHAND, Playwin, FALSE);
	(void) snprintf(prompt, sizeof prompt, "Your %s scores ", s);
	i = scorehand(hand, turnover, CINHAND, strcmp(s, "crib") == 0, explain);
	if ((j = number(0, 29, prompt)) == 19)
		j = 0;
	if (i != j) {
		if (i < j) {
			win = chkscr(&pscore, i);
			if (!win) {
				msg("It's really only %d points; I get %d", i, 2);
				win = chkscr(&cscore, 2);
			} else
				msg("It's really only %d points.", i);
		} else {
			win = chkscr(&pscore, j);
			msg("You should have taken %d, not %d!", i, j);
			if (!win && muggins) {
				msg("Muggins!  I score %d", i - j);
				win = chkscr(&cscore, i - j);
			}
		}
		if (explain)
			msg("Explanation: %s", expl_string);
		do_wait();
	} else
		win = chkscr(&pscore, i);
	return (win);
}

/*
 * comphand:
 *	Handle scoring and displaying the computers hand
 */
int
comphand(CARD h[], char *s)
{
	int j;

	j = scorehand(h, turnover, CINHAND, strcmp(s, "crib") == 0, FALSE);
	prhand(h, CINHAND, Compwin, FALSE);
	msg("My %s scores %d", s, (j == 0 ? 19 : j));
	return (chkscr(&cscore, j));
}

/*
 * chkscr:
 *	Add inc to scr and test for > glimit, printing on the scoring
 *	board while we're at it.
 */
int Lastscore[2] = {-1, -1};

int
chkscr(int *scr, int inc)
{
	bool myturn;

	myturn = (scr == &cscore);
	if (inc != 0) {
		prpeg(Lastscore[(int)myturn], '.', myturn);
		Lastscore[(int)myturn] = *scr;
		*scr += inc;
		prpeg(*scr, PEG, myturn);
		refresh();
	}
	return (*scr >= glimit);
}

/*
 * prpeg:
 *	Put out the peg character on the score board and put the
 *	score up on the board.
 */
void
prpeg(int score, int peg, bool myturn)
{
	int y, x;

	if (!myturn)
		y = SCORE_Y + 2;
	else
		y = SCORE_Y + 5;

	if (score <= 0 || score >= glimit) {
		if (peg == '.')
			peg = ' ';
		if (score == 0)
			x = SCORE_X + 2;
		else {
			x = SCORE_X + 2;
			y++;
		}
	} else {
		x = (score - 1) % 30;
		if (score > 90 || (score > 30 && score <= 60)) {
			y++;
			x = 29 - x;
		}
		x += x / 5;
		x += SCORE_X + 3;
	}
	mvaddch(y, x, peg);
	mvprintw(SCORE_Y + (myturn ? 7 : 1), SCORE_X + 10, "%3d", score);
}

/*
 * cdiscard -- the computer figures out what is the best discard for
 * the crib and puts the best two cards at the end
 */
void
cdiscard(bool mycrib)
{
	CARD    d[CARDS], h[FULLHAND], cb[2];
	int i, j, k;
	int     nc, ns;
	long    sums[15];
	static int undo1[15] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4};
	static int undo2[15] = {1, 2, 3, 4, 5, 2, 3, 4, 5, 3, 4, 5, 4, 5, 5};

	makedeck(d);
	nc = CARDS;
	for (i = 0; i < knownum; i++) {	/* get all other cards */
		cremove(known[i], d, nc--);
	}
	for (i = 0; i < 15; i++)
		sums[i] = 0L;
	ns = 0;
	for (i = 0; i < (FULLHAND - 1); i++) {
		cb[0] = chand[i];
		for (j = i + 1; j < FULLHAND; j++) {
			cb[1] = chand[j];
			for (k = 0; k < FULLHAND; k++)
				h[k] = chand[k];
			cremove(chand[i], h, FULLHAND);
			cremove(chand[j], h, FULLHAND - 1);
			for (k = 0; k < nc; k++) {
				sums[ns] +=
				    scorehand(h, d[k], CINHAND, TRUE, FALSE);
				if (mycrib)
					sums[ns] += adjust(cb, d[k]);
				else
					sums[ns] -= adjust(cb, d[k]);
			}
			++ns;
		}
	}
	j = 0;
	for (i = 1; i < 15; i++)
		if (sums[i] > sums[j])
			j = i;
	for (k = 0; k < FULLHAND; k++)
		h[k] = chand[k];
	cremove(h[undo1[j]], chand, FULLHAND);
	cremove(h[undo2[j]], chand, FULLHAND - 1);
	chand[4] = h[undo1[j]];
	chand[5] = h[undo2[j]];
}

/*
 * returns true if some card in hand can be played without exceeding 31
 */
int
anymove(CARD hand[], int n, int sum)
{
	int i, j;

	if (n < 1)
		return (FALSE);
	j = hand[0].rank;
	for (i = 1; i < n; i++) {
		if (hand[i].rank < j)
			j = hand[i].rank;
	}
	return (sum + VAL(j) <= 31);
}

/*
 * anysumto returns the index (0 <= i < n) of the card in hand that brings
 * the s up to t, or -1 if there is none
 */
int
anysumto(CARD hand[], int n, int s, int t)
{
	int i;

	for (i = 0; i < n; i++) {
		if (s + VAL(hand[i].rank) == t)
			return (i);
	}
	return (-1);
}

/*
 * return the number of cards in h having the given rank value
 */
int
numofval(CARD h[], int n, int v)
{
	int i, j;

	j = 0;
	for (i = 0; i < n; i++) {
		if (VAL(h[i].rank) == v)
			++j;
	}
	return (j);
}

/*
 * makeknown remembers all n cards in h for future recall
 */
void
makeknown(CARD h[], int n)
{
	int i;

	for (i = 0; i < n; i++)
		known[knownum++] = h[i];
}
