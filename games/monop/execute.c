/*	$OpenBSD: execute.c,v 1.16 2022/08/08 17:57:05 op Exp $	*/
/*	$NetBSD: execute.c,v 1.3 1995/03/23 08:34:38 cgd Exp $	*/

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

#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "monop.ext"

typedef	struct stat	STAT;
typedef	struct tm	TIME;

static char	buf[257];

static bool	new_play;	/* set if move on to new player		*/

static void	show_move(void);

/*
 *	This routine takes user input and puts it in buf
 */
void
getbuf(void)
{
	char	*sp;
	int	tmpin, i;

	i = 1;
	sp = buf;
	while (((tmpin = getchar()) != '\n') && (i < (int)sizeof(buf)) &&
	    (tmpin != EOF)) {
		*sp++ = tmpin;
		i++;
	}
	if (tmpin == EOF) {
		printf("user closed input stream, quitting...\n");
                exit(0);
	}
	*sp = '\0';
}
/*
 *	This routine executes the given command by index number
 */
void
execute(int com_num)
{
	new_play = FALSE;	/* new_play is true if fixing	*/
	(*func[com_num])();
	notify();
	force_morg();
	if (new_play)
		next_play();
	else if (num_doub)
		printf("%s rolled doubles.  Goes again\n", cur_p->name);
}
/*
 *	This routine moves a piece around.
 */
void
do_move(void)
{
	int	r1, r2;
	bool	was_jail;

	new_play = was_jail = FALSE;
	printf("roll is %d, %d\n", r1 = roll(1, 6), r2 = roll(1, 6));
	if (cur_p->loc == JAIL) {
		was_jail++;
		if (!move_jail(r1, r2)) {
			new_play++;
			goto ret;
		}
	}
	else {
		if (r1 == r2 && ++num_doub == 3) {
			printf("That's 3 doubles.  You go to jail\n");
			goto_jail();
			new_play++;
			goto ret;
		}
		move(r1 + r2);
	}
	if (r1 != r2 || was_jail)
		new_play++;
ret:
	return;
}
/*
 *	This routine moves a normal move
 */
void
move(int rl)
{
	int	old_loc;

	old_loc = cur_p->loc;
	cur_p->loc = (cur_p->loc + rl) % N_SQRS;
	if (cur_p->loc < old_loc && rl > 0) {
		cur_p->money += 200;
		printf("You pass %s and get $200\n", board[0].name);
	}
	show_move();
}
/*
 *	This routine shows the results of a move
 */
static void
show_move(void)
{
	SQUARE	*sqp;

	sqp = &board[(int)cur_p->loc];
	printf("That puts you on %s\n", sqp->name);
	switch (sqp->type) {
	case SAFE:
		printf("That is a safe place\n");
		break;
	case CC:
		cc();
		break;
	case CHANCE:
		chance();
		break;
	case INC_TAX:
		inc_tax();
		break;
	case GOTO_J:
		goto_jail();
		break;
	case LUX_TAX:
		lux_tax();
		break;
	case PRPTY:
	case RR:
	case UTIL:
		if (sqp->owner < 0) {
			printf("That would cost $%d\n", sqp->cost);
			if (getyn("Do you want to buy? ") == 0) {
				buy(player, sqp);
				cur_p->money -= sqp->cost;
			}
			else if (num_play > 2)
				bid();
		}
		else if (sqp->owner == player)
			printf("You own it.\n");
		else
			rent(sqp);
	}
}


#define MONOP_TAG "monop(6) save file"
/*
 *	This routine saves the current game for use at a later date
 */
