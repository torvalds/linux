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

#ifndef lint
#include <sys/cdefs.h>
#ifdef __COPYRIGHT
__COPYRIGHT("@(#) Copyright (c) 1989, 1993\
	The Regents of the University of California.  All rights reserved.");
#endif
#ifdef __SCCSID
__SCCSID("@(#)factor.c	8.4 (Berkeley) 5/4/95");
#endif
#ifdef __RCSID
__RCSID("$NetBSD: factor.c,v 1.19 2009/08/12 05:54:31 dholland Exp $");
#endif
#ifdef __FBSDID
__FBSDID("$FreeBSD$");
#endif
#endif /* not lint */

/*
 * factor - factor a number into primes
 *
 * By: Landon Curt Noll   chongo@toad.com,   ...!{sun,tolsoft}!hoptoad!chongo
 *
 *   chongo <for a good prime call: 391581 * 2^216193 - 1> /\oo/\
 *
 * usage:
 *	factor [-h] [number] ...
 *
 * The form of the output is:
 *
 *	number: factor1 factor1 factor2 factor3 factor3 factor3 ...
 *
 * where factor1 <= factor2 <= factor3 <= ...
 *
 * If no args are given, the list of numbers are read from stdin.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "primes.h"

#ifdef HAVE_OPENSSL

#include <openssl/bn.h>

#define	PRIME_CHECKS	5

static void	pollard_pminus1(BIGNUM *); /* print factors for big numbers */

#else

typedef ubig	BIGNUM;
typedef u_long	BN_ULONG;

#define BN_CTX			int
#define BN_CTX_new()		NULL
#define BN_new()		((BIGNUM *)calloc(sizeof(BIGNUM), 1))
#define BN_is_zero(v)		(*(v) == 0)
#define BN_is_one(v)		(*(v) == 1)
#define BN_mod_word(a, b)	(*(a) % (b))

static int	BN_dec2bn(BIGNUM **a, const char *str);
static int	BN_hex2bn(BIGNUM **a, const char *str);
static BN_ULONG BN_div_word(BIGNUM *, BN_ULONG);
static void	BN_print_fp(FILE *, const BIGNUM *);

#endif

static void	BN_print_dec_fp(FILE *, const BIGNUM *);

static void	pr_fact(BIGNUM *);	/* print factors of a value */
static void	pr_print(BIGNUM *);	/* print a prime */
static void	usage(void);

static BN_CTX	*ctx;			/* just use a global context */
static int	hflag;

