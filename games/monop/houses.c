/*	$OpenBSD: houses.c,v 1.10 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: houses.c,v 1.3 1995/03/23 08:34:40 cgd Exp $	*/

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

#include <stdio.h>
#include <stdlib.h>

#include "monop.ext"

static char	*names[N_MON+2],
		cur_prop[80];

static MON	*monops[N_MON];

static void	buy_h(MON *);
static void	sell_h(MON *);
static void	list_cur(MON *);
static int	avail_houses();
static int	avail_hotels();
static bool	can_only_buy_hotel(MON *);

/*
 *	These routines deal with buying and selling houses
 */
void
buy_houses(void)
{
	int	num_mon;
	MON	*mp;
	OWN	*op;
	bool	good, got_morg;
	int	i,p;

over:
	num_mon = 0;
	good = TRUE;
	got_morg = FALSE;
	for (op = cur_p->own_list; op && op->sqr->type != PRPTY; op = op->next)
		continue;
	while (op)
		if (op->sqr->desc->monop) {
			mp = op->sqr->desc->mon_desc;
			names[num_mon] = (monops[num_mon]=mp)->name;
			num_mon++;
			got_morg = good = FALSE;
			for (i = 0; i < mp->num_in; i++) {
				if (op->sqr->desc->morg)
					got_morg = TRUE;
				if (op->sqr->desc->houses != 5)
					good = TRUE;
				op = op->next;
			}
			if (!good || got_morg)
				--num_mon;
		}
		else
			op = op->next;
	if (num_mon == 0) {
		if (got_morg)
			printf("You can't build on mortgaged monopolies.\n");
		else if (!good)
			printf("You can't build any more.\n");
		else
			printf("But you don't have any monopolies!!\n");
		return;
	}
	if (num_mon == 1)
		buy_h(monops[0]);
	else {
		names[num_mon++] = "done";
		names[num_mon--] = 0;
		if ((p=getinp("Which property do you wish to buy houses for? ", names)) == num_mon)
			return;
		buy_h(monops[p]);
		goto over;
	}
}

static void
buy_h(MON *mnp)
{
	int	i;
	MON	*mp;
	int	price;
	shrt	input[3],temp[3];
	int	tot, tot2;
	PROP	*pp;
	int	nhous, nhot;
	bool chot;

	mp = mnp;
	price = mp->h_cost * 50;
	nhous = avail_houses();
	nhot = avail_hotels();
	chot = can_only_buy_hotel(mnp);
	if (nhous == 0 && !chot) {
		printf("Building shortage:  no houses available.");
		return;
	}
	if (nhot == 0 && chot) {
		printf("Building shortage:  no hotels available.");
		return;
	}
blew_it:
	list_cur(mp);
	printf("Houses will cost $%d\n", price);
	printf("How many houses do you wish to buy for\n");
	for (i = 0; i < mp->num_in; i++) {
		pp = mp->sq[i]->desc;
over:
		if (pp->houses == 5) {
			printf("%s (H):\n", mp->sq[i]->name);
			input[i] = 0;
			temp[i] = 5;
			continue;
		}
		(void)snprintf(cur_prop, sizeof(cur_prop), "%s (%d): ",
			mp->sq[i]->name, pp->houses);
		input[i] = get_int(cur_prop);
		temp[i] = input[i] + pp->houses;
		if (temp[i] > 5 || temp[i] < 0) {
			printf("That's too many.  The most you can buy is %d\n",
			    5 - pp->houses);
				goto over;
			}
	}
	if (mp->num_in == 3 && (abs(temp[0] - temp[1]) > 1 ||
	    abs(temp[0] - temp[2]) > 1 || abs(temp[1] - temp[2]) > 1)) {
err:		printf("That makes the spread too wide.  Try again\n");
		goto blew_it;
	}
	else if (mp->num_in == 2 && abs(temp[0] - temp[1]) > 1)
		goto err;
	for (tot = tot2 = i = 0; i < mp->num_in; i++) {
		if (temp[i] == 5 && input[i])
			tot2++;
		else
			tot += input[i];
	}
	if (tot > nhous) {
		printf(
"You have asked for %d house%s but only %d are available.  Try again\n",
		    tot, tot == 1 ? "":"s", nhous);
		goto blew_it;
	} else if (tot2 > nhot) {
		printf(
"You have asked for %d hotel%s but only %d are available.  Try again\n",
		    tot2, tot2 == 1 ? "":"s", nhot);
		goto blew_it;
	}
	if (!chot && tot2) {
		printf(
"You must have 4 houses on all your property before building hotels\n");
		goto blew_it;
	}
	if (tot || tot2) {
		printf("You asked for %d %s%s for $%d\n", tot ? tot : tot2, 
		    tot ? "house" : "hotel", (tot == 1 || tot2 == 1) ? "" : "s",
		    (tot ? tot : tot2) * price);
		if (getyn("Is that ok? ") == 0) {
			cur_p->money -= (tot ? tot : tot2) * price;
			for (tot = i = 0; i < mp->num_in; i++)
				mp->sq[i]->desc->houses = temp[i];
		}
	}
}

