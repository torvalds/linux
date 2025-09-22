/*	$OpenBSD: comp.c,v 1.10 2016/01/08 18:09:59 mestre Exp $	*/
/*	$NetBSD: comp.c,v 1.4 1995/03/24 05:01:11 cgd Exp $	*/

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
 * @(#)comp.c	1.1 (Berkeley) 4/1/82
 */

# define	V_VALUABLE	40

void
calcmove(void)
{
	CARD		card;
	int		*value;
	PLAY		*pp, *op;
	bool		foundend, cango, canstop, foundlow;
	unsigned int	i, count200, badcount, nummin, nummax, diff;
	int		curmin, curmax;
	CARD		safe, oppos;
	int		valbuf[HAND_SZ], count[NUM_CARDS];
	bool		playit[HAND_SZ];

	wmove(Score, ERR_Y, ERR_X);	/* get rid of error messages	*/
	wclrtoeol(Score);
	pp = &Player[COMP];
	op = &Player[PLAYER];
	safe = 0;
	cango = 0;
	canstop = FALSE;
	foundend = FALSE;

	/* Try for a Coup Forre, and see what we have. */
	for (i = 0; i < NUM_CARDS; i++)
		count[i] = 0;
	for (i = 0; i < HAND_SZ; i++) {
		card = pp->hand[i];
		switch (card) {
		  case C_STOP:	case C_CRASH:
		  case C_FLAT:	case C_EMPTY:
			if ((playit[i] = canplay(pp, op, card)))
				canstop = TRUE;
			goto norm;
		  case C_LIMIT:
			if ((playit[i] = canplay(pp, op, card))
			    && Numseen[C_25] == Numcards[C_25]
			    && Numseen[C_50] == Numcards[C_50])
				canstop = TRUE;
			goto norm;
		  case C_25:	case C_50:	case C_75:
		  case C_100:	case C_200:
			if ((playit[i] = canplay(pp, op, card))
			    && pp->mileage + Value[card] == End)
				foundend = TRUE;
			goto norm;
		  default:
			playit[i] = canplay(pp, op, card);
norm:
			if (playit[i])
				++cango;
			break;
		  case C_GAS_SAFE:	case C_DRIVE_SAFE:
		  case C_SPARE_SAFE:	case C_RIGHT_WAY:
			if (pp->battle == opposite(card) ||
			    (pp->speed == C_LIMIT && card == C_RIGHT_WAY)) {
				Movetype = M_PLAY;
				Card_no = i;
				return;
			}
			++safe;
			playit[i] = TRUE;
			break;
		}
		if (card >= 0)
			++count[card];
	}

	/* No Coup Forre.  Draw to fill hand, then restart, as needed. */
	if (pp->hand[0] == C_INIT && Topcard > Deck) {
		Movetype = M_DRAW;
		return;
	}

#ifdef DEBUG
	if (Debug)
		fprintf(outf, "CALCMOVE: cango = %d, canstop = %d, safe = %d\n",
			cango, canstop, safe);
#endif
	if (foundend)
		foundend = !check_ext(TRUE);
	for (i = 0; safe && i < HAND_SZ; i++) {
		if (is_safety(pp->hand[i])) {
			if (onecard(op) || (foundend && cango && !canstop)) {
#ifdef DEBUG
				if (Debug)
					fprintf(outf,
						"CALCMOVE: onecard(op) = %d, foundend = %d\n",
						onecard(op), foundend);
#endif
playsafe:
				Movetype = M_PLAY;
				Card_no = i;
				return;
			}
			oppos = opposite(pp->hand[i]);
			if (Numseen[oppos] == Numcards[oppos] &&
			    !(pp->hand[i] == C_RIGHT_WAY &&
			      Numseen[C_LIMIT] != Numcards[C_LIMIT]))
				goto playsafe;
			else if (!cango
			    && (op->can_go || !pp->can_go || Topcard < Deck)) {
				card = (Topcard - Deck) - roll(1, 10);
				if ((!pp->mileage) != (!op->mileage))
					card -= 7;
#ifdef DEBUG
				if (Debug)
					fprintf(outf,
						"CALCMOVE: card = %d, DECK_SZ / 4 = %d\n",
						card, DECK_SZ / 4);
#endif
				if (card < DECK_SZ / 4)
					goto playsafe;
			}
			safe--;
			playit[i] = cango;
		}
	}
	if (!pp->can_go && !is_repair(pp->battle))
		Numneed[opposite(pp->battle)]++;
redoit:
	foundlow = (cango || count[C_END_LIMIT] != 0
			  || Numseen[C_LIMIT] == Numcards[C_LIMIT]
			  || pp->safety[S_RIGHT_WAY] != S_UNKNOWN);
	foundend = FALSE;
	count200 = pp->nummiles[C_200];
	badcount = 0;
	curmax = -1;
	curmin = 101;
	nummin = -1;
	nummax = -1;
	value = valbuf;
	for (i = 0; i < HAND_SZ; i++) {
		card = pp->hand[i];
		if (is_safety(card) || playit[i] == (cango != 0)) {
#ifdef DEBUG
			if (Debug)
				fprintf(outf, "CALCMOVE: switch(\"%s\")\n",
					C_name[card]);
#endif
			switch (card) {
			  case C_25:	case C_50:
				diff = End - pp->mileage;
				/* avoid getting too close */
				if (Topcard > Deck && cango && diff <= 100
				    && diff / Value[card] > count[card]
				    && (card == C_25 || diff % 50 == 0)) {
					if (card == C_50 && diff - 50 == 25
					    && count[C_25] > 0)
						goto okay;
					*value = 0;
					if (--cango <= 0)
						goto redoit;
					break;
				}
okay:
				*value = (Value[card] >> 3);
				if (pp->speed == C_LIMIT)
					++*value;
				else
					--*value;
				if (!foundlow
				   && (card == C_50 || count[C_50] == 0)) {
					*value = (pp->mileage ? 10 : 20);
					foundlow = TRUE;
				}
				goto miles;
			  case C_200:
				if (++count200 > 2) {
					*value = 0;
					break;
				}
			  case C_75:	case C_100:
				*value = (Value[card] >> 3);
				if (pp->speed == C_LIMIT)
					--*value;
				else
					++*value;
miles:
				if (pp->mileage + Value[card] > End)
					*value = (End == 700 ? card : 0);
				else if (pp->mileage + Value[card] == End) {
					*value = (foundend ? card : V_VALUABLE);
					foundend = TRUE;
				}
				break;
			  case C_END_LIMIT:
				if (pp->safety[S_RIGHT_WAY] != S_UNKNOWN)
					*value = (pp->safety[S_RIGHT_WAY] ==
						  S_PLAYED ? -1 : 1);
				else if (pp->speed == C_LIMIT &&
					 End - pp->mileage <= 50)
					*value = 1;
				else if (pp->speed == C_LIMIT
				    || Numseen[C_LIMIT] != Numcards[C_LIMIT]) {
					safe = S_RIGHT_WAY;
					oppos = C_LIMIT;
					goto repair;
				}
				else {
					*value = 0;
					--count[C_END_LIMIT];
				}
				break;
			  case C_REPAIRS:	case C_SPARE:	case C_GAS:
				safe = safety(card) - S_CONV;
				oppos = opposite(card);
				if (pp->safety[safe] != S_UNKNOWN)
					*value = (pp->safety[safe] ==
						  S_PLAYED ? -1 : 1);
				else if (pp->battle != oppos
				    && (Numseen[oppos] == Numcards[oppos] ||
					Numseen[oppos] + count[card] >
					Numcards[oppos])) {
					*value = 0;
					--count[card];
				}
				else {
repair:
					*value = Numcards[oppos] * 6;
					*value += Numseen[card] -
						  Numseen[oppos];
					if (!cango)
					    *value /= (count[card]*count[card]);
					count[card]--;
				}
				break;
			  case C_GO:
				if (pp->safety[S_RIGHT_WAY] != S_UNKNOWN)
					*value = (pp->safety[S_RIGHT_WAY] ==
						  S_PLAYED ? -1 : 2);
				else if (pp->can_go
				 && Numgos + count[C_GO] == Numneed[C_GO]) {
					*value = 0;
					--count[C_GO];
				}
				else {
					*value = Numneed[C_GO] * 3;
					*value += (Numseen[C_GO] - Numgos);
					*value /= (count[C_GO] * count[C_GO]);
					count[C_GO]--;
				}
				break;
			  case C_LIMIT:
				if (op->mileage + 50 >= End) {
					*value = (End == 700 && !cango);
					break;
				}
				if (canstop || (cango && !op->can_go))
					*value = 1;
				else {
					*value = (pp->safety[S_RIGHT_WAY] !=
						  S_UNKNOWN ? 2 : 3);
					safe = S_RIGHT_WAY;
					oppos = C_END_LIMIT;
					goto normbad;
				}
				break;
			  case C_CRASH:	case C_EMPTY:	case C_FLAT:
				safe = safety(card) - S_CONV;
				oppos = opposite(card);
				*value = (pp->safety[safe]!=S_UNKNOWN ? 3 : 4);
normbad:
				if (op->safety[safe] == S_PLAYED)
					*value = -1;
				else {
					*value *= Numneed[oppos] +
						  Numseen[oppos] + 2;
					if (!pp->mileage || foundend ||
					    onecard(op))
						*value += 5;
					if (op->mileage == 0 || onecard(op))
						*value += 5;
					if (op->speed == C_LIMIT)
						*value -= 3;
					if (cango &&
					    pp->safety[safe] != S_UNKNOWN)
						*value += 3;
					if (!cango)
						*value /= ++badcount;
				}
				break;
			  case C_STOP:
				if (op->safety[S_RIGHT_WAY] == S_PLAYED)
					*value = -1;
				else {
					*value = (pp->safety[S_RIGHT_WAY] !=
						  S_UNKNOWN ? 3 : 4);
					*value *= Numcards[C_STOP] +
						  Numseen[C_GO];
					if (!pp->mileage || foundend ||
					    onecard(op))
						*value += 5;
					if (!cango)
						*value /= ++badcount;
					if (op->mileage == 0)
						*value += 5;
					if ((op->speed == C_LIMIT) ||
					    !op->can_go)
						*value -= 5;
					if (cango && pp->safety[S_RIGHT_WAY] !=
						     S_UNKNOWN)
						*value += 5;
				}
				break;
			  case C_GAS_SAFE:	case C_DRIVE_SAFE:
			  case C_SPARE_SAFE:	case C_RIGHT_WAY:
				*value = cango ? 0 : 101;
				break;
			  case C_INIT:
				*value = 0;
				break;
			}
		}
		else
			*value = cango ? 0 : 101;
		if (card != C_INIT) {
			if (*value >= curmax) {
				nummax = i;
				curmax = *value;
			}
			if (*value <= curmin) {
				nummin = i;
				curmin = *value;
			}
		}
#ifdef DEBUG
		if (Debug)
			mvprintw(i + 6, 2, "%3d %-14s", *value,
				 C_name[pp->hand[i]]);
#endif
		value++;
	}
	if (!pp->can_go && !is_repair(pp->battle))
		Numneed[opposite(pp->battle)]++;
	if (cango) {
play_it:
		mvaddstr(MOVE_Y + 1, MOVE_X, "PLAY\n");
		Movetype = M_PLAY;
		Card_no = nummax;
	}
	else {
		if (is_safety(pp->hand[nummin])) { /* NEVER discard a safety */
			nummax = nummin;
			goto play_it;
		}
		mvaddstr(MOVE_Y + 1, MOVE_X, "DISCARD\n");
		Movetype = M_DISCARD;
		Card_no = nummin;
	}
	mvprintw(MOVE_Y + 2, MOVE_X, "%16s", C_name[pp->hand[Card_no]]);
}

/*
 * Return true if the given player could conceivably win with his next card.
 */
int
onecard(const PLAY *pp)
{
	CARD	bat, spd, card;

	bat = pp->battle;
	spd = pp->speed;
	card = -1;
	if (pp->can_go || ((is_repair(bat) || bat == C_STOP || spd == C_LIMIT) &&
			   Numseen[S_RIGHT_WAY] != 0) ||
	    (bat >= 0 && Numseen[safety(bat)] != 0))
		switch (End - pp->mileage) {
		  case 200:
			if (pp->nummiles[C_200] == 2)
				return FALSE;
			card = C_200;
			/* FALLTHROUGH */
		  case 100:
		  case 75:
			if (card == -1)
				card = (End - pp->mileage == 75 ? C_75 : C_100);
			if (spd == C_LIMIT)
				return Numseen[S_RIGHT_WAY] == 0;
		  case 50:
		  case 25:
			if (card == -1)
				card = (End - pp->mileage == 25 ? C_25 : C_50);
			return Numseen[card] != Numcards[card];
		}
	return FALSE;
}

int
canplay(const PLAY *pp, const PLAY *op, CARD card)
{
	switch (card) {
	  case C_200:
		if (pp->nummiles[C_200] == 2)
			break;
		/* FALLTHROUGH */
	  case C_75:	case C_100:
		if (pp->speed == C_LIMIT)
			break;
		/* FALLTHROUGH */
	  case C_50:
		if (pp->mileage + Value[card] > End)
			break;
		/* FALLTHROUGH */
	  case C_25:
		if (pp->can_go)
			return TRUE;
		break;
	  case C_EMPTY:	case C_FLAT:	case C_CRASH:
	  case C_STOP:
		if (op->can_go && op->safety[safety(card) - S_CONV] != S_PLAYED)
			return TRUE;
		break;
	  case C_LIMIT:
		if (op->speed != C_LIMIT &&
		    op->safety[S_RIGHT_WAY] != S_PLAYED &&
		    op->mileage + 50 < End)
			return TRUE;
		break;
	  case C_GAS:	case C_SPARE:	case C_REPAIRS:
		if (pp->battle == opposite(card))
			return TRUE;
		break;
	  case C_GO:
		if (!pp->can_go &&
		    (is_repair(pp->battle) || pp->battle == C_STOP))
			return TRUE;
		break;
	  case C_END_LIMIT:
		if (pp->speed == C_LIMIT)
			return TRUE;
	}
	return FALSE;
}
