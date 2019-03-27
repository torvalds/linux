/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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
static const char copyright[] =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)pac.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * Do Printer accounting summary.
 * Currently, usage is
 *	pac [-Pprinter] [-pprice] [-s] [-r] [-c] [-m] [user ...]
 * to print the usage information for the named people.
 */

#include <sys/param.h>

#include <dirent.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lp.h"
#include "lp.local.h"

static char	*acctfile;	/* accounting file (input data) */
static int	 allflag = 1;	/* Get stats on everybody */
static int	 errs;
static size_t	 hcount;	/* Count of hash entries */
static int	 mflag = 0;	/* disregard machine names */
static int	 pflag = 0;	/* 1 if -p on cmd line */
static float	 price = 0.02;	/* cost per page (or what ever) */
static int	 reverse;	/* Reverse sort order */
static int	 sort;		/* Sort by cost */
static char	*sumfile;	/* summary file */
static int	 summarize;	/* Compress accounting file */

uid_t	uid, euid;

/*
 * Grossness follows:
 *  Names to be accumulated are hashed into the following
 *  table.
 */

#define	HSHSIZE	97			/* Number of hash buckets */

struct hent {
	struct	hent *h_link;		/* Forward hash link */
	char	*h_name;		/* Name of this user */
	float	h_feetpages;		/* Feet or pages of paper */
	int	h_count;		/* Number of runs */
};

static struct	hent	*hashtab[HSHSIZE];	/* Hash table proper */

int		 main(int argc, char **_argv);
static void	 account(FILE *_acctf);
static int	 any(int _ch, const char _str[]);
static int	 chkprinter(const char *_ptrname);
static void	 dumpit(void);
static int	 hash(const char _name[]);
static struct hent 	*enter(const char _name[]);
static struct hent 	*lookup(const char _name[]);
static int	 qucmp(const void *_a, const void *_b);
static void	 rewrite(void);
static void	 usage(void);

int
main(int argc, char **argv)
{
	FILE *acctf;
	const char *cp, *printer;

	printer = NULL;
	euid = geteuid();	/* these aren't used in pac(1) */
	uid = getuid();
	while (--argc) {
		cp = *++argv;
		if (*cp++ == '-') {
			switch(*cp++) {
			case 'P':
				/*
				 * Printer name.
				 */
				printer = cp;
				continue;

			case 'p':
				/*
				 * get the price.
				 */
				price = atof(cp);
				pflag = 1;
				continue;

			case 's':
				/*
				 * Summarize and compress accounting file.
				 */
				summarize++;
				continue;

			case 'c':
				/*
				 * Sort by cost.
				 */
				sort++;
				continue;

			case 'm':
				/*
				 * disregard machine names for each user
				 */
				mflag = 1;
				continue;

			case 'r':
				/*
				 * Reverse sorting order.
				 */
				reverse++;
				continue;

			default:
				usage();
			}
		}
		(void) enter(--cp);
		allflag = 0;
	}
	if (printer == NULL && (printer = getenv("PRINTER")) == NULL)
		printer = DEFLP;
	if (!chkprinter(printer)) {
		printf("pac: unknown printer %s\n", printer);
		exit(2);
	}

	if ((acctf = fopen(acctfile, "r")) == NULL) {
		perror(acctfile);
		exit(1);
	}
	account(acctf);
	fclose(acctf);
	if ((acctf = fopen(sumfile, "r")) != NULL) {
		account(acctf);
		fclose(acctf);
	}
	if (summarize)
		rewrite();
	else
		dumpit();
	exit(errs);
}

static void
usage(void)
{
	fprintf(stderr,
	"usage: pac [-Pprinter] [-pprice] [-s] [-c] [-r] [-m] [user ...]\n");
	exit(1);
}

/*
 * Read the entire accounting file, accumulating statistics
 * for the users that we have in the hash table.  If allflag
 * is set, then just gather the facts on everyone.
 * Note that we must accommodate both the active and summary file
 * formats here.
 * Host names are ignored if the -m flag is present.
 */
static void
account(FILE *acctf)
{
	char linebuf[BUFSIZ];
	double t;
	register char *cp, *cp2;
	register struct hent *hp;
	register int ic;

	while (fgets(linebuf, BUFSIZ, acctf) != NULL) {
		cp = linebuf;
		while (any(*cp, " \t"))
			cp++;
		t = atof(cp);
		while (any(*cp, ".0123456789"))
			cp++;
		while (any(*cp, " \t"))
			cp++;
		for (cp2 = cp; !any(*cp2, " \t\n"); cp2++)
			;
		ic = atoi(cp2);
		*cp2 = '\0';
		if (mflag && strchr(cp, ':'))
		    cp = strchr(cp, ':') + 1;
		hp = lookup(cp);
		if (hp == NULL) {
			if (!allflag)
				continue;
			hp = enter(cp);
		}
		hp->h_feetpages += t;
		if (ic)
			hp->h_count += ic;
		else
			hp->h_count++;
	}
}

/*
 * Sort the hashed entries by name or footage
 * and print it all out.
 */