int
main(int argc, char *argv[])
{
	BIGNUM *val;
	int ch;
	char *p, buf[LINE_MAX];		/* > max number of digits. */

	ctx = BN_CTX_new();
	val = BN_new();
	if (val == NULL)
		errx(1, "can't initialise bignum");

	while ((ch = getopt(argc, argv, "h")) != -1)
		switch (ch) {
		case 'h':
			hflag++;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	/* No args supplied, read numbers from stdin. */
	if (argc == 0)
		for (;;) {
			if (fgets(buf, sizeof(buf), stdin) == NULL) {
				if (ferror(stdin))
					err(1, "stdin");
				exit (0);
			}
			for (p = buf; isblank(*p); ++p);
			if (*p == '\n' || *p == '\0')
				continue;
			if (*p == '-')
				errx(1, "negative numbers aren't permitted.");
			if (BN_dec2bn(&val, buf) == 0 &&
			    BN_hex2bn(&val, buf) == 0)
				errx(1, "%s: illegal numeric format.", buf);
			pr_fact(val);
		}
	/* Factor the arguments. */
	else
		for (; *argv != NULL; ++argv) {
			if (argv[0][0] == '-')
				errx(1, "negative numbers aren't permitted.");
			if (BN_dec2bn(&val, argv[0]) == 0 &&
			    BN_hex2bn(&val, argv[0]) == 0)
				errx(1, "%s: illegal numeric format.", argv[0]);
			pr_fact(val);
		}
	exit(0);
}

/*
 * pr_fact - print the factors of a number
 *
 * Print the factors of the number, from the lowest to the highest.
 * A factor will be printed multiple times if it divides the value
 * multiple times.
 *
 * Factors are printed with leading tabs.
 */
static void
pr_fact(BIGNUM *val)
{
	const ubig *fact;	/* The factor found. */

	/* Firewall - catch 0 and 1. */
	if (BN_is_zero(val))	/* Historical practice; 0 just exits. */
		exit(0);
	if (BN_is_one(val)) {
		printf("1: 1\n");
		return;
	}

	/* Factor value. */

	if (hflag) {
		fputs("0x", stdout);
		BN_print_fp(stdout, val);
	} else
		BN_print_dec_fp(stdout, val);
	putchar(':');
	for (fact = &prime[0]; !BN_is_one(val); ++fact) {
		/* Look for the smallest factor. */
		do {
			if (BN_mod_word(val, (BN_ULONG)*fact) == 0)
				break;
		} while (++fact <= pr_limit);

		/* Watch for primes larger than the table. */
		if (fact > pr_limit) {
#ifdef HAVE_OPENSSL
			BIGNUM *bnfact;

			bnfact = BN_new();
			BN_set_word(bnfact, *(fact - 1));
			if (!BN_sqr(bnfact, bnfact, ctx))
				errx(1, "error in BN_sqr()");
			if (BN_cmp(bnfact, val) > 0 ||
			    BN_is_prime_ex(val, PRIME_CHECKS, NULL, NULL) == 1)
				pr_print(val);
			else
				pollard_pminus1(val);
#else
			pr_print(val);
#endif
			break;
		}

		/* Divide factor out until none are left. */
		do {
			printf(hflag ? " 0x%" PRIx64 "" : " %" PRIu64 "", *fact);
			BN_div_word(val, (BN_ULONG)*fact);
		} while (BN_mod_word(val, (BN_ULONG)*fact) == 0);

		/* Let the user know we're doing something. */
		fflush(stdout);
	}
	putchar('\n');
}

static void
pr_print(BIGNUM *val)
{
	if (hflag) {
		fputs(" 0x", stdout);
		BN_print_fp(stdout, val);
	} else {
		putchar(' ');
		BN_print_dec_fp(stdout, val);
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: factor [-h] [value ...]\n");
	exit(1);
}

#ifdef HAVE_OPENSSL

/* pollard p-1, algorithm from Jim Gillogly, May 2000 */
static void
pollard_pminus1(BIGNUM *val)
{
	BIGNUM *base, *rbase, *num, *i, *x;

	base = BN_new();
	rbase = BN_new();
	num = BN_new();
	i = BN_new();
	x = BN_new();

	BN_set_word(rbase, 1);
newbase:
	if (!BN_add_word(rbase, 1))
		errx(1, "error in BN_add_word()");
	BN_set_word(i, 2);
	BN_copy(base, rbase);

	for (;;) {
		BN_mod_exp(base, base, i, val, ctx);
		if (BN_is_one(base))
			goto newbase;

		BN_copy(x, base);
		BN_sub_word(x, 1);
		if (!BN_gcd(x, x, val, ctx))
			errx(1, "error in BN_gcd()");

		if (!BN_is_one(x)) {
			if (BN_is_prime_ex(x, PRIME_CHECKS, NULL, NULL) == 1)
				pr_print(x);
			else
				pollard_pminus1(x);
			fflush(stdout);

			BN_div(num, NULL, val, x, ctx);
			if (BN_is_one(num))
				return;
			if (BN_is_prime_ex(num, PRIME_CHECKS, NULL,
			    NULL) == 1) {
				pr_print(num);
				fflush(stdout);
				return;
			}
			BN_copy(val, num);
		}
		if (!BN_add_word(i, 1))
			errx(1, "error in BN_add_word()");
	}
}

/*
 * Sigh..  No _decimal_ output to file functions in BN.
 */
static void
BN_print_dec_fp(FILE *fp, const BIGNUM *num)
{
	char *buf;

	buf = BN_bn2dec(num);
	if (buf == NULL)
		return;	/* XXX do anything here? */
	fprintf(fp, "%s", buf);
	free(buf);
}

#else

static void
BN_print_fp(FILE *fp, const BIGNUM *num)
{
	fprintf(fp, "%lx", (unsigned long)*num);
}

static void
BN_print_dec_fp(FILE *fp, const BIGNUM *num)
{
	fprintf(fp, "%lu", (unsigned long)*num);
}

static int
BN_dec2bn(BIGNUM **a, const char *str)
{
	char *p;

	errno = 0;
	**a = strtoul(str, &p, 10);
	return (errno == 0 && (*p == '\n' || *p == '\0'));
}

static int
BN_hex2bn(BIGNUM **a, const char *str)
{
	char *p;

	errno = 0;
	**a = strtoul(str, &p, 16);
	return (errno == 0 && (*p == '\n' || *p == '\0'));
}

static BN_ULONG
BN_div_word(BIGNUM *a, BN_ULONG b)
{
	BN_ULONG mod;

	mod = *a % b;
	*a /= b;
	return mod;
}

#endif