void
save(void)
{
	int i, j;
	time_t t;
	struct stat sb;
	char *sp;
	FILE *outf;

	printf("Which file do you wish to save it in? ");
	getbuf();

	/*
	 * check for existing files, and confirm overwrite if needed
	 */
	if (stat(buf, &sb) == 0
	    && getyn("File exists.  Do you wish to overwrite? ") > 0)
		return;

	umask(022);
	if ((outf = fopen(buf, "w")) == NULL) {
		warn("%s", buf);
		return;
	}
	printf("\"%s\" ", buf);
	time(&t);			/* get current time		*/
	fprintf(outf, "%s\n", MONOP_TAG);
	fprintf(outf, "# %s", ctime(&t));	/* ctime() has \n */
	fprintf(outf, "%d %d %d\n", num_play, player, num_doub);
	for (i = 0; i < num_play; i++)
		fprintf(outf, "%s\n", name_list[i]);
	for (i = 0; i < num_play; i++)
		fprintf(outf, "%d %d %d %d\n", play[i].money, play[i].loc,
		    play[i].num_gojf, play[i].in_jail);
	/* Deck status */
	for (i = 0; i < 2; i++) {
		fprintf(outf, "%d %d %d\n", (int)(deck[i].num_cards),
		    (int)(deck[i].top_card), (int)(deck[i].gojf_used));
		for (j = 0; j < deck[i].num_cards; j++)
			fprintf(outf, "%ld ", (long)(deck[i].offsets[j]));
		fprintf(outf, "\n");
	}
	/* Ownership */
	for (i = 0; i < N_SQRS; i++) {
		if (board[i].owner >= 0) {
			if (board[i].type == PRPTY)
				fprintf(outf, "%d %d %d %d\n", i, board[i].owner,
				    board[i].desc->morg, board[i].desc->houses);
			else if (board[i].type == RR || board[i].type == UTIL)
				fprintf(outf, "%d %d %d 0\n", i, board[i].owner,
				    board[i].desc->morg);
		}
	}
	fclose(outf);

	strlcpy(buf, ctime(&t), sizeof buf);
	for (sp = buf; *sp != '\n'; sp++)
		continue;
	*sp = '\0';
	printf("[%s]\n", buf);
}
/* 
 * If we are restoring during a game, try not to leak memory.
 */
void
game_restore(void)
{
	int i;

	free(play);
	for (i = 0; i < num_play; i++)
		free(name_list[i]);
	restore();
}
/*
 *	This routine restores an old game from a file
 */
void
restore(void)
{
	printf("Which file do you wish to restore from? ");
	getbuf();
	if (rest_f(buf) == FALSE) {
		printf("Restore failed\n");
		exit(1);
	}
}
/*
 *	This does the actual restoring.  It returns TRUE if the
 *	backup was successful, else FALSE.
 */