static void
dumpit(void)
{
	struct hent **base;
	register struct hent *hp, **ap;
	register int hno, runs;
	size_t c;
	float feet;

	hp = hashtab[0];
	hno = 1;
	base = (struct hent **) calloc(sizeof hp, hcount);
	for (ap = base, c = hcount; c--; ap++) {
		while (hp == NULL)
			hp = hashtab[hno++];
		*ap = hp;
		hp = hp->h_link;
	}
	qsort(base, hcount, sizeof hp, qucmp);
	printf("  Login               pages/feet   runs    price\n");
	feet = 0.0;
	runs = 0;
	for (ap = base, c = hcount; c--; ap++) {
		hp = *ap;
		runs += hp->h_count;
		feet += hp->h_feetpages;
		printf("%-24s %7.2f %4d   $%6.2f\n", hp->h_name,
		    hp->h_feetpages, hp->h_count, hp->h_feetpages * price);
	}
	if (allflag) {
		printf("\n");
		printf("%-24s %7.2f %4d   $%6.2f\n", "total", feet,
		    runs, feet * price);
	}
}

/*
 * Rewrite the summary file with the summary information we have accumulated.
 */
static void
rewrite(void)
{
	register struct hent *hp;
	register int i;
	FILE *acctf;

	if ((acctf = fopen(sumfile, "w")) == NULL) {
		warn("%s", sumfile);
		errs++;
		return;
	}
	for (i = 0; i < HSHSIZE; i++) {
		hp = hashtab[i];
		while (hp != NULL) {
			fprintf(acctf, "%7.2f\t%s\t%d\n", hp->h_feetpages,
			    hp->h_name, hp->h_count);
			hp = hp->h_link;
		}
	}
	fflush(acctf);
	if (ferror(acctf)) {
		warn("%s", sumfile);
		errs++;
	}
	fclose(acctf);
	if ((acctf = fopen(acctfile, "w")) == NULL)
		warn("%s", acctfile);
	else
		fclose(acctf);
}

/*
 * Hashing routines.
 */

/*
 * Enter the name into the hash table and return the pointer allocated.
 */

static struct hent *
enter(const char name[])
{
	register struct hent *hp;
	register int h;

	if ((hp = lookup(name)) != NULL)
		return(hp);
	h = hash(name);
	hcount++;
	hp = (struct hent *) calloc(sizeof *hp, (size_t)1);
	hp->h_name = (char *) calloc(sizeof(char), strlen(name)+1);
	strcpy(hp->h_name, name);
	hp->h_feetpages = 0.0;
	hp->h_count = 0;
	hp->h_link = hashtab[h];
	hashtab[h] = hp;
	return(hp);
}

/*
 * Lookup a name in the hash table and return a pointer
 * to it.
 */

static struct hent *
lookup(const char name[])
{
	register int h;
	register struct hent *hp;

	h = hash(name);
	for (hp = hashtab[h]; hp != NULL; hp = hp->h_link)
		if (strcmp(hp->h_name, name) == 0)
			return(hp);
	return(NULL);
}

/*
 * Hash the passed name and return the index in
 * the hash table to begin the search.
 */
static int
hash(const char name[])
{
	register int h;
	register const char *cp;

	for (cp = name, h = 0; *cp; h = (h << 2) + *cp++)
		;
	return((h & 0x7fffffff) % HSHSIZE);
}

/*
 * Other stuff
 */
static int
any(int ch, const char str[])
{
	register int c = ch;
	register const char *cp = str;

	while (*cp)
		if (*cp++ == c)
			return(1);
	return(0);
}

/*
 * The qsort comparison routine.
 * The comparison is ascii collating order
 * or by feet of typesetter film, according to sort.
 */
static int
qucmp(const void *a, const void *b)
{
	register const struct hent *h1, *h2;
	register int r;

	h1 = *(const struct hent * const *)a;
	h2 = *(const struct hent * const *)b;
	if (sort)
		r = h1->h_feetpages < h2->h_feetpages ?
		    -1 : h1->h_feetpages > h2->h_feetpages;
	else
		r = strcmp(h1->h_name, h2->h_name);
	return(reverse ? -r : r);
}

/*
 * Perform lookup for printer name or abbreviation --
 */
static int
chkprinter(const char *ptrname)
{
	int stat;
	struct printer myprinter, *pp = &myprinter;

	init_printer(&myprinter);
	stat = getprintcap(ptrname, pp);
	switch(stat) {
	case PCAPERR_OSERR:
		printf("pac: getprintcap: %s\n", pcaperr(stat));
		exit(3);
	case PCAPERR_NOTFOUND:
		return 0;
	case PCAPERR_TCLOOP:
		fatal(pp, "%s", pcaperr(stat));
	}
	if ((acctfile = pp->acct_file) == NULL)
		errx(3, "accounting not enabled for printer %s", ptrname);
	if (!pflag && pp->price100)
		price = pp->price100/10000.0;
	sumfile = (char *) calloc(sizeof(char), strlen(acctfile)+5);
	if (sumfile == NULL)
		errx(1, "calloc failed");
	strcpy(sumfile, acctfile);
	strcat(sumfile, "_sum");
	return(1);
}
