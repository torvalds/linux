/*	$OpenBSD: move.c,v 1.13 2015/11/30 08:19:25 tb Exp $	*/

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

#include "back.h"
#include "backlocal.h"

struct BOARD  {		/* structure of game position */
	int     b_board[26];	/* board position */
	int     b_in[2];	/* men in */
	int     b_off[2];	/* men off */
	int     b_st[4], b_fn[4];	/* moves */

	struct BOARD *b_next;	/* forward queue pointer */
};

struct BOARD *freeq = 0;
struct BOARD *checkq = 0;

static int cp[5];		/* candidate start position */
static int cg[5];		/* candidate finish position */

static int race;		/* game reduced to a race */
static float bestmove;

static int bcomp(struct BOARD *, struct BOARD *);
static struct BOARD *bsave(void);
static void binsert(struct BOARD *);
static void boardcopy(struct BOARD *);
static void makefree(struct BOARD *);
static void movcmp(void);
static void mvcheck(struct BOARD *, struct BOARD *);
static struct BOARD *nextfree(void);
static void pickmove(void);


void
domove(int okay)
{
	int     i;		/* index */
	int     l = 0;		/* last man */

	bestmove = -9999999.;
	if (okay) {
	    	/* see if comp should double */
		if (dflag && gvalue < 64 && dlast != cturn && dblgood()) {
			addstr(*Colorptr);
			dble();	/* double */
			/* return if declined */
			if (cturn != 1 && cturn != -1)
				return;
		}
		roll();
	}
	race = 0;
	for (i = 0; i < 26; i++) {
		if (board[i] < 0)
			l = i;
	}
	for (i = 0; i < l; i++) {
		if (board[i] > 0)
			break;
	}
	if (i == l)
		race = 1;

	/* print roll */
	move(cturn == -1 ? 18 : 19, 0);
	printw("%s rolls %d %d", *Colorptr, D0, D1);
	clrtoeol();

	/* find out how many moves */
	mvlim = movallow();
	if (mvlim == 0) {
		addstr(" but cannot use it.\n");
		nexturn();
		return;
	}

	/* initialize */
	for (i = 0; i < 4; i++)
		cp[i] = cg[i] = 0;

	/* strategize */
	trymove(0, 0);
	pickmove();

	/* print move */
	addstr(" and moves ");
	for (i = 0; i < mvlim; i++) {
		if (i > 0)
			addch(',');
		printw("%d-%d", p[i] = cp[i], g[i] = cg[i]);
		makmove(i);
	}
	addch('.');

	/* print blots hit */
	move(20, 0);
	for (i = 0; i < mvlim; i++)
		if (h[i])
			wrhit(g[i]);
	/* get ready for next move */
	nexturn();
	if (!okay) {
		refresh();
		sleep(3);
	}
}

void
trymove(int mvnum, int swapped)
{
	int     pos;		/* position on board */
	int     rval;		/* value of roll */

	/* if recursed through all dice values, compare move */
	if (mvnum == mvlim) {
		binsert(bsave());
		return;
	}
	/* make sure dice in always same order */
	if (d0 == swapped)
		swap;
	/* choose value for this move */
	rval = dice[mvnum != 0];

	/* find all legitimate moves */
	for (pos = bar; pos != home; pos += cturn) {
		/* fix order of dice */
		if (d0 == swapped)
			swap;
		/* break if stuck on bar */
		if (board[bar] != 0 && pos != bar)
			break;
		/* on to next if not occupied */
		if (board[pos] * cturn <= 0)
			continue;
		/* set up arrays for move */
		p[mvnum] = pos;
		g[mvnum] = pos + rval * cturn;
		if (g[mvnum] * cturn >= home) {
			if (*offptr < 0)
				break;
			g[mvnum] = home;
		}
		/* try to move */
		if (makmove(mvnum))
			continue;
		else
			trymove(mvnum + 1, 2);
		/* undo move to try another */
		backone(mvnum);
	}

	/* swap dice and try again */
	if ((!swapped) && D0 != D1)
		trymove(0, 1);
}

