/*	$OpenBSD: cards.c,v 1.12 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: cards.c,v 1.3 1995/03/23 08:34:35 cgd Exp $	*/

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
#include <stdio.h>
#include <stdlib.h>

#include "monop.ext"
#include "pathnames.h"

/*
 *	These routine deal with the card decks
 */

#define	GOJF	'F'	/* char for get-out-of-jail-free cards	*/

static char	*cardfile	= _PATH_CARDS;

static FILE	*deckf;

static void set_up(DECK *);
static void printmes(void);

/*
 *	This routine initializes the decks from the data file,
 * which it opens.
 */
void
init_decks(void)
{
	if ((deckf = fopen(cardfile, "r")) == NULL)
file_err:
		err(1, "%s", cardfile);
	if (fread(&deck[0].num_cards, sizeof(deck[0].num_cards), 1, deckf) != 1)
		goto file_err;
	if (fread(&deck[0].top_card, sizeof(deck[0].top_card), 1, deckf) != 1)
		goto file_err;
	if (fread(&deck[0].gojf_used, sizeof(deck[0].gojf_used), 1, deckf) != 1)
		goto file_err;
	deck[0].num_cards = ntohs(deck[0].num_cards);
	deck[0].top_card = ntohs(deck[0].top_card);

	if (fread(&deck[1].num_cards, sizeof(deck[1].num_cards), 1, deckf) != 1)
		goto file_err;
	if (fread(&deck[1].top_card, sizeof(deck[1].top_card), 1, deckf) != 1)
		goto file_err;
	if (fread(&deck[1].gojf_used, sizeof(deck[1].gojf_used), 1, deckf) != 1)
		goto file_err;
	deck[1].num_cards = ntohs(deck[1].num_cards);
	deck[1].top_card = ntohs(deck[1].top_card);

	set_up(&CC_D);
	set_up(&CH_D);
}
/*
 *	This routine sets up the offset pointers for the given deck.
 */
static void
set_up(DECK *dp)
{
	int	r1, r2;
	int	i;

	if ((dp->offsets = calloc(dp->num_cards, sizeof (int32_t))) == NULL)
		err(1, NULL);
	for (i = 0 ; i < dp->num_cards ; i++) {
		if (fread(&dp->offsets[i], sizeof(dp->offsets[i]), 1, deckf) != 1)
			err(1, "%s", cardfile);
		dp->offsets[i] = ntohl(dp->offsets[i]);
	}
	dp->top_card = 0;
	dp->gojf_used = FALSE;
	for (i = 0; i < dp->num_cards; i++) {
		long	temp;

		r1 = roll(1, dp->num_cards) - 1;
		r2 = roll(1, dp->num_cards) - 1;
		temp = dp->offsets[r2];
		dp->offsets[r2] = dp->offsets[r1];
		dp->offsets[r1] = temp;
	}
}
/*
 *	This routine draws a card from the given deck
 */
void
get_card(DECK *dp)
{
	char	type_maj, type_min;
	int16_t	num;
	int	i, per_h, per_H, num_h, num_H;
	OWN	*op;

	do {
		fseek(deckf, dp->offsets[dp->top_card], SEEK_SET);
		dp->top_card = ++(dp->top_card) % dp->num_cards;
		type_maj = getc(deckf);
	} while (dp->gojf_used && type_maj == GOJF);
	type_min = getc(deckf);
	fread(&num, sizeof(num), 1, deckf);
	num = ntohs(num);
	printmes();
	switch (type_maj) {
	  case '+':		/* get money		*/
		if (type_min == 'A') {
			for (i = 0; i < num_play; i++)
				if (i != player)
					play[i].money -= num;
			num = num * (num_play - 1);
		}
		cur_p->money += num;
		break;
	  case '-':		/* lose money		*/
		if (type_min == 'A') {
			for (i = 0; i < num_play; i++)
				if (i != player)
					play[i].money += num;
			num = num * (num_play - 1);
		}
		cur_p->money -= num;
		break;
	  case 'M':		/* move somewhere	*/
		switch (type_min) {
		  case 'F':		/* move forward	*/
			num -= cur_p->loc;
			if (num < 0)
				num += 40;
			break;
		  case 'J':		/* move to jail	*/
			goto_jail();
			return;
		  case 'R':		/* move to railroad	*/
			spec = TRUE;
			num = (int)((cur_p->loc + 5)/10)*10 + 5 - cur_p->loc;
			break;
		  case 'U':		/* move to utility	*/
			spec = TRUE;
			if (cur_p->loc >= 12 && cur_p->loc < 28)
				num = 28 - cur_p->loc;
			else {
				num = 12 - cur_p->loc;
				if (num < 0)
					num += 40;
			}
			break;
		  case 'B':
			num = -num;
			break;
		}
		move(num);
		break;
	  case 'T':			/* tax			*/
		if (dp == &CC_D) {
			per_h = 40;
			per_H = 115;
		}
		else {
			per_h = 25;
			per_H = 100;
		}
		num_h = num_H = 0;
		for (op = cur_p->own_list; op; op = op->next)
			if (op->sqr->type == PRPTY) {
				if (op->sqr->desc->houses == 5)
					++num_H;
				else
					num_h += op->sqr->desc->houses;
			}
		num = per_h * num_h + per_H * num_H;
		printf("You had %d Houses and %d Hotels, so that cost you $%d\n", num_h, num_H, num);
		if (num == 0)
			lucky("");
		else
			cur_p->money -= num;
		break;
	  case GOJF:		/* get-out-of-jail-free card	*/
		cur_p->num_gojf++;
		dp->gojf_used = TRUE;
		break;
	}
	spec = FALSE;
}

/*
 *	This routine prints out the message on the card
 */
static void
printmes(void)
{
	char	c;

	printline();
	fflush(stdout);
	while ((c = getc(deckf)) != '\0')
		putchar(c);
	printline();
	fflush(stdout);
}

/*
 *	This routine returns the players get-out-of-jail-free card
 * to the bottom of a deck.  XXX currently does not return to the correct
 * deck.
 */
void
ret_card(PLAY *plr)
{
	char	type_maj;
	int16_t	gojfpos, last_card;
	int	i;
	DECK *dp;
	int32_t temp;

	plr->num_gojf--;
	if (CC_D.gojf_used)
		dp = &CC_D;
	else
		dp = &CH_D;
	dp->gojf_used = FALSE;

	/* Put at bottom of deck (top_card - 1) and remove it from wherever else
	 * it used to be.
	 */
	last_card = dp->top_card - 1;
	if (last_card < 0)
		last_card += dp->num_cards;
	gojfpos = dp->top_card;
	do {
		gojfpos = (gojfpos + 1) % dp->num_cards;
		fseek(deckf, dp->offsets[gojfpos], SEEK_SET);
		type_maj = getc(deckf);
	} while (type_maj != GOJF);
	temp = dp->offsets[gojfpos];
	/* Only one of the next two loops does anything */
	for (i = gojfpos - 1; i > last_card; i--)
		dp->offsets[i + 1] = dp->offsets[i];
	for (i = gojfpos; i < last_card; i++)
		dp->offsets[i] = dp->offsets[i + 1];
	if (gojfpos > last_card) {
		dp->offsets[dp->top_card] = temp;
		dp->top_card++;
		dp->top_card %= dp->num_cards;
	} else
		dp->offsets[last_card] = temp;
}