int
rest_f(char *file)
{
	char *sp;
	int  i, j, num;
	FILE *inf;
	char *st, *a, *b;
	size_t linesize;
	ssize_t len;
	STAT sbuf;
	int  t1;
	short t2, t3, t4;
	long tl;

	printf("\"%s\" ", file);
	if (stat(file, &sbuf) == -1) {		/* get file stats	*/
		warn("%s", file);
		return(FALSE);
	}
	if ((inf = fopen(file, "r")) == NULL) {
		warn("%s", file);
		return(FALSE);
	}

	num = 1;
	st = NULL;
	linesize = 0;
	len = getline(&st, &linesize, inf);
	if (len == -1 || len != strlen(MONOP_TAG) + 1 ||
	    strncmp(st, MONOP_TAG, strlen(MONOP_TAG))) {
badness:
		warnx("%s line %d", file, num);
		free(st);
		fclose(inf);
		return(FALSE);
	}
	num++;
	if (getline(&st, &linesize, inf) == -1)
		goto badness;
	num++;
	if ((len = getline(&st, &linesize, inf)) == -1 || st[len - 1] != '\n')
		goto badness;
	st[len - 1] = '\0';
	if (sscanf(st, "%d %d %d", &num_play, &player, &num_doub) != 3 ||
	    num_play > MAX_PL || num_play < 1 ||
	    player < 0 || player >= num_play ||
	    num_doub < 0 || num_doub > 2)
		goto badness;
	if ((play = calloc(num_play, sizeof(PLAY))) == NULL)
		err(1, NULL);
	cur_p = play + player;
	/* Names */
	for (i = 0; i < num_play; i++) {
		num++;
		if ((len = getline(&st, &linesize, inf)) == -1 ||
		    st[len - 1] != '\n')
			goto badness;
		st[len - 1] = '\0';
		if ((name_list[i] = play[i].name = strdup(st)) == NULL)
			err(1, NULL);
	}
	if ((name_list[i++] = strdup("done")) == NULL)
		err(1, NULL);
	name_list[i] = NULL;
	/* Money, location, GOJF cards, turns in jail */
	for (i = 0; i < num_play; i++) {
		num++;
		if ((len = getline(&st, &linesize, inf)) == -1 ||
		    st[len - 1] != '\n')
			goto badness;
		st[len - 1] = '\0';
		if (sscanf(st, "%d %hd %hd %hd", &(play[i].money), &t2,
		    &t3, &t4) != 4 ||
		    t2 < 0 || t2 > N_SQRS || t3 < 0 || t3 > 2 ||
		    (t2 != JAIL && t4 != 0) || t4 < 0 || t4 > 3)
			goto badness;
		play[i].loc = t2;
		play[i].num_gojf = t3;
		play[i].in_jail  = t4;
	}
	/* Deck status; init_decks() must have been called. */
	for (i = 0; i < 2; i++) {
		num++;
		if ((len = getline(&st, &linesize, inf)) == -1 ||
		    st[len - 1] != '\n')
			goto badness;
		st[len - 1] = '\0';
		if (sscanf(st, "%d %d %hd", &t1, &j, &t2) != 3 ||
		    j > t1 || t1 != deck[i].num_cards || j < 0 ||
		    (t2 != FALSE && t2 != TRUE))
			goto badness;
		deck[i].top_card = j;
		deck[i].gojf_used = t2;
		num++;
		if ((len = getline(&st, &linesize, inf)) == -1 ||
		    st[len - 1] != '\n')
			goto badness;
		st[len - 1] = '\0';
		a = st;
		for (j = 0; j < deck[i].num_cards; j++) {
			if ((tl = strtol(a, &b, 10)) < 0 || tl >= 0x7FFFFFFF ||
			    b == a)
			    goto badness;
			deck[i].offsets[j] = tl;
			b = a;
		}
		/* Ignore anything trailing */
	}
	trading = FALSE;
	while ((len = getline(&st, &linesize, inf)) != -1) {
		num++;
		if (st[len - 1] != '\n')
			goto badness;
		st[len - 1] = '\0';
		/* Location, owner, mortgaged, nhouses */
		if (sscanf(st, "%d %hd %hd %hd", &t1, &t2, &t3, &t4) != 4 ||
		    t1 < 0 || t1 >= N_SQRS || (board[t1].type != PRPTY &&
		    board[t1].type != RR && board[t1].type != UTIL) ||
		    t2 < 0 || t2 >= num_play ||
		    (t3 != TRUE && t3 != FALSE) ||
		    t4 < 0 || t4 > 5 || (t4 > 0 && t3 == TRUE))
			goto badness;
		add_list(t2, &(play[t2].own_list), t1);
		/* No properties on mortgaged lots */
		if (t3 && t4)
			goto badness;
		board[t1].owner = t2;
		(board[t1].desc)->morg = t3;
		(board[t1].desc)->houses = t4;
		/* XXX Should check that number of houses per property are all
		 * within 1 in each monopoly
		 */
	}
	free(st);
	fclose(inf);
	/* Check total hotel and house count */
	t1 = j = 0;
	for (i = 0; i < N_SQRS; i++) {
		if (board[i].type == PRPTY) {
			if ((board[i].desc)->houses == 5)
				j++;
			else
				t1 += (board[i].desc)->houses;
		}
	}
	if (t1 > N_HOUSE || j > N_HOTEL) {
		warnx("too many buildings");
		return(FALSE);
	}
	/* Check GOJF cards */
	t1 = 0;
	for (i = 0; i < num_play; i++)
		t1 += play[i].num_gojf;
	for (i = 0; i < 2; i++)
		t1 -= (deck[i].gojf_used == TRUE);
	if (t1 != 0) {
		warnx("can't figure out the Get-out-of-jail-free cards");
		return(FALSE);
	}

	strlcpy(buf, ctime(&sbuf.st_mtime), sizeof buf);
	for (sp = buf; *sp != '\n'; sp++)
		continue;
	*sp = '\0';
	printf("[%s]\n", buf);
	return(TRUE);
}