static struct BOARD *
bsave(void)
{
	int     i;		/* index */
	struct BOARD *now;	/* current position */

	now = nextfree();	/* get free BOARD */

	/* store position */
	for (i = 0; i < 26; i++)
		now->b_board[i] = board[i];
	now->b_in[0] = in[0];
	now->b_in[1] = in[1];
	now->b_off[0] = off[0];
	now->b_off[1] = off[1];
	for (i = 0; i < mvlim; i++) {
		now->b_st[i] = p[i];
		now->b_fn[i] = g[i];
	}
	return(now);
}

static void
binsert(struct BOARD *new)
{
	struct BOARD *p = checkq;	/* queue pointer */
	int     result;		/* comparison result */

	if (p == 0) {		/* check if queue empty */
		checkq = p = new;
		p->b_next = 0;
		return;
	}
	result = bcomp(new, p);	/* compare to first element */
	if (result < 0) {	/* insert in front */
		new->b_next = p;
		checkq = new;
		return;
	}
	if (result == 0) {	/* duplicate entry */
		mvcheck(p, new);
		makefree(new);
		return;
	}
	while (p->b_next != 0) {/* traverse queue */
		result = bcomp(new, p->b_next);
		if (result < 0) {	/* found place */
			new->b_next = p->b_next;
			p->b_next = new;
			return;
		}
		if (result == 0) {	/* duplicate entry */
			mvcheck(p->b_next, new);
			makefree(new);
			return;
		}
		p = p->b_next;
	}
	/* place at end of queue */
	p->b_next = new;
	new->b_next = 0;
}

static int
bcomp(struct BOARD *a, struct BOARD *b)
{
	int    *aloc = a->b_board;	/* pointer to board a */
	int    *bloc = b->b_board;	/* pointer to board b */
	int     i;		/* index */
	int     result;		/* comparison result */

	for (i = 0; i < 26; i++) {	/* compare boards */
		result = cturn * (aloc[i] - bloc[i]);
		if (result)
			return(result);		/* found inequality */
	}
	return(0);			/* same position */
}

static void
mvcheck(struct BOARD *incumbent, struct BOARD *candidate)
{
	int     i, result;

	for (i = 0; i < mvlim; i++) {
		result = cturn * (candidate->b_st[i] - incumbent->b_st[i]);
		if (result > 0)
			return;
		if (result < 0)
			break;
	}
	if (i == mvlim)
		return;
	for (i = 0; i < mvlim; i++) {
		incumbent->b_st[i] = candidate->b_st[i];
		incumbent->b_fn[i] = candidate->b_fn[i];
	}
}

static void
makefree(struct BOARD *dead)
{
	dead->b_next = freeq;	/* add to freeq */
	freeq = dead;
}

static struct BOARD *
nextfree(void)
{
	struct BOARD *new;

	if (freeq == 0) {
		new = calloc (1, sizeof(struct BOARD));
		if (new == 0) {
			addstr("\nOut of memory\n");
			getout(0);
		}
	} else {
		new = freeq;
		freeq = freeq->b_next;
	}

	new->b_next = 0;
	return(new);
}

static void
pickmove(void)
{
	/* current game position */
	struct BOARD *now = bsave();
	struct BOARD *next;	/* next move */

#ifdef DEBUG
	if (ftrace == NULL)
		ftrace = fopen("bgtrace", "w");
	fprintf(ftrace, "\nRoll:  %d %d%s\n", D0, D1, race ? " (race)" : "");
	fflush(ftrace);
#endif
	do {			/* compare moves */
		boardcopy(checkq);
		next = checkq->b_next;
		makefree(checkq);
		checkq = next;
		movcmp();
	} while (checkq != 0);

	boardcopy(now);
}

static void
boardcopy(struct BOARD *s)
{
	int     i;		/* index */

	for (i = 0; i < 26; i++)
		board[i] = s->b_board[i];
	for (i = 0; i < 2; i++) {
		in[i] = s->b_in[i];
		off[i] = s->b_off[i];
	}
	for (i = 0; i < mvlim; i++) {
		p[i] = s->b_st[i];
		g[i] = s->b_fn[i];
	}
}

static void
movcmp(void)
{
	int i;
	float f;

	setx();
	f = pubeval(race);
	if (f > bestmove) {
		bestmove = f;
		for (i = 0; i < mvlim; i++) {
			cp[i] = p[i];
			cg[i] = g[i];
		}
	}
}
