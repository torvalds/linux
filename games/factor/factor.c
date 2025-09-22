/*	$OpenBSD: factor.c,v 1.30 2016/09/01 10:48:38 tb Exp $	*/
/*	$NetBSD: factor.c,v 1.5 1995/03/23 08:28:07 cgd Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Landon Curt Noll.
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

/*
 * factor - factor a number into primes
 *
 * By: Landon Curt Noll   chongo@toad.com,   ...!{sun,tolsoft}!hoptoad!chongo
 *
 *   chongo <for a good prime call: 391581 * 2^216193 - 1> /\oo/\
 *
 * usage:
 *	factor [number ...]
 *
 * The form of the output is:
 *
 *	number: factor1 factor1 factor2 factor3 factor3 factor3 ...
 *
 * where factor1 < factor2 < factor3 < ...
 *
 * If no args are given, the list of numbers are read from stdin.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "primes.h"

/*
 * prime[i] is the (i+1)th prime.
 *
 * We are able to sieve 2^32-1 because this byte table yields all primes
 * up to 65537 and 65537^2 > 2^32-1.
 */
extern const ubig prime[];
extern const ubig *pr_limit;		/* largest prime in the prime array */
extern const char pattern[];
extern const int pattern_size;

static void		pr_fact(u_int64_t);	/* print factors of a value */
static void		pr_bigfact(u_int64_t);
static u_int64_t	usqrt(u_int64_t);
static void __dead	usage(void);

int
main(int argc, char *argv[])
{
	u_int64_t val;
	int ch;
	char *p, buf[100];		/* > max number of digits. */

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* No args supplied, read numbers from stdin. */
	if (argc == 0) {
		for (;;) {
			if (fgets(buf, sizeof(buf), stdin) == NULL) {
				if (ferror(stdin))
					err(1, "stdin");
				return 0;
			}
			buf[strcspn(buf, "\n")] = '\0';
			for (p = buf; isblank((unsigned char)*p); ++p)
				;
			if (*p == '\0')
				continue;
			if (*p == '-')
				errx(1, "negative numbers aren't permitted.");
			errno = 0;
			val = strtoull(buf, &p, 10);
			if (errno)
				err(1, "%s", buf);
			for (; isblank((unsigned char)*p); ++p)
				;
			if (*p != '\0')
				errx(1, "%s: illegal numeric format.", buf);
			pr_fact(val);
		}
	/* Factor the arguments. */
	} else {
		for (; *argv != NULL; ++argv) {
			if (argv[0][0] == '-')
				errx(1, "negative numbers aren't permitted.");
			errno = 0;
			val = strtoull(argv[0], &p, 10);
			if (errno)
				err(1, "%s", argv[0]);
			if (*p != '\0')
				errx(1, "%s: illegal numeric format.", argv[0]);
			pr_fact(val);
		}
	}
	return 0;
}

/*
 * pr_fact - print the prime factors of a number
 *
 * If the number is 0 or 1, then print the number and return.
 * If the number is < 0, print -1, negate the number and continue
 * processing.
 *
 * Print the factors of the number, from the lowest to the highest.
 * A prime factor will be printed as often as it divides the value.
 *
 * Prime factors are printed with leading spaces.
 */
static void
pr_fact(u_int64_t val)		/* Factor this value. */
{
	const ubig *fact;	/* The factor found. */

	/* Firewall - catch 0 and 1. */
	if (val == 0)		/* Historical practice; 0 just exits. */
		exit(0);
	if (val == 1) {
		(void)printf("1: 1\n");
		return;
	}

	/* Factor value. */
	(void)printf("%llu:", val);
	fflush(stdout);
	for (fact = &prime[0]; val > 1; ++fact) {
		/* Look for the smallest factor. */
		do {
			if (val % (long)*fact == 0)
				break;
		} while (++fact <= pr_limit);

		/* Watch for primes larger than the table. */
		if (fact > pr_limit) {
			if (val > BIG)
				pr_bigfact(val);
			else
				(void)printf(" %llu", val);
			break;
		}

		/* Divide factor out until none are left. */
		do {
			(void)printf(" %lu", (unsigned long) *fact);
			val /= (long)*fact;
		} while ((val % (long)*fact) == 0);

		/* Let the user know we're doing something. */
		(void)fflush(stdout);
	}
	(void)putchar('\n');
}

/*
 * At this point, our number may have factors greater than those in primes[];
 * however, we can generate primes up to 32 bits (see primes(6)), which is
 * sufficient to factor a 64-bit quad.
 */
static void
pr_bigfact(u_int64_t val)	/* Factor this value. */
{
	u_int64_t start, stop;
	ubig factor;
	char *q;
	const ubig *p;
	ubig fact_lim, mod;
	char *tab_lim;
	char table[TABSIZE];	/* Eratosthenes sieve of odd numbers */

	start = *pr_limit + 2;
	stop = usqrt(val) + 1;
	if ((stop & 0x1) == 0)
		stop++;
	/*
	 * Following code barely modified from that in primes(6)
	 *
	 * we shall sieve a bytemap window, note primes and move the window
	 * upward until we pass the stop point
	 */
	while (start < stop) {
		/*
		 * factor out 3, 5, 7, 11 and 13
		 */
		/* initial pattern copy */
		factor = (start%(2*3*5*7*11*13))/2; /* starting copy spot */
		memcpy(table, &pattern[factor], pattern_size-factor);
		/* main block pattern copies */
		for (fact_lim = pattern_size - factor;
		    fact_lim + pattern_size <= TABSIZE;
		    fact_lim += pattern_size) {
			memcpy(&table[fact_lim], pattern, pattern_size);
		}
		/* final block pattern copy */
		memcpy(&table[fact_lim], pattern, TABSIZE-fact_lim);

		if (stop-start > TABSIZE+TABSIZE) {
			tab_lim = &table[TABSIZE]; /* sieve it all */
			fact_lim = usqrt(start + TABSIZE + TABSIZE + 1);
		} else {
			tab_lim = &table[(stop - start)/2]; /* partial sieve */
			fact_lim = usqrt(stop + 1);
		}
		/* sieve for factors >= 17 */
		factor = 17;	/* 17 is first prime to use */
		p = &prime[7];	/* 19 is next prime, pi(19)=7 */
		do {
			/* determine the factor's initial sieve point */
			mod = start % factor;
			if (mod & 0x1)
				q = &table[(factor-mod)/2];
			else
				q = &table[mod ? factor-(mod/2) : 0];
			/* sieve for our current factor */
			for ( ; q < tab_lim; q += factor) {
				*q = '\0'; /* sieve out a spot */
			}
		} while ((factor=(ubig)(*(p++))) <= fact_lim);

		/*
		 * use generated primes
		 */
		for (q = table; q < tab_lim; ++q, start+=2) {
			if (*q) {
				if (val % start == 0) {
					do {
						printf(" %llu", start);
						val /= start;
					} while ((val % start) == 0);
					(void)fflush(stdout);
					stop = usqrt(val) + 1;
					if ((stop & 0x1) == 0)
						stop++;
				}
			}
		}
	}
	if (val > 1)
		printf(" %llu", val);
}

/* Code taken from ping.c */
static u_int64_t
usqrt(u_int64_t n)
{
	u_int64_t y, x = 1;

	if (n == 0 || n == 1)
		return n;

	do { /* newton was a stinker */
		y = x;
		x = n / x;
		x += y;
		x /= 2;
	} while (((y < x) && (x - y) > 1) || (y - x) > 1);

	return x;
}

static void __dead
usage(void)
{
	(void)fprintf(stderr, "usage: %s [number ...]\n", getprogname());
	exit (1);
}
