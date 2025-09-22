/*	$OpenBSD: extern.c,v 1.8 2016/01/08 18:05:58 mestre Exp $	*/
/*	$NetBSD: extern.c,v 1.4 1995/03/24 05:01:36 cgd Exp $	*/

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
 * @(#)extern.c	1.1 (Berkeley) 4/1/82
 */

bool	Debug,			/* set if debugging code on		*/
	Finished,		/* set if current hand is finished	*/
	Next,			/* set if changing players		*/
	On_exit,		/* set if game saved on exiting		*/
	Order,			/* set if hand should be sorted		*/
	Saved;			/* set if game just saved		*/

char	Initstr[100],		/* initial string for error field	*/
	*C_fmt = "%-18.18s";	/* format for printing cards	*/
const char	*Fromfile = NULL,	/* startup file for game		*/
	*const _cn[NUM_CARDS] = {	/* Card name buffer		*/
		"",
		"25",
		"50",
		"75",
		"100",
		"200",
		"Out of Gas",
		"Flat Tire",
		"Accident",
		"Stop",
		"Speed Limit",
		"Gasoline",
		"Spare Tire",
		"Repairs",
		"Go",
		"End of Limit",
		"Extra Tank",
		"Puncture Proof",
		"Driving Ace",
		"Right of Way"
	},
	*const *C_name = &_cn[1];	/* Card names			*/

int	Card_no,		/* Card number for current move		*/
	End,			/* End value for current hand		*/
	Handstart = COMP,	/* Player who starts hand		*/
	Movetype,		/* Current move type			*/
	Play,			/* Current player			*/
	Numgos,			/* Number of Go cards used by computer	*/
	Window = W_SMALL,	/* Current window wanted		*/
	Numseen[NUM_CARDS];	/* Number of cards seen in current hand	*/

const int	Value[NUM_MILES] = {	/* Value of mileage cards	*/
		25, 50, 75, 100, 200
	},
	Numcards[NUM_CARDS] = {	/* Number of cards in deck		*/
		10,	/* C_25 */
		10,	/* C_50 */
		10,	/* C_75 */
		12,	/* C_100 */
		4,	/* C_200 */
		2,	/* C_EMPTY */
		2,	/* C_FLAT */
		2,	/* C_CRASH */
		4,	/* C_STOP */
		3,	/* C_LIMIT */
		6,	/* C_GAS */
		6,	/* C_SPARE */
		6,	/* C_REPAIRS */
		14,	/* C_GO */
		6,	/* C_END_LIMIT */
		1,	/* C_GAS_SAFE */
		1,	/* C_SPARE_SAFE */
		1,	/* C_DRIVE_SAFE */
		1,	/* C_RIGHT_WAY */
		0	/* C_INIT */
	};
int	Numneed[NUM_CARDS] = {	/* number of cards needed per hand	*/
		0,	/* C_25 */
		0,	/* C_50 */
		0,	/* C_75 */
		0,	/* C_100 */
		0,	/* C_200 */
		2,	/* C_EMPTY */
		2,	/* C_FLAT */
		2,	/* C_CRASH */
		4,	/* C_STOP */
		3,	/* C_LIMIT */
		2,	/* C_GAS */
		2,	/* C_SPARE */
		2,	/* C_REPAIRS */
		10,	/* C_GO */
		3,	/* C_END_LIMIT */
		1,	/* C_GAS_SAFE */
		1,	/* C_SPARE_SAFE */
		1,	/* C_DRIVE_SAFE */
		1,	/* C_RIGHT_WAY */
		0	/* C_INIT */
	};

const CARD	Opposite[NUM_CARDS] = {	/* Opposites of each card	*/
		C_25, C_50, C_75, C_100, C_200, C_GAS, C_SPARE,
		C_REPAIRS, C_GO, C_END_LIMIT, C_EMPTY, C_FLAT, C_CRASH,
		C_STOP, C_LIMIT, C_EMPTY, C_FLAT, C_CRASH, C_STOP, C_INIT
	};
CARD	Discard,		/* Top of discard pile			*/
	Sh_discard,		/* Last discard card shown		*/
	*Topcard,		/* Pointer to next card to be picked	*/
	Deck[DECK_SZ] = {	/* Current deck				*/
		C_25, C_25, C_25, C_25, C_25, C_25, C_25, C_25, C_25, C_25,
		C_50, C_50, C_50, C_50, C_50, C_50, C_50, C_50, C_50, C_50,
		C_75, C_75, C_75, C_75, C_75, C_75, C_75, C_75, C_75, C_75,
		C_100, C_100, C_100, C_100, C_100, C_100, C_100, C_100, C_100,
		C_100, C_100, C_100,
		C_200, C_200, C_200, C_200,
		C_EMPTY, C_EMPTY,
		C_FLAT, C_FLAT,
		C_CRASH, C_CRASH,
		C_STOP, C_STOP, C_STOP, C_STOP,
		C_LIMIT, C_LIMIT, C_LIMIT,
		C_GAS, C_GAS, C_GAS, C_GAS, C_GAS, C_GAS,
		C_SPARE, C_SPARE, C_SPARE, C_SPARE, C_SPARE, C_SPARE,
		C_REPAIRS, C_REPAIRS, C_REPAIRS, C_REPAIRS, C_REPAIRS,
			C_REPAIRS,
		C_END_LIMIT, C_END_LIMIT, C_END_LIMIT, C_END_LIMIT, C_END_LIMIT,
			C_END_LIMIT,
		C_GO, C_GO, C_GO, C_GO, C_GO, C_GO, C_GO, C_GO, C_GO, C_GO,
			C_GO, C_GO, C_GO, C_GO,
		C_GAS_SAFE, C_SPARE_SAFE, C_DRIVE_SAFE, C_RIGHT_WAY
	};

FILE	*outf;

PLAY	Player[2];		/* Player descriptions			*/

WINDOW	*Board,			/* Playing field screen			*/
	*Miles,			/* Mileage screen			*/
	*Score;			/* Score screen				*/