/*
 *	This routine sells houses.
 */
void
sell_houses(void)
{
	int	num_mon;
	MON	*mp;
	OWN	*op;
	bool	good;
	int	p;

over:
	num_mon = 0;
	good = TRUE;
	for (op = cur_p->own_list; op; op = op->next)
		if (op->sqr->type == PRPTY && op->sqr->desc->monop) {
			mp = op->sqr->desc->mon_desc;
			names[num_mon] = (monops[num_mon]=mp)->name;
			num_mon++;
			good = 0;
			do
				if (!good && op->sqr->desc->houses != 0)
					good++;
			while (op->next && op->sqr->desc->mon_desc == mp
			    && (op=op->next));
			if (!good)
				--num_mon;
		}
	if (num_mon == 0) {
		printf("You don't have any houses to sell!!\n");
		return;
	}
	if (num_mon == 1)
		sell_h(monops[0]);
	else {
		names[num_mon++] = "done";
		names[num_mon--] = 0;
		if ((p=getinp("Which property do you wish to sell houses from? ", names)) == num_mon)
			return;
		sell_h(monops[p]);
		notify();
		goto over;
	}
}

static void
sell_h(MON *mnp)
{
	int	i;
	MON	*mp;
	int	price;
	shrt	input[3],temp[3];
	int	tot;
	PROP	*pp;

	mp = mnp;
	price = mp->h_cost * 25;
blew_it:
	printf("Houses will get you $%d apiece\n", price);
	list_cur(mp);
	printf("How many houses do you wish to sell from\n");
	for (i = 0; i < mp->num_in; i++) {
		pp = mp->sq[i]->desc;
over:
		if (pp->houses == 0) {
			printf("%s (0):\n", mp->sq[i]->name);
			input[i] = temp[i] = 0;
			continue;
		}
		if (pp->houses < 5)
			(void)snprintf(cur_prop, sizeof(cur_prop), "%s (%d): ",
				mp->sq[i]->name,pp->houses);
		else
			(void)snprintf(cur_prop, sizeof(cur_prop), "%s (H): ",
			    mp->sq[i]->name);
		input[i] = get_int(cur_prop);
		temp[i] = pp->houses - input[i];
		if (temp[i] < 0) {
			printf("That's too many.  The most you can sell is %d\n", pp->houses);
				goto over;
			}
	}
	if (mp->num_in == 3 && (abs(temp[0] - temp[1]) > 1 ||
	    abs(temp[0] - temp[2]) > 1 || abs(temp[1] - temp[2]) > 1)) {
err:		printf("That makes the spread too wide.  Try again\n");
		goto blew_it;
	}
	else if (mp->num_in == 2 && abs(temp[0] - temp[1]) > 1)
		goto err;
	for (tot = i = 0; i < mp->num_in; i++)
		tot += input[i];
	if (tot) {
		printf("You asked to sell %d house%s for $%d\n", tot,
		    tot == 1 ? "" : "s", tot * price);
		if (getyn("Is that ok? ") == 0) {
			cur_p->money += tot * price;
			for (tot = i = 0; i < mp->num_in; i++)
				mp->sq[i]->desc->houses = temp[i];
		}
	}
}

static void
list_cur(MON *mp)
{
	int	i;
	SQUARE	*sqp;

	for (i = 0; i < mp->num_in; i++) {
		sqp = mp->sq[i];
		if (sqp->desc->houses == 5)
			printf("%s (H) ", sqp->name);
		else
			printf("%s (%d) ", sqp->name, sqp->desc->houses);
	}
	putchar('\n');
}

static int
avail_houses(void)
{
	int i, c;
	SQUARE *sqp;

	c = 0;
	for (i = 0; i < N_SQRS; i++) {
		sqp = &board[i];
		if (sqp->type == PRPTY && sqp->owner >= 0 && sqp->desc->monop) {
			if (sqp->desc->houses < 5 && sqp->desc->houses > 0)
				c += sqp->desc->houses;
		}
	}
	return(N_HOUSE - c);
}

static int
avail_hotels(void)
{
	int i, c;
	SQUARE *sqp;

	c = 0;
	for (i = 0; i < N_SQRS; i++) {
		sqp = &board[i];
		if (sqp->type == PRPTY && sqp->owner >= 0 && sqp->desc->monop) {
			if (sqp->desc->houses == 5)
				c++;
		}
	}
	return(N_HOTEL - c);
}

static bool
can_only_buy_hotel(MON *mp)
{
	int i;

	for (i = 0; i < mp->num_in; i++) {
		if (mp->sq[i]->desc->houses < 4)
			return(FALSE);
	}
	return(TRUE);
}
