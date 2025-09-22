/*	$OpenBSD: initdeck.c,v 1.17 2016/01/08 18:20:33 mestre Exp $	*/
/*	$NetBSD: initdeck.c,v 1.3 1995/03/23 08:34:43 cgd Exp $	*/

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
#include <unistd.h>

#include "deck.h"

/*
 *	This program initializes the card files for monopoly.
 * It reads in a data file with Com. Chest cards, followed by
 * the Chance card.  The two are separated by a line of "%-".
 * All other cards are separated by lines of "%%".  In the front
 * of the file is the data for the decks in the same order.
 * This includes the seek pointer for the start of each card.
 * All cards start with their execution code, followed by the
 * string to print, terminated with a null byte.
 */

#define	TRUE	1
#define	FALSE	0

#define	bool	int8_t

char	*infile		= "cards.inp",		/* input file		*/
	*outfile	= "cards.pck";		/* "packed" file	*/

DECK	deck[2];

FILE	*inf, *outf;

static void	getargs(int, char *[]);
static void	count(void);
static void	putem(void);

int
main(int ac, char *av[])
{
	int n;

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	getargs(ac, av);
	if ((inf = fopen(infile, "r")) == NULL)
		err(1, "%s", infile);
	count();
	/*
	 * allocate space for pointers.
	 */
	if ((CC_D.offsets = calloc(CC_D.num_cards + 1,
			sizeof (int32_t))) == NULL ||
	    (CH_D.offsets = calloc(CH_D.num_cards + 1,
			sizeof (int32_t))) == NULL)
		err(1, NULL);
	fseek(inf, 0L, SEEK_SET);
	if ((outf = fopen(outfile, "w")) == NULL)
		err(1, "%s", outfile);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	fwrite(&deck[0].num_cards, sizeof(deck[0].num_cards), 1, outf);
	fwrite(&deck[0].top_card, sizeof(deck[0].top_card), 1, outf);
	fwrite(&deck[0].gojf_used, sizeof(deck[0].gojf_used), 1, outf);

	fwrite(&deck[0].num_cards, sizeof(deck[0].num_cards), 1, outf);
	fwrite(&deck[0].top_card, sizeof(deck[0].top_card), 1, outf);
	fwrite(&deck[0].gojf_used, sizeof(deck[0].gojf_used), 1, outf);

	fwrite(CC_D.offsets, sizeof(CC_D.offsets[0]), CC_D.num_cards, outf);
	fwrite(CH_D.offsets, sizeof(CH_D.offsets[0]), CH_D.num_cards, outf);
	putem();

	fclose(inf);
	fseek(outf, 0L, SEEK_SET);

	deck[0].num_cards = htons(deck[0].num_cards);
	fwrite(&deck[0].num_cards, sizeof(deck[0].num_cards), 1, outf);
	deck[0].top_card = htons(deck[0].top_card);
	fwrite(&deck[0].top_card, sizeof(deck[0].top_card), 1, outf);
	fwrite(&deck[0].gojf_used, sizeof(deck[0].gojf_used), 1, outf);
	deck[0].num_cards = ntohs(deck[0].num_cards);

	deck[1].num_cards = htons(deck[1].num_cards);
	fwrite(&deck[1].num_cards, sizeof(deck[1].num_cards), 1, outf);
	deck[1].top_card = htons(deck[1].top_card);
	fwrite(&deck[1].top_card, sizeof(deck[1].top_card), 1, outf);
	fwrite(&deck[1].gojf_used, sizeof(deck[1].gojf_used), 1, outf);
	deck[1].num_cards = ntohs(deck[1].num_cards);

	for (n = 0 ; n < CC_D.num_cards ; n++) {
		CC_D.offsets[n] = htonl(CC_D.offsets[n]);
		fwrite(&CC_D.offsets[n], sizeof(CC_D.offsets[n]), 1, outf);
	}
	for (n = 0 ; n < CH_D.num_cards ; n++) {
		CH_D.offsets[n] = htonl(CH_D.offsets[n]);
		fwrite(&CH_D.offsets[n], sizeof(CH_D.offsets[n]), 1, outf);
	}

	fclose(outf);
	printf("There were %d com. chest and %d chance cards\n", CC_D.num_cards,
	    CH_D.num_cards);
	return 0;
}

static void
getargs(int ac, char *av[])
{
	if (ac > 1)
		infile = av[1];
	if (ac > 2)
		outfile = av[2];
}

/*
 * count the cards
 */
static void
count(void)
{
	bool	newline;
	DECK	*in_deck;
	int	c;

	newline = TRUE;
	in_deck = &CC_D;
	while ((c=getc(inf)) != EOF)
		if (newline && c == '%') {
			newline = FALSE;
			in_deck->num_cards++;
			if (getc(inf) == '-')
				in_deck = &CH_D;
		}
		else
			newline = (c == '\n');
	in_deck->num_cards++;
}
/*
 *	put strings in the file
 */
static void
putem(void)
{
	bool	newline;
	DECK	*in_deck;
	int	c;
	int16_t	num;

	in_deck = &CC_D;
	CC_D.num_cards = 1;
	CH_D.num_cards = 0;
	CC_D.offsets[0] = ftell(outf);
	putc(getc(inf), outf);
	putc(getc(inf), outf);
	for (num = 0; (c=getc(inf)) != '\n'; )
		num = num * 10 + (c - '0');
	num = htons(num);
	fwrite(&num, sizeof(num), 1, outf);
	newline = FALSE;
	while ((c=getc(inf)) != EOF)
		if (newline && c == '%') {
			putc('\0', outf);
			newline = FALSE;
			if (getc(inf) == '-')
				in_deck = &CH_D;
			while (getc(inf) != '\n')
				continue;
			in_deck->offsets[in_deck->num_cards++] = ftell(outf);
			if ((c=getc(inf)) == EOF)
				break;
			putc(c, outf);
			putc(c = getc(inf), outf);
			for (num = 0; (c=getc(inf)) != EOF && c != '\n'; )
				num = num * 10 + (c - '0');
			num = htons(num);
			fwrite(&num, sizeof(num), 1, outf);
		}
		else {
			putc(c, outf);
			newline = (c == '\n');
		}
	putc('\0', outf);
}
