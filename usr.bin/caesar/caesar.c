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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)caesar.c    8.1 (Berkeley) 5/31/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define	LINELENGTH	2048
#define	ROTATE(ch, perm) \
     isascii(ch) ? ( \
	isupper(ch) ? ('A' + (ch - 'A' + perm) % 26) : \
	    islower(ch) ? ('a' + (ch - 'a' + perm) % 26) : ch) : ch

/*
 * letter frequencies (taken from some unix(tm) documentation)
 * (unix is a trademark of Bell Laboratories)
 */
static double stdf[26] = {
	7.97, 1.35, 3.61, 4.78, 12.37, 2.01, 1.46, 4.49, 6.39, 0.04,
	0.42, 3.81, 2.69, 5.92,  6.96, 2.91, 0.08, 6.63, 8.77, 9.68,
	2.62, 0.81, 1.88, 0.23,  2.07, 0.06,
};

static void printit(char *);

int
main(int argc, char **argv)
{
	int ch, dot, i, nread, winnerdot = 0;
	char *inbuf;
	int obs[26], try, winner;

	if (argc > 1)
		printit(argv[1]);

	if (!(inbuf = malloc((size_t)LINELENGTH))) {
		(void)fprintf(stderr, "caesar: out of memory.\n");
		exit(1);
	}

	/* adjust frequency table to weight low probs REAL low */
	for (i = 0; i < 26; ++i)
		stdf[i] = log(stdf[i]) + log(26.0 / 100.0);

	/* zero out observation table */
	bzero(obs, 26 * sizeof(int));

	if ((nread = read(STDIN_FILENO, inbuf, (size_t)LINELENGTH)) < 0) {
		(void)fprintf(stderr, "caesar: %s\n", strerror(errno));
		exit(1);
	}
	for (i = nread; i--;) {
		ch = (unsigned char) inbuf[i];
		if (isascii(ch)) {
			if (islower(ch))
				++obs[ch - 'a'];
			else if (isupper(ch))
				++obs[ch - 'A'];
		}
	}

	/*
	 * now "dot" the freqs with the observed letter freqs
	 * and keep track of best fit
	 */
	for (try = winner = 0; try < 26; ++try) { /* += 13) { */
		dot = 0;
		for (i = 0; i < 26; i++)
			dot += obs[i] * stdf[(i + try) % 26];
		/* initialize winning score */
		if (try == 0)
			winnerdot = dot;
		if (dot > winnerdot) {
			/* got a new winner! */
			winner = try;
			winnerdot = dot;
		}
	}

	for (;;) {
		for (i = 0; i < nread; ++i) {
			ch = (unsigned char) inbuf[i];
			putchar(ROTATE(ch, winner));
		}
		if (nread < LINELENGTH)
			break;
		if ((nread = read(STDIN_FILENO, inbuf, (size_t)LINELENGTH)) < 0) {
			(void)fprintf(stderr, "caesar: %s\n", strerror(errno));
			exit(1);
		}
	}
	exit(0);
}

static void
printit(char *arg)
{
	int ch, rot;

	if ((rot = atoi(arg)) < 0) {
		(void)fprintf(stderr, "caesar: bad rotation value.\n");
		exit(1);
	}
	while ((ch = getchar()) != EOF)
		putchar(ROTATE(ch, rot));
	exit(0);
}
