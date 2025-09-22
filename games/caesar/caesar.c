/*	$OpenBSD: caesar.c,v 1.21 2019/05/15 15:59:24 schwarze Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Adams.
 *
 * Authors:
 *	Stan King, John Eldridge, based on algorithm suggested by
 *	Bob Morris
 * 29-Sep-82
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
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define	LINELENGTH	2048
#define	ROTATE(ch, perm) \
	isupper(ch) ? ('A' + (ch - 'A' + perm) % 26) : \
	    islower(ch) ? ('a' + (ch - 'a' + perm) % 26) : ch

/*
 * letter frequencies
 */
double stdf[26] = {
	8.1, 1.4, 2.7, 3.8, 13.0, 2.9, 2.0, 5.2, 6.3, 0.13,
	0.4, 3.4, 2.5, 7.1, 7.9, 1.9, 0.11, 6.8, 6.1, 10.5,
	2.4, 0.9, 1.5, 0.15, 1.9, 0.07
};

__dead void printit(int);

int
main(int argc, char *argv[])
{
	int ch, i, nread;
	extern char *__progname;
	const char *errstr;
	char *inbuf;
	int obs[26], try, winner;
	double dot, winnerdot;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	/* check to see if we were called as rot13 */
	if (strcmp(__progname, "rot13") == 0)
		printit(13);

	if (argc > 1) {
		i = strtonum(argv[1], -25, 25, &errstr);
		if (errstr)
			errx(1, "rotation is %s: %s", errstr, argv[1]);
		else
			printit(i);
	}

	if (!(inbuf = malloc(LINELENGTH)))
		err(1, NULL);

	/* adjust frequency table to weight low probs REAL low */
	for (i = 0; i < 26; ++i)
		stdf[i] = log(stdf[i]) + log(26.0 / 100.0);

	/* zero out observation table */
	memset(obs, 0, 26 * sizeof(int));

	nread = 0;
	while ((nread < LINELENGTH) && ((ch = getchar()) != EOF)) {
		inbuf[nread++] = ch;
		if (islower(ch))
			++obs[ch - 'a'];
		else if (isupper(ch))
			++obs[ch - 'A'];
	}

	/*
	 * now "dot" the freqs with the observed letter freqs
	 * and keep track of best fit
	 */
	winnerdot = 0;
	for (try = winner = 0; try < 26; ++try) { /* += 13) { */
		dot = 0;
		for (i = 0; i < 26; i++)
			dot += obs[i] * stdf[(i + try) % 26];
		if (dot > winnerdot) {
			/* got a new winner! */
			winner = try;
			winnerdot = dot;
		}
	}

	/* dump the buffer before calling printit */
	for (i = 0; i < nread; ++i) {
		ch = inbuf[i];
		putchar(ROTATE(ch, winner));
	}
	printit(winner);
}

void
printit(int rot)
{
	int ch;

	if (rot < 0)
		rot = rot + 26;
	while ((ch = getchar()) != EOF)
		putchar(ROTATE(ch, rot));
	exit(0);
}
