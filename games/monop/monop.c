/*	$OpenBSD: monop.c,v 1.16 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: monop.c,v 1.3 1995/03/23 08:34:52 cgd Exp $	*/

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
#include <string.h>
#include <unistd.h>

#include "monop.def"

static void	getplayers(void);
static void	init_players(void);
static void	init_monops(void);

/*
 *	This program implements a monopoly game
 */
int
main(int ac, char *av[])
{
	num_luck = sizeof lucky_mes / sizeof (char *);

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	init_decks();
	init_monops();
	if (ac > 1) {
		if (!rest_f(av[1]))
			restore();
	}
	else {
		getplayers();
		init_players();
	}
	for (;;) {
		printf("\n%s (%d) (cash $%d) on %s\n", cur_p->name, player + 1,
			cur_p->money, board[(int)cur_p->loc].name);
		printturn();
		force_morg();
		execute(getinp("-- Command: ", comlist));
	}
}

/*
 *	This routine gets the names of the players
 */
static void
getplayers(void)
{
	int	i, j;
	char	buf[257];

blew_it:
	for (;;) {
		if ((num_play = get_int("How many players? ")) <= 1 ||
		    num_play > MAX_PL)
			printf("Sorry. Number must range from 2 to %d\n",
			    MAX_PL);
		else
			break;
	}
	if ((cur_p = play = calloc(num_play, sizeof (PLAY))) == NULL)
		err(1, NULL);
	for (i = 0; i < num_play; i++) {
		do {
			printf("Player %d's name: ", i + 1);
			fgets(buf, sizeof(buf), stdin);
			if ((feof(stdin))) {
				printf("user closed input stream, quitting...\n");
				exit(0);
			}
			buf[strcspn(buf, "\n")] = '\0';
		} while (strlen(buf) == 0);
		if ((name_list[i] = play[i].name = strdup(buf)) == NULL)
			err(1, NULL);
		play[i].money = 1500;
	}
	name_list[i++] = "done";
	name_list[i] = 0;
	for (i = 0; i < num_play; i++)
		for (j = i + 1; j <= num_play; j++)
			if (strcasecmp(name_list[i], name_list[j]) == 0) {
				if (j != num_play)
					printf("Hey!!! Some of those are IDENTICAL!!  Let's try that again...\n");
				else
					printf("\"done\" is a reserved word.  Please try again\n");
				for (i = 0; i < num_play; i++)
					free(play[i].name);
				free(play);
				goto blew_it;
			}
}
/*
 *	This routine figures out who goes first
 */
static void
init_players(void)
{
	int	i, rl, cur_max;
	bool	over = 0;
	int	max_pl = 0;

again:
	putchar('\n');
	for (cur_max = i = 0; i < num_play; i++) {
		printf("%s (%d) rolls %d\n", play[i].name, i+1, rl=roll(2, 6));
		if (rl > cur_max) {
			over = FALSE;
			cur_max = rl;
			max_pl = i;
		}
		else if (rl == cur_max)
			over++;
	}
	if (over) {
		printf("%d people rolled the same thing, so we'll try again\n",
		    over + 1);
		goto again;
	}
	player = max_pl;
	cur_p = &play[max_pl];
	printf("%s (%d) goes first\n", cur_p->name, max_pl + 1);
}
/*
 *	This routine initializes the monopoly structures.
 */
static void
init_monops(void)
{
	MON	*mp;
	int	i;

	for (mp = mon; mp < &mon[N_MON]; mp++) {
		mp->name = mp->not_m;
		for (i = 0; i < mp->num_in; i++)
			mp->sq[i] = &board[(int)mp->sqnums[i]];
	}
}
