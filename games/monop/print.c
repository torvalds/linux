/*	$OpenBSD: print.c,v 1.8 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: print.c,v 1.3 1995/03/23 08:35:05 cgd Exp $	*/

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

#include "monop.ext"

static const char	*header	= "Name      Own      Price Mg # Rent";

static void	printmorg(SQUARE *);

/*
 *	This routine prints out the current board
 */
void
printboard(void)
{
	int	i;

	printf("%s\t%s\n", header, header);
	for (i = 0; i < N_SQRS/2; i++) {
		printsq(i, FALSE);
		putchar('\t');
		printsq(i+N_SQRS/2, TRUE);
	}
}
/*
 *	This routine lists where each player is.
 */
void
where(void)
{
	int	i;

	printf("%s Player\n", header);
	for (i = 0; i < num_play; i++) {
		printsq(play[i].loc, FALSE);
		printf(" %s (%d)", play[i].name, i+1);
		if (cur_p == &play[i])
			printf(" *");
		putchar('\n');
	}
}
/*
 *	This routine prints out an individual square
 */
void
printsq(int sqn, bool eoln)
{
	int	rnt;
	PROP	*pp;
	SQUARE	*sqp;

	sqp = &board[sqn];
	printf("%-10.10s", sqp->name);
	switch (sqp->type) {
	case SAFE:
	case CC:
	case CHANCE:
	case INC_TAX:
	case GOTO_J:
	case LUX_TAX:
	case IN_JAIL:
		if (!eoln)
			printf("                        ");
		break;
	case PRPTY:
		pp = sqp->desc;
		if (sqp->owner < 0) {
			printf(" - %-8.8s %3d", pp->mon_desc->name, sqp->cost);
			if (!eoln)
				printf("         ");
			break;
		}
		printf(" %d %-8.8s %3d", sqp->owner+1, pp->mon_desc->name,
			sqp->cost);
		printmorg(sqp);
		if (pp->monop) {
			if (pp->houses < 5) {
				if (pp->houses > 0)
					printf("%d %4d", pp->houses,
						pp->rent[(int)pp->houses]);
				else
					printf("0 %4d", pp->rent[0] * 2);
			} else
				printf("H %4d", pp->rent[5]);
		} else
			printf("  %4d", pp->rent[0]);
		break;
	case UTIL:
		if (sqp->owner < 0) {
			printf(" -          150");
			if (!eoln)
				printf("         ");
			break;
		}
		printf(" %d          150", sqp->owner+1);
		printmorg(sqp);
		printf("%d", play[(int)sqp->owner].num_util);
		if (!eoln)
			printf("    ");
		break;
	case RR:
		if (sqp->owner < 0) {
			printf(" - Railroad 200");
			if (!eoln)
				printf("         ");
			break;
		}
		printf(" %d Railroad 200", sqp->owner+1);
		printmorg(sqp);
		rnt = 25;
		rnt <<= play[(int)sqp->owner].num_rr - 1;
		printf("%d %4d", play[(int)sqp->owner].num_rr,
		    25 << (play[(int)sqp->owner].num_rr - 1));
		break;
	default:
		printf("Warning: printsq() switch %d\n", sqp->type);
		break;
	}
	if (eoln)
		putchar('\n');
}
/*
 *	This routine prints out the mortgage flag.
 */
static void
printmorg(SQUARE *sqp)
{
	if (sqp->desc->morg)
		printf(" * ");
	else
		printf("   ");
}
/*
 *	This routine lists the holdings of the player given
 */
void
printhold(int pl)
{
	OWN	*op;
	PLAY	*pp;

	pp = &play[pl];
	printf("%s's (%d) holdings (Total worth: $%d):\n", name_list[pl], pl+1,
		pp->money + prop_worth(pp));
	printf("\t$%d", pp->money);
	if (pp->num_gojf) {
		printf(", %d get-out-of-jail-free card", pp->num_gojf);
		if (pp->num_gojf > 1)
			putchar('s');
	}
	putchar('\n');
	if (pp->own_list) {
		printf("\t%s\n", header);
		for (op = pp->own_list; op; op = op->next) {
			putchar('\t');
			printsq(sqnum(op->sqr), TRUE);
		}
	}
}
