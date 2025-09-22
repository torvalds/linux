/*	$OpenBSD: misc.c,v 1.14 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: misc.c,v 1.4 1995/03/23 08:34:47 cgd Exp $	*/

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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "monop.ext"

/*
 *	This routine executes a truncated set of commands until a
 * "yes or "no" answer is gotten.
 */
int
getyn(char *prompt)
{
	int	com;

	for (;;)
		if ((com=getinp(prompt, ynlist)) < 2)
			return com;
		else
			(*func[com-2])();
}
/*
 *	This routine tells the player if he's out of money.
 */
void
notify(void)
{
	if (cur_p->money < 0)
		printf("That leaves you $%d in debt\n", -cur_p->money);
	else if (cur_p->money == 0)
		printf("that leaves you broke\n");
	else if (fixing && !told_em && cur_p->money > 0) {
		printf("-- You are now Solvent ---\n");
		told_em = TRUE;
	}
}
/*
 *	This routine switches to the next player
 */
void
next_play(void)
{
	player = (player + 1) % num_play;
	cur_p = &play[player];
	num_doub = 0;
}
/*
 *	This routine gets an integer from the keyboard after the
 * given prompt.
 */
int
get_int(char *prompt)
{
	int	num, snum;
	char	*sp;
	int	c, i;
	char	buf[257];

	for (;;) {
		printf("%s", prompt);
		num = 0;
		i = 1;
		for (sp = buf; (c = getchar()) != '\n';) {
			if (c == EOF) {
				printf("user closed input stream, quitting...\n");
				exit(0);
			}
			*sp = c;
			if (i < (int)sizeof(buf)) {
				i++;
				sp++;
			}
		}
		*sp = c;
		if (sp == buf)
			continue;
		for (sp = buf; isspace((unsigned char)*sp); sp++)
			;
		for (; isdigit((unsigned char)*sp); sp++) {
			snum = num;
			num = num * 10 + *sp - '0';
			if (num < snum) {
				printf("Number too large - ");
				*(sp + 1) = 'X';	/* Force a break */
			}
		}
		/* Be kind to trailing spaces */
		for (; *sp == ' '; sp++)
			;
		if (*sp == '\n')
			return num;
		else
			printf("I can't understand that\n");
	}
}
/*
 *	This routine sets the monopoly flag from the list given.
 */
void
set_ownlist(int pl)
{
	int	num;		/* general counter		*/
	MON	*orig;		/* remember starting monop ptr	*/
	OWN	*op;		/* current owned prop		*/
	OWN	*orig_op;		/* origianl prop before loop	*/

	op = play[pl].own_list;
#ifdef DEBUG
	printf("op [%p] = play[pl [%d] ].own_list;\n", op, pl);
#endif
	while (op) {
#ifdef DEBUG
		printf("op->sqr->type = %d\n", op->sqr->type);
#endif
		switch (op->sqr->type) {
		  case UTIL:
#ifdef DEBUG
			printf("  case UTIL:\n");
#endif
			for (num = 0; op && op->sqr->type == UTIL; op = op->next)
				num++;
			play[pl].num_util = num;
#ifdef DEBUG
			printf("play[pl].num_util = num [%d];\n", num);
#endif
			break;
		  case RR:
#ifdef DEBUG
			printf("  case RR:\n");
#endif
			for (num = 0; op && op->sqr->type == RR; op = op->next) {
#ifdef DEBUG
				printf("iter: %d\n", num);
				printf("op = %p, op->sqr = %p, op->sqr->type = %d\n", op, op->sqr, op->sqr->type);
#endif
				num++;
			}
			play[pl].num_rr = num;
#ifdef DEBUG
			printf("play[pl].num_rr = num [%d];\n", num);
#endif
			break;
		  case PRPTY:
#ifdef DEBUG
			printf("  case PRPTY:\n");
#endif
			orig = op->sqr->desc->mon_desc;
			orig_op = op;
			num = 0;
			while (op && op->sqr->desc->mon_desc == orig) {
#ifdef DEBUG
				printf("iter: %d\n", num);
#endif
				num++;
#ifdef DEBUG
				printf("op = op->next ");
#endif
				op = op->next;
#ifdef DEBUG
				printf("[%p];\n", op);
#endif
			}
#ifdef DEBUG
			printf("num = %d\n", num);
#endif
			if (orig == NULL) {
				printf("panic:  bad monopoly descriptor: orig = %p\n", orig);
				printf("player # %d\n", pl+1);
				printhold(pl);
				printf("orig_op = %p\n", orig_op);
				if (orig_op) {
					printf("orig_op->sqr->type = %d (PRPTY)\n",
					    orig_op->sqr->type);
					printf("orig_op->next = %p\n",
					    orig_op->next);
					printf("orig_op->sqr->desc = %p\n",
					    orig_op->sqr->desc);
				}
				printf("op = %p\n", op);
				if (op) {
					printf("op->sqr->type = %d (PRPTY)\n",
					    op->sqr->type);
					printf("op->next = %p\n", op->next);
					printf("op->sqr->desc = %p\n",
					    op->sqr->desc);
				}
				printf("num = %d\n", num);
				exit(1);
			}
#ifdef DEBUG
			printf("orig->num_in = %d\n", orig->num_in);
#endif
			if (num == orig->num_in)
				is_monop(orig, pl);
			else
				isnot_monop(orig);
			break;
		}
	}
}
/*
 *	This routine sets things up as if it is a new monopoly
 */
void
is_monop(MON *mp, int pl)
{
	int	i;

	mp->owner = pl;
	mp->num_own = mp->num_in;
	for (i = 0; i < mp->num_in; i++)
		mp->sq[i]->desc->monop = TRUE;
	mp->name = mp->mon_n;
}
/*
 *	This routine sets things up as if it is no longer a monopoly
 */
void
isnot_monop(MON *mp)
{
	int	i;

	mp->owner = -1;
	for (i = 0; i < mp->num_in; i++)
		mp->sq[i]->desc->monop = FALSE;
	mp->name = mp->not_m;
}
/*
 *	This routine gives a list of the current player's routine
 */
void
list(void)
{
	printhold(player);
}
/*
 *	This routine gives a list of a given players holdings
 */
void
list_all(void)
{
	int	pl;

	while ((pl=getinp("Whose holdings do you want to see? ", name_list)) < num_play)
		printhold(pl);
}
/*
 *	This routine gives the players a chance before it exits.
 */
void
quit(void)
{
	putchar('\n');
	if (getyn("Do you all really want to quit? ") == 0)
		exit(0);
}
